#include "tobServModule.h"
#include "dbg.h"

char *tobServ_FormRelativePath(tobServ_Querry *querry, char *path)
{
    char *output;

    output = malloc(strlen(querry->modulepath)+strlen(path)+1);
    check_mem(output);

    sprintf(output, "%s%s", querry->modulepath, path);

    return output;

error:
    return NULL;
}
