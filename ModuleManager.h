#ifndef _MODULEMANAGER_H_
#define _MODULEMANAGER_H_

#include <tobServModule.h>

typedef struct _tobServ_module
{
    char name[128];
    char path[256];
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
} tobServ_modulelist;

//Loads Modules and stores them in modulelist
//returns 0 on success
//-1 on error. Error is written to log
//calls init function of modules
int LoadModules(tobServ_modulelist *modulelist, char *path);

//Frees Modules and makes sure they are not used before releasing them.
//calls destroy function of modules
int FreeModules(tobServ_modulelist *modulelist);

#endif
