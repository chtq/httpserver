#ifndef POSTVAR_H
#define POSTVAR_H

#include "tobServModule.h"
#include <stdint.h>

char *GetPostVariable(header *, char *);
int32_t GetPostVariableInt(header*, char*);

//0 if not, 1 if set
uint32_t IsPostVariableSet(header*, char*);

#endif
