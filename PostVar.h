#ifndef POSTVAR_H
#define POSTVAR_H

#include "tobServModule.h"

char *GetPostVariable(header *, char *);
int GetPostVariableInt(header*, char*);
int IsPostVariableSet(header*, char*);

#endif
