#include "PostVar.h"
#include <stdlib.h>
#include <string.h>

char *GetPostVariable(header* headerstruct, char *name)
{
    int i;
    char *result;

    for(i=0;i<headerstruct->numpostdata;i++)
    {
        if(!strcmp(headerstruct->postdata[i].name, name))
        {
            result = malloc(strlen(headerstruct->postdata[i].value)+1);
            stringcpy(result, headerstruct->postdata[i].value, strlen(headerstruct->postdata[i].value)+1);
            return result;
        }
    }

    return NULL;
}

int IsPostVariableSet(header *headerstruct, char *name)
{
    int i;

    for(i=0;i<headerstruct->numpostdata;i++)
    {
        if(!strcmp(headerstruct->postdata[i].name, name))
        {
            return 1;
        }
    }

    return 0;
}

int GetPostVariableInt(header *requestheader, char *variable)
{
    char *temp;
    int result;

    temp = GetPostVariable(requestheader, variable);

    if(!temp)
        return 0;

    result = atoi(temp);
    free(temp);

    return result;
}
