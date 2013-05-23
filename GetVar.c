#include "GetVar.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "dbg.h"

char *GetGetVariable(header* headerstruct, char *name)
{
    uint32_t i;
    char *result;

    for(i=0;i<headerstruct->numgetdata;i++)
    {
        if(!strcmp(headerstruct->getdata[i].name, name))
        {
            result = malloc(strlen(headerstruct->getdata[i].value)+1);
	    check_mem(result);
	    
            stringcpy(result, headerstruct->getdata[i].value, strlen(headerstruct->getdata[i].value)+1);
            return result;
        }
    }

    log_warn("Variable %s was not found during a GetGetVariable call. Use IsGetVariableSet", name);

error: //also execute if not found
    return NULL;
}

uint32_t IsGetVariableSet(header *headerstruct, char *name)
{
    uint32_t i;

    for(i=0;i<headerstruct->numgetdata;i++)
    {
        if(!strcmp(headerstruct->getdata[i].name, name))
            return 1;
    }

    return 0;
}

int32_t GetGetVariableInt(header *requestheader, char *variable)
{
    char *temp;
    int32_t result;

    temp = GetGetVariable(requestheader, variable);

    if(!temp)
    {
	log_warn("GetGetVariableInt was called with a non existant variable %s", variable);
        return 0;
    }

    result = atoi(temp);
    free(temp);

    return result;
}
