#include "commandline.h"
#include "commandlineAPI.h"
#include "globalconstants.h"
#include "ModuleManager.h"
#include "FileCache.h"
#include "Template.h"

#include <stdio.h>
#include <malloc.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <pthread.h>
#include <signal.h>
#include <tobFUNC.h>

void *handle_commandline(void *arg)
{
    char *command;
    tobServ_commandline *commandline;
    uint32_t argc, i;
    char **argv;

    commandline = (tobServ_commandline*)arg;

    //register help function
    commandlineAPI_registerCMD(commandline, "help", "Gives an overview over the commandline commands", commandline_printHelp, commandline);

    while(1)
    {
        command = readline("> ");
        add_history(command);

        if(!strcmp(command, "shutdown") || !strcmp(command, "exit"))
        {
            printf("tobServ going to shutdown......");
            free(command);           

            //tell the mainthread to shutdown
            commandline->doshutdown = 1;
            pthread_kill(commandline->mainthreadID, SIGTERM);
            return 0;
        }
        else
        {
            //explode it by spaces
            argc = explode(&argv, command, " ");
            
            pthread_mutex_lock(&commandline->commandlist_mutex);

            for(i=0;i<commandline->numCommands;i++)
            {
                if(!strcmp(commandline->commands[i].name, argv[0]))
                {
                    //call the registered function
                    //looks messy :/ as function pointers always do
                    if(commandline->commands[i].function(argc, argv, commandline->commands[i].data) < 0)
                    {
                        printf("command handler returned failure. Something seems wrong there");
                    }
                    
                    break;
                }
            }

            if(i==commandline->numCommands)
            {
                printf("Command '%s' not found enter help to get a list of available commands", argv[0]);
            }

            pthread_mutex_unlock(&commandline->commandlist_mutex);

            free(argv);
        }
       
        free(command);
    }
    return 0;
}

int32_t commandline_printServerStats(uint32_t argc, char **argv, void *data)
{
    tobServ_ServerStats *serverstats;

    serverstats = data; //cast

    //NOT THREADSAFE BUT ONLY READINGS SO SHOULDN'T MATTER
    
    printf("tobServ %s Current Status\nThreads: %i/%i PeakThreads: %i TotalRequests: %i\n", VERSION, serverstats->numthreads, serverstats->maxthreads, serverstats->peakthreads, serverstats->numrequests);

    return 0;
}

int32_t commandline_reloadModules(uint32_t argc, char **argv, void *data)
{
    tobServ_modulelist *modulelist;

    modulelist = data; //cast
    
    printf("tobServ going to reload modules....\n");
    
    ModuleManager_FreeModules(modulelist);
    
    if(ModuleManager_LoadModules(modulelist, MODULEFILE) < 0)
    {
        printf("Loading modules failed\n");
    }
    else
    {
        printf("%i Modules were successfully loaded\n", modulelist->count);
    }

    return 0;
}

int32_t commandline_printCacheStats(uint32_t argc, char **argv, void *data)
{
    tobServ_FileCache *filecache;

    filecache = data; //cast

    //NOT THREADSAFE BUT ONLY READINGS SO SHOULDN'T MATTER

    printf("Cached Files: %i/%i with a total size of %iKB\n", filecache->numfiles, filecache->maxfiles, GetTotalFileCacheSize(filecache)/1024);

    return 0;
}

int32_t commandline_printCacheList(uint32_t argc, char **argv, void *data)
{
    unsigned int i, currenttime;
    tobServ_FileCache *filecache;

    filecache = data; //cast

    currenttime = time(NULL);    

    printf("%-50s%-10s%-20s%-5s\n", "Path", "Size", "LastAccess in s", "usecount"); 

    pthread_rwlock_rdlock(&filecache->lock);
    for(i=0;i<filecache->numfiles;i++)
    {
        printf("\n%-50s%-10i%-20i%-5i", filecache->files[i].path, filecache->files[i].file->size, currenttime-filecache->files[i].lastaccess, filecache->files[i].usecount);
    }
    pthread_rwlock_unlock(&filecache->lock);

    if(i>0) //new line for the next cmd
        printf("\n");

    return 0;
}

int32_t commandline_printHelp(uint32_t argc, char **argv, void *data)
{
    tobServ_commandline *commandline;
    uint32_t i;

    commandline = data; //cast: data contains the pointer to the commandline struct

    pthread_mutex_lock(&commandline->commandlist_mutex);
    
    if(argc<2) //no argument (argv[0] is the name of the command itself)
    {
        printf("Available commands:\n");

        for(i=0;i<commandline->numCommands;i++)
        {
            printf("%s\n", commandline->commands[i].name);
        }

        //is not registered as a cmd
        printf("exit/shutdown\n");

        printf("\nFor additional help type 'help <command>'\n");
    }
    else //argument specified -> give help for a specific command
    {
        for(i = 0;i< commandline->numCommands;i++)
        {
            if(!strcmp(commandline->commands[i].name, argv[1]))
                break;
        }

        if(i==commandline->numCommands)
        {
            //is not registered as a cmd
            if(!strcmp(argv[1], "exit") || !strcmp(argv[1], "shutdown"))
            {
                printf("Shuts the server down (obviously)\n");
            }
            else
            {
                printf("Command '%s' not found\n", argv[1]);
            }
        }
        else
        {
            printf("Help for %s:\n%s\n", argv[1], commandline->commands[i].description);
        }
    }

    pthread_mutex_unlock(&commandline->commandlist_mutex);

    return 0;
}
