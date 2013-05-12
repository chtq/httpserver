#include "ModuleManager.h"
#include "logger.h"

#include <pthread.h>
#include <tobCONF.h>
#include <dlfcn.h>

int LoadModules(tobServ_modulelist *modulelist, char *path)
{
    int i, a;
    char logger[1024];
    char *name, *modulepath, *error, *host;
    tobCONF_File modulefile;
    tobCONF_Section *configsection;

    module_INIT_function initModule;

    modulelist->count = 0;
    modulelist->modules = NULL;

    //create lock
    pthread_rwlock_init(&modulelist->lock, NULL);

    //get the modules from file
    if(tobCONF_ReadFile(&modulefile, path)<0)
    {
	    snprintf(logger, sizeof(logger), "ERROR on loading module configfile: %s", tobCONF_GetLastError(&modulefile));
	    write_log("error.txt", logger);
	    tobCONF_Free(&modulefile);
	    return -1;
    }

    configsection = tobCONF_GetFirstSection(&modulefile);

    if(configsection)
    {
	do
	{
	    name = tobCONF_GetElement(configsection, "name");
	    modulepath = tobCONF_GetElement(configsection, "path");
        host = tobCONF_GetElement(configsection, "host");

	    if(name && path && host)
	    {
		modulelist->count++;
		modulelist->modules = realloc(modulelist->modules, sizeof(tobServ_module)*modulelist->count);

		stringcpy(modulelist->modules[modulelist->count-1].name, name, sizeof(modulelist->modules[modulelist->count-1].name));
		stringcpy(modulelist->modules[modulelist->count-1].path, modulepath, sizeof(modulelist->modules[modulelist->count-1].path));
        stringcpy(modulelist->modules[modulelist->count-1].host, host, sizeof(modulelist->modules[modulelist->count-1].host));
	    }
	    else
	    {
		snprintf(logger, sizeof(logger), "ERROR on loading module in section \"%s\"", tobCONF_GetSectionName(configsection));
		write_log("error.txt", logger);
	    }
	} while((configsection = tobCONF_GetNextSection(&modulefile)));
    }

    snprintf(logger, sizeof(logger), "%i modules successfully parsed", modulelist->count);
    write_log("log.txt", logger);
    tobCONF_Free(&modulefile);
    
    //load modules
    dlerror(); //reset error var
    for(i=0; i<modulelist->count; i++)
    {
        modulelist->modules[i].handle = dlopen(modulelist->modules[i].path, RTLD_LAZY);

	error = dlerror();
        if(error)
        {
            snprintf(logger, sizeof(logger), "failed on loading module: %s, REASON: %s", modulelist->modules[i].name, error);
            write_log("error.txt", logger);

            free(modulelist->modules);
            modulelist->modules = NULL;
            modulelist->count = 0;

            return -1;
        }

        modulelist->modules[i].querry_function = dlsym(modulelist->modules[i].handle, "tobModule_QuerryFunction");
        if(dlerror()!=NULL)
        {
            snprintf(logger, 1024, "failed on loading tobModule_QuerryFunction from %s", modulelist->modules[i].name);
            write_log("error.txt", logger);

            free(modulelist->modules);
            modulelist->modules = NULL;
            modulelist->count = 0;
	    
            return -1;
        }

	initModule = dlsym(modulelist->modules[i].handle, "tobModule_InitFunction");
        if(dlerror()!=NULL)
        {
            snprintf(logger, 1024, "failed on loading tobModule_InitFunction from %s", modulelist->modules[i].name);
            write_log("error.txt", logger);

            free(modulelist->modules);
            modulelist->modules = NULL;
            modulelist->count = 0;
	    
            return -1;
        }

	modulelist->modules[i].destroy_function = dlsym(modulelist->modules[i].handle, "tobModule_DestroyFunction");
        if(dlerror()!=NULL)
        {
            snprintf(logger, 1024, "failed on loading tobModule_DestroyFunction from %s", modulelist->modules[i].name);
            write_log("error.txt", logger);

            free(modulelist->modules);
            modulelist->modules = NULL;
            modulelist->count = 0;
	    
            return -1;
        }

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
    for(i=0;i<modulelist->count;i++)
    {
	initModule(modulelist->modules[i].name, modulelist->modules[i].path, &modulelist->modules[i].data);
        if(dlerror()!=NULL)
        {
            snprintf(logger, 1024, "failed on executing tobModule_InitFunction from %s", modulelist->modules[i].name);
            write_log("error.txt", logger);

	    //destroy modules
	    for(a=0;a<i;a++)
		modulelist->modules[a].destroy_function(modulelist->modules[a].name, modulelist->modules[a].path, modulelist->modules[a].data);				

            free(modulelist->modules);
            modulelist->modules = NULL;
            modulelist->count = 0;
	    
            return -1;
        }
    }

    snprintf(logger, sizeof(logger), "%i modules successfully loaded", modulelist->count);
    write_log("log.txt", logger);

    return 0;
}

int FreeModules(tobServ_modulelist *modulelist)
{
    int i;

    pthread_rwlock_wrlock(&modulelist->lock);

    for(i=0; i < modulelist->count; i++)
    {
	modulelist->modules[i].destroy_function(modulelist->modules[i].name, modulelist->modules[i].path, modulelist->modules[i].data);
        dlclose(modulelist->modules[i].handle);
    }

    if(modulelist->modules)
        free(modulelist->modules);

    modulelist->modules = NULL;
    modulelist->count = 0;

    pthread_rwlock_unlock(&modulelist->lock);
    
    pthread_rwlock_destroy(&modulelist->lock);

    return 0;
}
