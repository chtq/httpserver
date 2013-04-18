#include "GetVar.h"
#include <stdlib.h>
#include <string.h>

char *GetGetVariable(header* headerstruct, char *name)
{
    int i;
    char *result;

    for(i=0;i<headerstruct->numgetdata;i++)
    {
        if(!strcmp(headerstruct->getdata[i].name, name))
        {
            result = malloc(strlen(headerstruct->getdata[i].value)+1);
            stringcpy(result, headerstruct->getdata[i].value, strlen(headerstruct->getdata[i].value)+1);
            return result;
        }
    }

    return NULL;
}

int IsGetVariableSet(header *headerstruct, char *name)
{
    int i;

    for(i=0;i<headerstruct->numgetdata;i++)
    {
        if(!strcmp(headerstruct->getdata[i].name, name))
        {
            return 1;
        }
    }

    return 0;
}

int GetGetVariableInt(header *requestheader, char *variable)
{
    char *temp;
    int result;

    temp = GetGetVariable(requestheader, variable);

    if(!temp)
        return 0;

    result = atoi(temp);
    free(temp);

    return result;
}
