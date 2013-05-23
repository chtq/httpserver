#ifndef GETVAR_H
#define GETVAR_H

#include "tobServModule.h"
#include <stdint.h>

char *GetGetVariable(header *, char *name);
int32_t GetGetVariableInt(header*, char *name);
uint32_t IsGetVariableSet(header*, char *name);

#endif
