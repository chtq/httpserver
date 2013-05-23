#include "Sessions.h"
#include "tobServModule.h"

#include <malloc.h>
#include <string.h>
#include <stdint.h>
#include "dbg.h"

int32_t SetSessionVariable(tobServ_Querry *querry, char *name, char *value)
{
    uint32_t i, a;

    check(pthread_mutex_lock(querry->sessionlist->mutex_session)==0, "pthread_mutex_lock failed");

    for(i=0;i<querry->sessionlist->num;i++)
        if((querry->sessionlist->sessions[i].code==querry->code) && (!strcmp(querry->sessionlist->sessions[i].IP, querry->IP)))
            break;

    check(querry->sessionlist->num > i, "No session exists for the query");

    for(a=0;a<querry->sessionlist->sessions[i].num;a++)
    {
        if(!strcmp(querry->sessionlist->sessions[i].variables[a].name, name) && !strcmp(querry->sessionlist->sessions[i].variables[a].module, querry->module))
            break;
    }

    if(a==querry->sessionlist->sessions[i].num) //if doesn't exist create it
    {
        querry->sessionlist->sessions[i].num++;

        querry->sessionlist->sessions[i].variables = realloc(querry->sessionlist->sessions[i].variables, sizeof(tobServ_SessionVariable)*querry->sessionlist->sessions[i].num);
	check_mem(querry->sessionlist->sessions[i].variables);
    }

    //fill
    stringcpy(querry->sessionlist->sessions[i].variables[a].name, name, SESSION_NAME_SIZE);
    stringcpy(querry->sessionlist->sessions[i].variables[a].value, value, SESSION_VALUE_SIZE);
    stringcpy(querry->sessionlist->sessions[i].variables[a].module, querry->module, MODULE_NAME_SIZE);

    pthread_mutex_unlock(querry->sessionlist->mutex_session);

    return 0;

error:
    pthread_mutex_unlock(querry->sessionlist->mutex_session);

    return -1;
}

char *GetSessionVariable(tobServ_Querry *querry, char *name)
{
    uint32_t i,a, length;
    char *result;

    check(pthread_mutex_lock(querry->sessionlist->mutex_session)==0, "pthread_mutex_lock failed");

    for(i=0;i<querry->sessionlist->num;i++)
        if( (querry->sessionlist->sessions[i].code==querry->code) && (!strcmp(querry->sessionlist->sessions[i].IP, querry->IP)))
            break;

    check(querry->sessionlist->num > i, "No session exists for the query");

    for(a=0;a<querry->sessionlist->sessions[i].num;a++)
    {
        if(!strcmp(querry->sessionlist->sessions[i].variables[a].name, name) && !strcmp(querry->sessionlist->sessions[i].variables[a].module, querry->module))
            break;
    }

    if(a==querry->sessionlist->sessions[i].num)
    {
	log_warn("GetSessionVariable was called for %s but it doesn't exist. Use IsSessionVariableSet", name);
	
        goto error;
    }

    length = strlen(querry->sessionlist->sessions[i].variables[a].value) + 1;

    result = malloc(length);
    check_mem(result);

    stringcpy(result, querry->sessionlist->sessions[i].variables[a].value, length);

    pthread_mutex_unlock(querry->sessionlist->mutex_session);

    return result;

error:
    pthread_mutex_unlock(querry->sessionlist->mutex_session);

    return NULL;
}

uint32_t IsSessionVariableSet(tobServ_Querry *querry, char *name)
{
    uint32_t i, a;

    for(i=0;i<querry->sessionlist->num;i++)
        if( (querry->sessionlist->sessions[i].code==querry->code) && (!strcmp(querry->sessionlist->sessions[i].IP, querry->IP)))
            break;

    check(querry->sessionlist->num > i, "No session exists for the query");

    for(i=0;a<querry->sessionlist->sessions[a].num;a++)
    {
        if(!strcmp(querry->sessionlist->sessions[i].variables[a].name, name) && !strcmp(querry->sessionlist->sessions[i].variables[a].module, querry->module))
            break;
    }

    if(a==querry->sessionlist->sessions[i].num)//not found
	return 0;

    return 1;//found

error:
    return 0; //nothing happens deal like it wasn't found
}
