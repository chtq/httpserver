#include "commandline.h"
#include "commandlineAPI.h"
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <tobFUNC.h>
#include <malloc.h>

int32_t commandlineAPI_registerCMD(tobServ_commandline *commandline, char *CMD, char *description, CMD_function cmdfunction, void *data)
{
    tobServ_commandlineCMD *newcommand;
    uint32_t i=0;
    
    pthread_mutex_lock(&commandline->commandlist_mutex);

    //check if it exists already
    for(i=0;i< commandline->numCommands ;i++)
    {
        if(!strcmp(CMD, commandline->commands[i].name))
        {
            pthread_mutex_unlock(&commandline->commandlist_mutex);
            return -1;
        }
    }

    commandline->numCommands++;

    commandline->commands = realloc(commandline->commands, sizeof(tobServ_commandlineCMD)*commandline->numCommands);

    //copy for easier accessibility
    newcommand = &commandline->commands[commandline->numCommands-1];
    
    stringcpy(newcommand->name, CMD, sizeof(newcommand->name));
    stringcpy(newcommand->description, description, sizeof(newcommand->description));
    newcommand->function = cmdfunction;
    newcommand->data = data;

    pthread_mutex_unlock(&commandline->commandlist_mutex);

    return 0;
}

int32_t commandlineAPI_unregisterCMD(tobServ_commandline *commandline, char *CMD)
{
    uint32_t i, a, k;
    tobServ_commandlineCMD *newlist;

    pthread_mutex_lock(&commandline->commandlist_mutex);
    
    for(i=0;i<commandline->numCommands;i++)
    {
        if(!strcmp(CMD, commandline->commands[i].name))
            break;        
    }

    if(i==commandline->numCommands)
    {
        pthread_mutex_unlock(&commandline->commandlist_mutex);
        return -1; //not found
    }

    newlist = malloc(sizeof(tobServ_commandlineCMD)*commandline->numCommands-1);
 
   
    k=0; //counter in the original array
    
    for(a=0;a <commandline->numCommands;a++)
    {
        if(i==a)
        {
            k++; //skip the one to be deleted
        }

        newlist[a] = commandline->commands[k];

        k++;
    }

    pthread_mutex_unlock(&commandline->commandlist_mutex);

    return 0;
}
