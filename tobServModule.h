#ifndef TOBSERVMODULE_H
#define TOBSERVMODULE_H

#include <tobFUNC.h>
#include "FileCache.h"
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "commandline.h"

typedef struct _tobServ_PostData
{
    char *name;
    char *value;
} tobServ_PostData;

typedef struct _tobServ_GetData
{
    char *name;
    char *value;
} tobServ_GetData;

typedef struct _tobServ_HeaderInfo
{
    char name[64];
    char value[256];
} tobServ_HeaderInfo;

typedef struct _header
{
    int32_t success;
    char method[32];
    char path[256];
    char host[256];
    char version[128];
    uint32_t numinfos;
    tobServ_HeaderInfo *infos;
    uint32_t numpostdata;
    tobServ_PostData *postdata;
    uint32_t numgetdata;
    tobServ_GetData *getdata;
} header;

struct _tobServ_SessionList;

typedef struct _tobServ_Querry
{
    uint64_t time;
    char module[128];
    char modulepath[512];
    char IP[20];
    uint32_t type;
    struct _tobServ_SessionList *sessionlist;
    uint32_t code;
    tobServ_FileCache *filecache;
    header *requestheader;
} tobServ_Querry;

typedef struct _tobServ_response
{
    char *response;
    char *type;
    uint32_t code;
    uint32_t usecache;
    uint32_t length;
    uint32_t nocookies;
} tobServ_response;

//action contains the requested path
//querry is filled with the query
//returns a response
typedef tobServ_response (*module_QUERRY_function)(tobServ_Querry querry, char *action, void *data);

//returns 0 on success
//-1 on failure. If Init failed it won't be added to the modulelist
//data can be filled with data
typedef int32_t (*module_INIT_function)(char *modulename, char *modulepath, tobServ_commandline *commandline, void **data);

//returns 0 on success
//-1 on failure. Failure will be ignored by the server
//data is the data pointer from Init
typedef int32_t (*module_DESTROY_function)(char *modulename, char *modulepath, tobServ_commandline *commandline, void *data);

//allocates space for the output, must be freed
char *tobServ_FormRelativePath(tobServ_Querry *querry, char *path);

#endif
