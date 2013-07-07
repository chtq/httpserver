#ifndef _COMMANDLINEAPI_H_
#define _COMMANDLINEAPI_H_

#include "commandline.h"
#include <stdint.h>

//register a cmd specific to a module
//the registered cmd is called by CMD
//data will be given to cmdfunction
//IMPORTANT: caller needs to ensure 'data' is valid until he calls unregisterCMD
//returns
//0 - on success
//-1 - CMD exists already
int32_t commandlineAPI_registerCMD(tobServ_commandline *commandline, char *CMD, char *description, CMD_function cmdfunction, void *data);

//unregisters CMD
//return:
//0 - success
//-1 - not found
//NOTE: this function copies quite some stuff and is therefore pretty slow. Normally this function is called very rarely so this shouldn't be an issue
int32_t commandlineAPI_unregisterCMD(tobServ_commandline *commandline, char *CMD);

#endif
