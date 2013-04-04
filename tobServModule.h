#ifndef TOBSERVMODULE_H
#define TOBSERVMODULE_H

#include <tobFUNC.h>
#include "FileCache.h"
#include <malloc.h>
#include <string.h>
#include <stdio.h>

typedef struct _tobServ_PostData
{
    char *name;
    char *value;
} tobServ_PostData;

typedef struct _tobServ_HeaderInfo
{
    char name[64];
    char value[256];
} tobServ_HeaderInfo;

typedef struct _header
{
    int success;
    char method[32];
    char path[256];
    char version[128];
    unsigned int numinfos;
    tobServ_HeaderInfo *infos;
    unsigned int numpostdata;
    tobServ_PostData *postdata;
} header;

struct _tobServ_SessionList;

typedef struct _tobServ_Querry
{
    int time;
    char module[128];
    char modulepath[512];
    char IP[20];
    int type;
    struct _tobServ_SessionList *sessionlist;
    int code;
    tobServ_FileCache *filecache;
    header *requestheader;
} tobServ_Querry;

typedef struct _tobServ_response
{
    char *response;
    char *type;
    int usecache;
    int length;
} tobServ_response;

typedef tobServ_response (*module_QUERRY_function)(tobServ_Querry querry, char *action);

//allocates space for the output, must be freed
char *tobServ_FormRelativePath(tobServ_Querry *querry, char *path)
{
    char *output;

    output = malloc(strlen(querry->modulepath)+strlen(path)+1+strlen("modules/")+1); // ex: modules/MODULE/INPUT\0

    sprintf(output, "modules/%s/%s", querry->modulepath, path);

    return output;
}

#endif
