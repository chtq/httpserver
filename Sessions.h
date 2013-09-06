#ifndef SESSION_H
#define SESSION_H

#include <pthread.h>
#include <stdint.h>
#include "tobServModule.h"

#define SESSION_NAME_SIZE 128
#define SESSION_VALUE_SIZE 1024
#define MODULE_NAME_SIZE 128

typedef struct _tobServ_SessionVariable
{
    char name[SESSION_NAME_SIZE];
    char value[SESSION_VALUE_SIZE];
    char module[MODULE_NAME_SIZE];
} tobServ_SessionVariable;

typedef struct _tobServ_Session
{
    char IP[20];
    uint64_t code;
    uint32_t num;
    uint32_t expire;
    tobServ_SessionVariable *variables;
} tobServ_Session;

typedef struct _tobServ_SessionList
{
    uint32_t num;
    tobServ_Session *sessions;
    pthread_mutex_t *mutex_session;
    int32_t initialized;
} tobServ_SessionList;

//returns 0 on success <0 on failure
int32_t SetSessionVariable(tobServ_Querry *querry, char *name, char *value);

//returns the corresponding value for name
//returns NULL on failure
char *GetSessionVariable(tobServ_Querry *querry, char *name);

//returns 0 if not set, 1 if set
uint32_t IsSessionVariableSet(tobServ_Querry *querry, char *name);

//returns a pointer to the searched Session or NULL if not found
//uses log search
//IMPORTANT: SESSIONLIST MUST BE LOCKED using its own mutex before this function is called to ensure that no shit happens
tobServ_Session *GetSession(tobServ_SessionList *sessionlist, uint64_t code, char *IP);

#endif
