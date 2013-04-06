#include "Sessions.h"
#include "tobServModule.h"

#include <malloc.h>
#include <string.h>

int SetSessionVariable(tobServ_Querry *querry, char *name, char *value)
{
    int i, a;

    pthread_mutex_lock(querry->sessionlist->mutex_session);

    for(i=0;i<querry->sessionlist->num;i++)
        if((querry->sessionlist->sessions[i].code==querry->code) && (!strcmp(querry->sessionlist->sessions[i].IP, querry->IP)))
            break;

    if(querry->sessionlist->num == i)
    {
        pthread_mutex_unlock(querry->sessionlist->mutex_session);
        return -1;
    }

    for(a=0;a<querry->sessionlist->sessions[i].num;a++)
    {
        if(!strcmp(querry->sessionlist->sessions[i].variables[a].name, name) && !strcmp(querry->sessionlist->sessions[i].variables[a].module, querry->module))
            break;
    }

    if(a==querry->sessionlist->sessions[i].num)
    {
        querry->sessionlist->sessions[i].num++;

        querry->sessionlist->sessions[i].variables = realloc(querry->sessionlist->sessions[i].variables, sizeof(tobServ_SessionVariable)*querry->sessionlist->sessions[i].num);
    }

    stringcpy(querry->sessionlist->sessions[i].variables[a].name, name, SESSION_NAME_SIZE);
    stringcpy(querry->sessionlist->sessions[i].variables[a].value, value, SESSION_VALUE_SIZE);
    stringcpy(querry->sessionlist->sessions[i].variables[a].module, querry->module, MODULE_NAME_SIZE);

    pthread_mutex_unlock(querry->sessionlist->mutex_session);

    return 0;
}

char *GetSessionVariable(tobServ_Querry *querry, char *name)
{
    int i,a, length;
    char *result;

    pthread_mutex_lock(querry->sessionlist->mutex_session);

    for(i=0;i<querry->sessionlist->num;i++)
        if( (querry->sessionlist->sessions[i].code==querry->code) && (!strcmp(querry->sessionlist->sessions[i].IP, querry->IP)))
            break;

    if(querry->sessionlist->num == i)
    {
        pthread_mutex_unlock(querry->sessionlist->mutex_session);
        return NULL;
    }

    for(a=0;a<querry->sessionlist->sessions[i].num;a++)
    {
        if(!strcmp(querry->sessionlist->sessions[i].variables[a].name, name) && !strcmp(querry->sessionlist->sessions[i].variables[a].module, querry->module))
            break;
    }

    if(a==querry->sessionlist->sessions[i].num)
    {
        pthread_mutex_unlock(querry->sessionlist->mutex_session);
        return NULL;
    }

    length = strlen(querry->sessionlist->sessions[i].variables[a].value) + 1;

    result = malloc(length);
    stringcpy(result, querry->sessionlist->sessions[i].variables[a].value, length);

    pthread_mutex_unlock(querry->sessionlist->mutex_session);

    return result;
}
