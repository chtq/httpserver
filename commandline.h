#ifndef _COMMANDLINE_H_
#define _COMMANDLINE_H_

#define MAX_CMD_LENGTH 32
#define MAX_CMD_DESCRIPTION_LENGTH 256

#include <stdint.h>
#include <pthread.h>

//returns 0 on success and -1 on failure
//argc num of args argv args themselves
//data can be filled with data
typedef int32_t (*CMD_function)(uint32_t argc, char **argv, void *data);

typedef struct _tobServ_commandlineCMD
{
    char name[MAX_CMD_LENGTH];
    char description[MAX_CMD_DESCRIPTION_LENGTH];

    CMD_function function;

    void *data;
} tobServ_commandlineCMD;

typedef struct _tobServ_ServerStats
{
    pthread_mutex_t stats_mutex;
    
    uint32_t numthreads;
    uint32_t maxthreads;
    uint32_t numrequests;
    uint32_t peakthreads;
} tobServ_ServerStats;

typedef struct _tobServ_commandline
{
    pthread_t mainthreadID;
    pthread_t commandthreadID;
    uint32_t doshutdown;

    uint32_t numCommands;
    tobServ_commandlineCMD *commands;
    pthread_mutex_t commandlist_mutex;    
} tobServ_commandline;

//prints the server statistics (mostly about used threads)
int32_t commandline_printServerStats(uint32_t argc, char **argv, void *data);

//reloads server modules
int32_t commandline_reloadModules(uint32_t argc, char **argv, void *data);

//prints statistics for the cache
int32_t commandline_printCacheStats(uint32_t argc, char **argv, void *data);

//prints the current file cache to the console
int32_t commandline_printCacheList(uint32_t argc, char **argv, void *data);

//prints help
int32_t commandline_printHelp(uint32_t argc, char **argv, void *data);

//handles the cmdline
//runs in it's own thread created by the server
//arg is a pointer to a tobServ_commandline struct filled by the server
void *handle_commandline(void *arg);

#endif
