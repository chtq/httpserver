#include "PostVar.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "dbg.h"

char *GetPostVariable(header* headerstruct, char *name)
{
    uint32_t i;
    char *result;

    for(i=0;i<headerstruct->numpostdata;i++)
    {
        if(!strcmp(headerstruct->postdata[i].name, name))
        {
            result = malloc(strlen(headerstruct->postdata[i].value)+1);
	    check_mem(result);
	    
            stringcpy(result, headerstruct->postdata[i].value, strlen(headerstruct->postdata[i].value)+1);
            return result;
        }
    }

    log_warn("Variable %s was not found during a GetPostVariable call. Use IsPostVariableSet", name);

error: //also executed if var not found
    return NULL;
}

uint32_t IsPostVariableSet(header *headerstruct, char *name)
{
    uint32_t i;

    for(i=0;i<headerstruct->numpostdata;i++)
    {
        if(!strcmp(headerstruct->postdata[i].name, name))
        {
            return 1;
        }
    }

    return 0;
}

int32_t GetPostVariableInt(header *requestheader, char *variable)
{
    char *temp;
    int result;

    temp = GetPostVariable(requestheader, variable);

    if(!temp)
    {
	log_warn("GetPostVariableInt called for variable %s that was not set", variable);
        return 0;
    }

    result = atoi(temp);
    free(temp);

    return result;
}
