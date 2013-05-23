#ifndef _MODULEMANAGER_H_
#define _MODULEMANAGER_H_

#include <tobServModule.h>
#include <stdint.h>

#define MODULEMANAGER_LASTERROR_SIZE 512
#define MODULEMANAGER_NAME_SIZE 128
#define MODULEMANAGER_PATH_SIZE 256
#define MODULEMANAGER_HOST_SIZE 256

typedef struct _tobServ_module
{
    char name[MODULEMANAGER_LASTERROR_SIZE];
    char path[MODULEMANAGER_NAME_SIZE];
    char host[MODULEMANAGER_HOST_SIZE];
    void *handle;

    void *data;

    module_QUERRY_function querry_function;
    module_DESTROY_function destroy_function;
} tobServ_module;

typedef struct _tobServ_modulelist
{
    tobServ_module *modules;
    int count;

    pthread_rwlock_t lock;

    char lasterror[MODULEMANAGER_LASTERROR_SIZE];
} tobServ_modulelist;

//Loads Modules and stores them in modulelist
//returns 0 on success
//-1 on error. Error is written to log
//calls init function of modules
int32_t ModuleManager_Initialize(tobServ_modulelist *modulelist);

int32_t ModuleManager_LoadModules(tobServ_modulelist *modulelist, char *path);

//Frees Modules and makes sure they are not used before releasing them.
//calls destroy function of modules
int32_t ModuleManager_FreeModules(tobServ_modulelist *modulelist);



#endif
