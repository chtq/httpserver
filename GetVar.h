#ifndef GETVAR_H
#define GETVAR_H

#include "tobServModule.h"

char *GetGetVariable(header *, char *name);
int GeGetVariableInt(header*, char *name);
int IsGetVariableSet(header*, char *name);

#endif
