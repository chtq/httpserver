#include "tobServModule.h"
#include <stdlib.h>

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

int InitializeTemplate(tobServ_template *templatehandle)
{
    templatehandle->numvariables = 0;
    templatehandle->variables = NULL;
    templatehandle->numsections = 0;
    templatehandle->sections = NULL;
    templatehandle->numswitches = 0;
    templatehandle->switches = NULL;

    return 0;
}

int FreeTemplate(tobServ_template *templatehandle)
{
    for(i=0;i<numvariables;i++)
    {
	tobString_Free(&templatehandle->variables[i].name);
	tobString_Free(&templatehandle->variables[i].replace);
    }
    free(templatehandle->variables);

    
	

    

    return 0;
}

int AddTemplateVariable(tobServ_template *templatehandle, char *name, char *replace)
{
    int newnum;

    newnum = templatehandle->num+1;
    templatehandle->items = realloc(templatehandle->items, sizeof(tobServ_TemplateVariable)*newnum);

    templatehandle->items[templatehandle->num].name = malloc(strlen(name)+1);
    strcpy(templatehandle->items[templatehandle->num].name, name);

    templatehandle->items[templatehandle->num].replace = malloc(strlen(replace)+1);
    strcpy(templatehandle->items[templatehandle->num].replace, replace);

    templatehandle->num = newnum;

    return 0;
}

int AddTemplateSection(tobServ_template *templatehandle, char *name)
{
    int newnum;

    newnum = templatehandle->numsections+1;
    templatehandle->sections = realloc(templatehandle->sections, sizeof(tobServ_TemplateSection)*newnum);

    templatehandle->sections[templatehandle->numsections].name = malloc(strlen(name)+1);
    strcpy(templatehandle->sections[templatehandle->numsections].name, name);

    templatehandle->sections[templatehandle->numsections].numrows = 0;
    templatehandle->sections[templatehandle->numsections].rows = NULL;

    templatehandle->numsections = newnum;

    return (newnum-1);
}

tobServ_template AddTemplateSectionRow(tobServ_template *templatehandle, int sectionID)
{
    int newnum;

    newnum = templatehandle->sections[sectionID].numrows+1;
    templatehandle->sections[sectionID].rows = realloc(templatehandle->sections[sectionID].rows, sizeof(tobServ_template*) * newnum);

    //get space for the new row template and initialize it
    templatehandle->sections[sectionID].rows[templatehandle->sections[sectionID].numrows] = malloc(sizeof(tobServ_template));
    InitializeTemplate(templatehandle->sections[sectionID].rows[templatehandle->sections[sectionID].numrows]);

    templatehandle->sections[sectionID].numrows = newnum;

    return templatehandle->sections[sectionID].rows[templatehandle->sections[sectionID].numrows-1];
}

tobServ_parsedFile ParseFile(tobServ_file *file)
{
    tobServ_parsedFile result;

    result = ParseFileSubString(file.content, file.size);
    result.type = 0;

    return result;
}

tobServ_parsedFile ParseFileSubString(char *string, int size)
{
    int i, a;
    tobServ_parsedFile result;
    char name[MAX_VARIABLE_LENGTH];
    tobString text;
    tobString switchSET;
    tobString switchUNSET;

    result.numparts = 0;
    result.parts = NULL;

    tobString_Init(&text, 512);
    
    for(i=0;i<size;i++)
    {
	if(string[i] == '%') //VARIABLE POSSIBLY FOUND
	{
	    i++; //go to the next char
	    a=0; //initialize variablename character counter
	    
	    for(;i<size && a<MAX_VARIABLE_LENGTH;i++)
	    {
		if(string[i] == '%') //end found
		{		    
		    name[a] = '\0';
		    
		    if(a>0)//it's only a variable if the variablename length is greater than zero
		    {
			//check if text must be added
			if(text.len>0)
			{
			    result.numparts++;
			    result.parts = realloc(result.parts, sizeof(tobServ_parsedFile)*result.numparts);

			    result.parts[result.numparts-1].type = PARSEDFILE_TEXT;
			    result.parts[result.numparts-1].numparts = 0;
			    result.parts[result.numparts-1].parts = NULL;

			    result.parts[result.numparts-1].name = text;

			    tobString_Init(&text, 512);
			}
			
			result.numparts++;
			result.parts = realloc(result.parts, result.numparts*sizeof(tobServ_parsedFile));

			result.parts[result.numparts-1].type = PARSEDFILE_VARIABLE;
			result.parts[result.numparts-1].numparts = 0;
			result.parts[result.numparts-1].parts = NULL;
			
			tobString_Init(&result.parts[result.numparts-1].name, MAX_VARIABLE_LENGTH);
			tobString_Copy(&result.parts[result.numparts-1].name, name);
		    }
		    else
			tobString_AddChar(&text, "%", strlen("%")); //%% means escaped %

		    break; //we are done
		}
		else
		{
		    //save char and go to the next one
		    name[a] = string[i];
		    a++;
		}
	    }

	    if(a==MAX_VARIABLE_LENGTH || i==size) //the variablename is too long or we have reached the end without finding the second %
	    {
		//add everything but don't replace anything
		tobString_AddChar(&text, '%');
		tobString_Add(&text, name, a);
	    }
	}
	else if(string[i] == '[' && string[i+1] != '/') //SECTION POSSIBLY FOUND [/ is reserved for section endings
	{
	    i++; //go to the next char
	    a=0; //reset name index
	    
	    for(;i<size && a<MAX_VARIABLE_LENGTH;i++)
	    {
		if(string[i] == ']') //end found
		{
		    name[a] = '\0';
		    
		    if(a>0) //it's only a section if the sectionname length is greater than zero
		    {
			tobString_Init(&sectioncontent, 512); //ALLOC SIZE OF 512
				
			//search for the ending
			for(;i< ( size - a - 3) ;i++) // there still has to be enough space for the test
			{
			    snprintf(buffer, sizeof(buffer), "[/%s]", name);
				    
			    if(!strcmp(string+i, name))
			    {
				//check if text must be added
				if(text.len>0)
				{
				    result.numparts++;
				    result.parts = realloc(result.parts, sizeof(tobServ_parsedFile)*result.numparts);

				    result.parts[result.numparts-1].type = PARSEDFILE_TEXT;
				    result.parts[result.numparts-1].numparts = 0;
				    result.parts[result.numparts-1].parts = NULL;

				    result.parts[result.numparts-1].name = text;

				    tobString_Init(&text, 512);
				}
				
				result.numparts++;
				result.parts = realloc(result.parts, sizeof(tobServ_parsedFile)*result.numparts);

				result.parts[result.numparts-1] = ParseFileSubString(sectioncontent.str, sectioncontent.len);

				result.parts[result.numparts-1].type = PARSEDFILE_SECTION;
			
				tobString_Init(&result.parts[result.numparts-1].name, MAX_VARIABLE_LENGTH);
				tobString_Copy(&result.parts[result.numparts-1].name, name);				

				i += a + 3; //put the index behind the section
				break;
			    }
			    else
				tobString_AddChar(&sectioncontent, string[i]);
			}

			if(i == size - a - 3) //ending wasn't found till the very end
			{
			    tobString_AddChar(&text, '['); //add the leading char
			    tobString_Add(&text, name, a); //add the name
			    tobString_AddChar(&text, ']'); //add the closing char
			    tobString_Add(&text, sectioncontent.str, sectioncontent.len);
			}

			tobString_Free(&sectioncontent);
		    }
		    else
			tobString_Add(&text, "[]", strlen("[]")); //no replacement needed because it isn't a valid sectionname

		    break;//we are done
		}

		name[a] = string[i];
		a++;
	    }
	}
	else if(string[i] == '{') //SWITCH possibly found
	{
	    i++; //go to the next char
	    a=0; //reset name index
	    
	    for(;i<size && a<MAX_VARIABLE_LENGTH;i++)
	    {
		if(string[i] == '}') //end found
		{
		    i++;
		    if(original->content[i] != '{') // { must follow right after
		    {
			//no switch found add everything
			tobString_AddChar(&text, '{');
			tobString_Add(&text, name, a);
			tobString_AddChar(&text, '}');
		    }
		    else
		    {
			i++; //next char
			tobString_Init(&switchSET, 512); //512 ALLOC size
			tobString_Init(&switchUNSET, 512); //512 ALLOC size

			//get the onset part of the switch
			for(;i<size;i++)
			{
			    if(string[i] == '}')
				break;

			    tobString_AddChar(&switchSET, string[i]);
			}
			if(i==size) //couldn't find '}'
			{
			    tobString_AddChar(&text, '{');
			    tobString_Add(&text, name, a);
			    tobString_Add(&text, "}{", strlen("}{"));
			    tobString_Add(&text, switchSET.str, switchSET.len);
			}
			else
			{
			    //it was found now search for the unset part of the switch
			    i++; //advance
			    if(string[i] != '{') //opening { must follow right after
			    {
				tobString_AddChar(&text, '{');
				tobString_Add(&text, name, a);
				tobString_Add(&text, "}{", strlen("}{"));
				tobString_Add(&text, switchSET.str, switchSET.len);
				tobString_AddChar(&text, '}');
			    }
			    else
			    {
				i++; //next char
				for(;i<size;i++)
				{
				    if(string[i] == '}')
					break;

				    tobString_AddChar(&switchUNSET, string[i]);
				}
				
				if(i==size) //couldn't find '}'
				{
				    tobString_AddChar(&text, '{');
				    tobString_Add(&text, name, a);
				    tobString_Add(&text, "}{", strlen("}{"));
				    tobString_Add(&text, switchSET.str, switchSET.len);
				    tobString_Add(&text, "}{", strlen("}{"));
				    tobString_Add(&text, switchUNSET.str, switchUNSET.len);
				}
				else
				{
				    //everything was found seems all valid

				    //check if text must be added
				    if(text.len>0)
				    {
					result.numparts++;
					result.parts = realloc(result.parts, sizeof(tobServ_parsedFile)*result.numparts);

					result.parts[result.numparts-1].type = PARSEDFILE_TEXT;
					result.parts[result.numparts-1].numparts = 0;
					result.parts[result.numparts-1].parts = NULL;

					result.parts[result.numparts-1].name = text;

					tobString_Init(&text, 512);
				    }

				    result.numparts++;
				    result.parts = realloc(result.parts, sizeof(tobServ_parsedFile)*result.numparts);

				    //add switchSET and switchUNSET parsed
				    result.parts[result.numparts-1].type = PARSEDFILE_SWITCH;
				    result.parts[result.numparts-1].numparts = 2;
				    result.parts[result.numparts-1].parts = malloc(sizeof(tobServ_parsedFile)*2);

				    result.parts[result.numparts-1].parts[0] = ParseFileSubString(switchSET.str, switchSET.len);
				    result.parts[result.numparts-1].parts[1] = ParseFileSubString(switchUNSET.str, switchUNSET.len);			    
				}
			    }
			}			

		        tobString_Free(&switchSET);
			tobString_Free(&switchUNSET);
		    }
		    
		    break;//done searching for the var name
		}

		//part of the name
		name[a] = string[i];
		a++;
	    }

	    if(a==MAX_VARIABLE_LENGTH || i==size) //the variablename is too long or we have reached the end without finding the closing bracket
	    {
		//add everything but don't replace anything
		tobString_AddChar(&text, '{');
		tobString_Add(&text, name, a);
	    }
	}
	else
	    tobString_AddChar(&text, string[i]);
    }
}

tobString TemplateReplace(tobServ_template *templatehandle, tobServ_parsedFile *parsed)
{
    int i, a;
    tobString result;
    tobString subresult;

    tobString_Init(&result);
    
    for(int i=0;i<parsed->numparts;i++)
    {
	switch(parsed->parts[i].type)
	{
	case PARSEDFILE_TEXT:
	    tobString_Add(&result, parsed->parts[i].name.str, parsed->parts[i].name.len);
	    break;
	    
	case PARSEDFILE_VARIABLE:
	    for(a=0;a<templatehandle->numvariables;a++)
	    {
		if(!strcmp(templatehandle->variables[a].name.str, parsed->parts[i].name.str))
		{
		    tobString_Add(&result, templatehandle->variables[a].replace.str, templatehandle->variables[a].replace.len);
		    break;
		}
	    }
	    if(a==templatehandle->numvariables) //variable not found
	    {
		//add the var unreplaced
		tobString_AddChar(&result, '%');
		tobString_Add(&result, templatehandle->variables[a].name.str, templatehandle->variables[a].name.len);
		tobString_AddChar(&result, '%');
	    }
	    break;
	    
	case PARSEDFILE_SECTION:
	    for(a=0;a<templatehandle->numsections;a++)
	    {
		if(!strcmp(templatehandle->sections[a].name.str, parsed->parts[i].name.str))
		{
		    for(b=0;b<templatehandle->sections[a].numrows;b++)
		    {
			subresult = TemplateReplace(templatehandle->sections[a].rows[b], &parsed->parts[i]); //recursively fill the sectioncontent
			tobString_Add(&result, subresult.str, subresult.len);
			tobString_Free(subresult);
		    }
		    break;
		}
	    }
	    if(a==templatehandle->numsections)//section not found
	    {
		//add some error
		tobString_AddChar(&result, '[');
		tobString_Add(&result, parsed->parts[i].name.str, parsed->parts[i].name.len);
		tobString_AddChar(&result, ']');

		tobString_Add(&result, "ERROR: Section was not found in the template", strlen("ERROR: Section was not found in the template"));

		tobString_Add(&result, "[/", strlen("[/"));
		tobString_Add(&result, parsed->parts[i].name.str, parsed->parts[i].name.len);
		tobString_Add(&result, ']');
	    }
	    break;
	    
	case PARSEDFILE_SWITCH:
	    for(a=0;a<templatehandle->numswitches;a++)
	    {
		if(!strcmp(templatehandle->switches[a].name.str, parsed->parts[i].name.str))
		{
		    subresult = TemplateReplace(templatehandle, &parsed->parts[i].parts[0]); //recursively fill the switchSET
		    tobString_Add(&result, subresult.str, subresult.len);
		    tobString_Free(subresult);
		    break;
		}
	    }
	    if(a==templatehandle->numswitches)
	    {
		subresult = TemplateReplace(templatehandle, &parsed->parts[i].parts[1]); //recursively fill the switchUNSET
		tobString_Add(&result, subresult.str, subresult.len);
		tobString_Free(subresult);
	    }

	    break;
	}
    }

    return result;
}

int SetSessionVariable(tobServ_Querry *querry, char *name, char *value)
{
    int i, a;

    pthread_mutex_lock(querry->sessionlist->mutex_session);

    for(i=0;i<querry->sessionlist->num;i++)
        if((querry->sessionlist->sessions[i].code==querry->code) && (!strcmp(querry->sessionlist->sessions[i].IP, querry->IP)))
            break;

    if(querry->sessionlist->num == i)
    {
        pthread_mutex_unlock(querry->sessionlist->mutex_session);
        return -1;
    }

    for(a=0;a<querry->sessionlist->sessions[i].num;a++)
    {
        if(!strcmp(querry->sessionlist->sessions[i].variables[a].name, name) && !strcmp(querry->sessionlist->sessions[i].variables[a].module, querry->module))
            break;
    }

    if(a==querry->sessionlist->sessions[i].num)
    {
        querry->sessionlist->sessions[i].num++;

        querry->sessionlist->sessions[i].variables = realloc(querry->sessionlist->sessions[i].variables, sizeof(tobServ_SessionVariable)*querry->sessionlist->sessions[i].num);
    }

    stringcpy(querry->sessionlist->sessions[i].variables[a].name, name, 128);
    stringcpy(querry->sessionlist->sessions[i].variables[a].value, value, 1024);
    stringcpy(querry->sessionlist->sessions[i].variables[a].module, querry->module, 128);

    pthread_mutex_unlock(querry->sessionlist->mutex_session);

    return 0;
}

char *GetSessionVariable(tobServ_Querry *querry, char *name)
{
    int i,a, length;
    char *result;

    pthread_mutex_lock(querry->sessionlist->mutex_session);

    for(i=0;i<querry->sessionlist->num;i++)
        if( (querry->sessionlist->sessions[i].code==querry->code) && (!strcmp(querry->sessionlist->sessions[i].IP, querry->IP)))
            break;

    if(querry->sessionlist->num == i)
    {
        pthread_mutex_unlock(querry->sessionlist->mutex_session);
        return NULL;
    }

    for(a=0;a<querry->sessionlist->sessions[i].num;a++)
    {
        if(!strcmp(querry->sessionlist->sessions[i].variables[a].name, name) && !strcmp(querry->sessionlist->sessions[i].variables[a].module, querry->module))
            break;
    }

    if(a==querry->sessionlist->sessions[i].num)
    {
        pthread_mutex_unlock(querry->sessionlist->mutex_session);
        return NULL;
    }

    length = strlen(querry->sessionlist->sessions[i].variables[a].value) + 1;

    result = malloc(length);
    stringcpy(result, querry->sessionlist->sessions[i].variables[a].value, length);

    pthread_mutex_unlock(querry->sessionlist->mutex_session);

    return result;
}

tobServ_file get_file(char *path)
{
    FILE *handle;
    char *buffer;
    char *type;
    size_t result;
    tobServ_file file;
    int size;

    type = malloc(100);

    handle = fopen(path, "rb");
    if (handle==NULL)
    {
        file.content = NULL;
        file.size = 0;
        file.type = NULL;

        free(type);

        return file;
    }

    fseek(handle, 0, SEEK_END);
    size = ftell(handle);
    rewind (handle);

    buffer = malloc(size);

    result = fread(buffer, 1, size, handle);
    if(result != size)
    {
        file.content = NULL;
        file.size = 0;
        file.type = NULL;

        free(type);

        free(buffer);

        return file;
    }

    get_file_type(type, 100, path);

    fclose(handle);

    file.size = size;
    file.content = buffer;
    file.type = type;

    return file;
}

int free_file(tobServ_file file)
{
    if(file.content)
        free(file.content);
    if(file.type)
        free(file.type);

    return 0;
}

int get_file_type(char *type, int size, char *path)
{
    char *ending=NULL;

    char *temptype;

    ending = strrchr(path,'.');

    if(ending==NULL)
        temptype = "application/octet-stream";
    else if(!strcmp((ending+1), "html") || !strcmp((ending+1), "htm"))
        temptype = "text/html";
    else if(!strcmp((ending+1), "jpeg") || !strcmp((ending+1), "jpg") || !strcmp((ending+1), "jpe"))
        temptype = "image/jpeg";
    else if(!strcmp((ending+1), "gif"))
        temptype = "image/gif";
    else if(!strcmp((ending+1), "png"))
        temptype = "image/png";
    else if(!strcmp((ending+1), "txt"))
        temptype = "text/plain";
    else if(!strcmp((ending+1), "ico"))
        temptype = "image/x-ico";
    else
        temptype = "application/octet-stream";

    stringcpy(type, temptype, size);

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

int SetTemplateSwitch(tobServ_template *template, char *name)
{
    templatehandle->numswitches++;

    templatehandle->switches = realloc(templatehandle->switches, sizeof(tobServ_TemplateSwitch)*templatehandle->numswitches);

    templatehandle->switches[templatehandle->numswitches-1].name = malloc(strlen(name)+1);
    strcpy(templatehandle->switches[templatehandle->numswitches-1].name, name);

    return 0;
}

int SetTemplateSectionSwitch(tobServ_template *templatehandle, int sectionID, int rowID, char *name)
{
    templatehandle->sections[sectionID].rows[rowID].numswitches++;

    templatehandle->sections[sectionID].rows[rowID].switches = realloc(templatehandle->sections[sectionID].rows[rowID].switches, sizeof(tobServ_TemplateSwitch)*templatehandle->sections[sectionID].rows[rowID].numswitches);

    templatehandle->sections[sectionID].rows[rowID].switches[templatehandle->sections[sectionID].rows[rowID].numswitches-1].name = malloc(strlen(name)+1);
    strcpy(templatehandle->sections[sectionID].rows[rowID].switches[templatehandle->sections[sectionID].rows[rowID].numswitches-1].name, name);

    return 0;
}
