#ifndef SESSION_H
#define SESSION_H

#include <pthread.h>
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
    int code;
    int num;
    int expire;
    tobServ_SessionVariable *variables;
} tobServ_Session;

typedef struct _tobServ_SessionList
{
    int num;
    tobServ_Session *sessions;
    pthread_mutex_t *mutex_session;
} tobServ_SessionList;

int SetSessionVariable(tobServ_Querry *querry, char *name, char *value);
char *GetSessionVariable(tobServ_Querry *querry, char *name);

#endif
