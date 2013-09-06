#include "Sessions.h"
#include "tobServModule.h"

#include <malloc.h>
#include <string.h>
#include <stdint.h>
#include "dbg.h"

tobServ_Session *GetSession(tobServ_SessionList *sessionlist, uint64_t code, char *IP)
{
    uint32_t i, a, left, right;

    //search for the correct session using a log search
    left = 0;
    right = sessionlist->num-1;

    while(right >= left)
    {
        //get the middle
        i = (left + right)/2;

        if(sessionlist->sessions[i].code < code)
            left = i + 1;
        else if(sessionlist->sessions[i].code > code)
            right = i -1;
        else //equal
            break;
    }

    check(right >=left, "No session exists for the query");

    check(!strcmp(sessionlist[i].IP, IP), "Codes match but IPs don't. Reason could be a poor random function, IP switch of the user or someone trying to steal someones identity using his cookie");

    return sessionlist[i];

error:
    return NULL;
}

int32_t SetSessionVariable(tobServ_Querry *querry, char *name, char *value)
{
    tobServ_Session *session;

    pthread_mutex_lock(querry->sessionlist->mutex_session);

    session = GetSession(querry->sessionlist, querry->code, querry->IP);

    check(session, "GetSession failed");

    //search for the variable
    //TODO?: ITS A LINEAR SEARCH UPGRADE IF NEEDED BUT NORMALLY THERE SHOULDN'T BE MUCH VARS PER SESSION
    for(a=0;a<session->num;a++)
    {
        if(!session->variables[a].name, name) && !strcmp(sessions->variables[a].module, querry->module))
            break;
    }

    if(a==session->num) //if doesn't exist create it
    {
        session->num++;

        session->variables = realloc(session->variables, sizeof(tobServ_SessionVariable)*session->num);
        check_mem(session->variables);
    }

    //fill
    stringcpy(session->variables[a].name, name, SESSION_NAME_SIZE);
    stringcpy(session->variables[a].value, value, SESSION_VALUE_SIZE);
    stringcpy(session->variables[a].module, querry->module, MODULE_NAME_SIZE);

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

    pthread_mutex_lock(querry->sessionlist->mutex_session);

    for(i=0;i<querry->sessionlist->num;i++)
    {
        if( (querry->sessionlist->sessions[i].code==querry->code) && (!strcmp(querry->sessionlist->sessions[i].IP, querry->IP)))
            break;
    }

    check(querry->sessionlist->num > i, "No session exists for the query");

    for(a=0;a<querry->sessionlist->sessions[i].num;a++)
    {
        if(!strcmp(querry->sessionlist->sessions[i].variables[a].name, name) && !strcmp(querry->sessionlist->sessions[i].variables[a].module, querry->module))
            break;
    }

    check(querry->sessionlist->sessions[i].num != a, "GetSessionVariable was called for %s but it doesn't exist. Use IsSessionVariableSet", name);

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
    return 0; //nothing happens deal like it wasn't found. Error was thrown though because not finding the session in the list is an issue that shouldn't occur
}
