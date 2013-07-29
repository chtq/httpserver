#include "ModuleManager.h"
#include "commandline.h"
#include "logger.h"
#include "dbg.h"

#include <pthread.h>
#include <tobCONF.h>
#include <dlfcn.h>
#include <stdint.h>

int32_t ModuleManager_Initialize(tobServ_modulelist *modulelist, tobServ_commandline *commandline)
{
    modulelist->count = 0;
    modulelist->modules = NULL;
    modulelist->commandline = commandline;

    pthread_rwlock_init(&modulelist->lock, NULL);

    return 0;
}

int32_t ModuleManager_LoadModules(tobServ_modulelist *modulelist, char *path)
{
    int32_t result;
    uint32_t i, a;
    uint32_t isLocked=0;
    uint32_t iterateModule = 0;
    char *name=NULL;
    char *modulepath=NULL;
    char *errorstring=NULL;
    char *host=NULL;
    tobCONF_File modulefile;
    tobCONF_Section *configsection;

    module_INIT_function initModule;

    tobCONF_Initialize(&modulefile);

    //create lock
    check(pthread_rwlock_wrlock(&modulelist->lock)==0, "pthread_rwlock_wrlock failed");
    isLocked = 1;

    //get the modules from file
    check(tobCONF_ReadFile(&modulefile, path)==0, "loading module configfile failed: %s", tobCONF_GetLastError(&modulefile));

    configsection = tobCONF_GetFirstSection(&modulefile);

    if(configsection)
    {
	do
	{
	    name = tobCONF_GetElement(configsection, "name");
	    modulepath = tobCONF_GetElement(configsection, "path");
	    host = tobCONF_GetElement(configsection, "host");

	    check(name && path && host, "loading module section %s failed: fields missing", tobCONF_GetSectionName(configsection));

	    modulelist->count++;
	    modulelist->modules = realloc(modulelist->modules, sizeof(tobServ_module)*modulelist->count);
	    check_mem(modulelist->modules);

	    stringcpy(modulelist->modules[modulelist->count-1].name, name, sizeof(modulelist->modules[modulelist->count-1].name));
	    stringcpy(modulelist->modules[modulelist->count-1].path, modulepath, sizeof(modulelist->modules[modulelist->count-1].path));
	    stringcpy(modulelist->modules[modulelist->count-1].host, host, sizeof(modulelist->modules[modulelist->count-1].host));

	} while((configsection = tobCONF_GetNextSection(&modulefile)));
    }

    log_info("%i modules successfully parsed", modulelist->count);
    tobCONF_Free(&modulefile);
    
    //load modules
    dlerror(); //reset error var
    for(i=0; i<modulelist->count; i++)
    {
        modulelist->modules[i].handle = dlopen(modulelist->modules[i].path, RTLD_LAZY);

	errorstring = dlerror();
	check(!errorstring, "failed on loading module: %s, REASON: %s", modulelist->modules[i].name, errorstring);

        modulelist->modules[i].querry_function = dlsym(modulelist->modules[i].handle, "tobModule_QuerryFunction");
	check(!dlerror(), "failed on loading tobModule_QuerryFunction from %s", modulelist->modules[i].name);

	modulelist->modules[i].destroy_function = dlsym(modulelist->modules[i].handle, "tobModule_DestroyFunction");
	check(!dlerror(), "failed on loading tobModule_DestroyFunction from %s", modulelist->modules[i].name);

	//remove the filename from path ex modules/test/test.so to modules/test/ to have a useable relativ path	
	for(a=strlen(modulelist->modules[i].path) ; a>=0 ; a--)
	{
	    if(modulelist->modules[i].path[a] == '/')
	    {
		modulelist->modules[i].path[a+1] = '\0';
		break;
	    }
	}
	if(a<0) //same directory
	    modulelist->modules[i].path[0] = '\0';
    }

    //call init
    for(iterateModule=0;iterateModule<modulelist->count;iterateModule++)
    {
	initModule = dlsym(modulelist->modules[iterateModule].handle, "tobModule_InitFunction");

        check(initModule, "tobModule_InitFunction not found for module: %s", modulelist->modules[iterateModule].name);

	result = initModule(modulelist->modules[iterateModule].name, modulelist->modules[iterateModule].path, modulelist->commandline, &modulelist->modules[iterateModule].data);
        check(result>=0, "tobModule_InitFunction failed for module: %s", modulelist->modules[iterateModule].name);
    }

    log_info("%i modules successfully loaded", modulelist->count);

    pthread_rwlock_unlock(&modulelist->lock);
    isLocked = 0;

    return 0;

error:
    //destroy but only those which are initialized already
    for(a=0;a<iterateModule;a++)
	modulelist->modules[a].destroy_function(modulelist->modules[a].name, modulelist->modules[a].path, modulelist->commandline, modulelist->modules[a].data);

    if(isLocked)
	pthread_rwlock_unlock(&modulelist->lock);
    
    tobCONF_Free(&modulefile);

    modulelist->count = 0; //make sure FreeModules doesn't call the destroy functions again    
    ModuleManager_FreeModules(modulelist);
    return -1;
}

int32_t ModuleManager_FreeModules(tobServ_modulelist *modulelist)
{
    uint32_t i;

    check(pthread_rwlock_wrlock(&modulelist->lock)==0, "pthread_rwlock_wrlock failed");

    for(i=0; i < modulelist->count; i++)
    {
	if(modulelist->modules[i].destroy_function(modulelist->modules[i].name, modulelist->modules[i].path, modulelist->commandline, modulelist->modules[i].data)<0)
	    log_warn("destroy_function of %s failed", modulelist->modules[i].name); //nothing fatal

        dlclose(modulelist->modules[i].handle);
    }

    if(modulelist->modules)
        free(modulelist->modules);

    modulelist->modules = NULL;
    modulelist->count = 0;

    pthread_rwlock_unlock(&modulelist->lock);

    return 0;

error:
    return -1;
}
