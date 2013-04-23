#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <pthread.h>
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <unistd.h>
#include <readline/readline.h>
#include <readline/history.h>

#include <tobCONF.h>
#include <tobFUNC.h>

#include <tobServModule.h>
#include <FileCache.h>
#include <Template.h>
#include <Sessions.h>

#define VERSION "0.1"
#define MODULEFILE "modules.cfg"

typedef struct _tobServ_commandline
{
    pthread_t mainthreadID;
    pthread_t commandthreadID;
    int doshutdown;
    int domodulereload;

    tobServ_FileCache *filecache;

    int numthreads;
    int maxthreads;
    int numrequests;
    int peakthreads;
    pthread_mutex_t commandline_mutex;
} tobServ_commandline;

typedef struct _tobServ_module
{
    char name[128];
    char path[256];
    void *handle;

    module_QUERRY_function querry_function;

} tobServ_module;

typedef struct _tobServ_modulelist
{
    tobServ_module *modules;
    int count;
} tobServ_modulelist;

typedef struct _tobServ_thread
{
    pthread_t threadID;
    int created;
    int connection;
    int last_active;
    pthread_cond_t *finished;
    pthread_mutex_t *mutex;
    tobServ_modulelist modulelist;
    tobServ_Querry querry;
    tobServ_commandline *commandline;
} tobServ_thread;

void error(char *msg)
{
    perror(msg);
    exit(1);
}

void write_log(char *file, char *string)
{
    //awesome log function needed
    printf("%s: %s\n", file, string);
    return;
}

void *handle_request(void *arg);
void send_response(int connection, char *type, char *content, int size, int sessioncode, int usecache, int code);
header get_header(int connection, tobServ_thread*);
tobServ_modulelist LoadModules(char *path);
int FreeModules(tobServ_modulelist);
int FreeResponse(tobServ_response);
int FreeResult(header result);
void *handle_commandline(void *arg);
void main_shutdown_handler(int);
int FreeHeader(header);
int FreeSessions(tobServ_SessionList*);
int StartSession(tobServ_SessionList*, char*, int);
int GetSessionCodeFromCookie(char*);
char *urldecode(char*);
void commandline_printCacheList(tobServ_FileCache *filecache);

int main(int argc, char *argv[])
{
    int sockfd, newsockfd, portno, i;
    unsigned int clilen;

    char IP[20];

    tobServ_thread *threads;
    tobServ_modulelist modulelist;
    int thread_num;
    int done;

    struct sockaddr_in serv_addr, cli_addr;

    pthread_attr_t attr;

    pthread_cond_t thread_finished;
    pthread_mutex_t mutex_finished;

    tobServ_commandline commandline;

    tobServ_SessionList sessionlist;
    pthread_mutex_t mutex_session;

    struct sigaction new_term_action;

    tobServ_FileCache filecache;
    int maxfiles;
    int maxfilesize;

    tobCONF_File configfile;
    tobCONF_Section *configsection;

    //code
    srand(time(NULL));

    if(tobCONF_ReadFile(&configfile, "server.cfg")<0)
    {
	printf("ERROR on reading ConfigFile: %s", tobCONF_GetLastError(&configfile));
        return 0;
    }

    configsection = tobCONF_GetSection(&configfile, "server");
    if(!configsection)
    {
	printf("ERROR config section \"server\" doesn't exist");
	tobCONF_Free(&configfile);
	return 0;
    }

    thread_num = atoi(tobCONF_GetElement(configsection, "maxthreads"));
    if(thread_num<1)
    {
	printf("ERROR invalid value for maxthreads");
	tobCONF_Free(&configfile);
	return 0;	
    }

    portno = atoi(tobCONF_GetElement(configsection, "port"));
    if(portno<1 || portno>65536)
    {
	printf("ERROR invalid value for port");
	tobCONF_Free(&configfile);
	return 0;	
    }

    configsection = tobCONF_GetSection(&configfile, "Cache");
    if(!configsection)
    {
	printf("ERROR config section \"Cache\" doesn't exist");
	tobCONF_Free(&configfile);
	return 0;
    }

    maxfiles = atoi(tobCONF_GetElement(configsection, "maxfiles"));
    if(maxfiles<1)
    {
	printf("ERROR invalid value for maxfiles");
	tobCONF_Free(&configfile);
	return 0;	
    }

    maxfilesize = atoi(tobCONF_GetElement(configsection, "maxfilesize"));
    if(maxfilesize<1)
    {
	printf("ERROR invalid value for maxfilesize");
	tobCONF_Free(&configfile);
	return 0;	
    }

    tobCONF_Free(&configfile);
    //done parsing the config

    new_term_action.sa_handler = main_shutdown_handler;
    sigemptyset (&new_term_action.sa_mask);
    new_term_action.sa_flags = 0;
    sigaction (SIGTERM, &new_term_action, NULL);

    sessionlist.num = 0;
    sessionlist.sessions = NULL;
    pthread_mutex_init(&mutex_session, NULL);
    sessionlist.mutex_session = &mutex_session;

    pthread_cond_init(&thread_finished, NULL);
    pthread_mutex_init(&mutex_finished, NULL);

    threads = malloc(sizeof(tobServ_thread)*thread_num);
    bzero((char *)threads, sizeof(tobServ_thread)*thread_num);

    //LOADING MODULES
    modulelist = LoadModules("modules.cfg");

    if(modulelist.count < 0)
        error("ERROR on loading modules");


    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        error("ERROR on binding");
    listen(sockfd,5);
    clilen = sizeof(cli_addr);

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    commandline.domodulereload = 0;
    commandline.doshutdown = 0;
    commandline.maxthreads = thread_num;
    commandline.numrequests = 0;
    commandline.numthreads = 0;
    commandline.peakthreads = 0;
    commandline.mainthreadID = pthread_self();

    pthread_mutex_init(&commandline.commandline_mutex, NULL);

    //FileCache
    InitializeFileCache(&filecache, maxfiles, maxfilesize);
    commandline.filecache = &filecache;

    //create command handler
    pthread_create(&commandline.commandthreadID, &attr, handle_commandline, (void*)&commandline);

    write_log("log.txt", "Server was successfully started");

    while (!commandline.doshutdown)
    {       
        errno = 0;
        newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
        if (newsockfd < 0)
        {

            if(errno == EINTR)
                continue;
            else
                error("ERROR on accept");
        }
        else
        {
            inet_ntop(AF_INET, &cli_addr.sin_addr.s_addr, IP, 20);

            for(i=0; i<thread_num; i++)
            {
                if(threads[i].threadID==0)
                {
                    threads[i].connection = newsockfd;
                    threads[i].created = time(NULL);
                    threads[i].last_active = time(NULL);
                    threads[i].finished = &thread_finished;
                    threads[i].mutex = &mutex_finished;
                    threads[i].modulelist = modulelist;
                    stringcpy(threads[i].querry.IP, IP, 20);
                    threads[i].querry.time = time(NULL);
                    threads[i].commandline = &commandline;
                    threads[i].querry.sessionlist = &sessionlist;
		            threads[i].querry.filecache = &filecache;
                    
                    pthread_create(&threads[i].threadID, &attr, handle_request, (void*)&threads[i]);

                    break;
                }
            }
            if(i==thread_num) //no open slots close the connection
            {
                close(newsockfd);
            }
        }   
    }

    done = 0;
    while(!done)
    {
        sleep(1);
        pthread_mutex_lock(&commandline.commandline_mutex);
        if(commandline.numthreads == 0)
            done = 1;
        pthread_mutex_unlock(&commandline.commandline_mutex);
    }

    pthread_mutex_destroy(&commandline.commandline_mutex);
    pthread_mutex_destroy(&mutex_finished);
    pthread_cond_destroy(&thread_finished);

    //free everything
    close(sockfd);
    FreeModules(modulelist);
    FreeSessions(&sessionlist);
    FreeFileCache(&filecache);

    free(threads);
    threads = NULL;

    printf("DONE\n");

    return 0;
}

void main_shutdown_handler(int signum)
{
    return;
}

void *handle_request(void *arg)
{
    header request;
    char *module;
    char *action;
    int length;
    char logger[1024];
    char *pathclone;
    tobServ_response response;
    int connection;
    int i, state, code, newcode;


    //update num threads and peak threads
    pthread_mutex_lock(&((tobServ_thread*)arg)->commandline->commandline_mutex);

    ((tobServ_thread*)arg)->commandline->numthreads++;
    ((tobServ_thread*)arg)->commandline->numrequests++;

    if(((tobServ_thread*)arg)->commandline->peakthreads < ((tobServ_thread*)arg)->commandline->numthreads)
        ((tobServ_thread*)arg)->commandline->peakthreads = ((tobServ_thread*)arg)->commandline->numthreads;

    pthread_mutex_unlock(&((tobServ_thread*)arg)->commandline->commandline_mutex);

    connection = ((tobServ_thread*)arg)->connection;

    request = get_header(connection, arg);
    
    if(!request.success)
    {
        close(connection);

        //change commandline thread count
        pthread_mutex_lock(&((tobServ_thread*)arg)->commandline->commandline_mutex);
        ((tobServ_thread*)arg)->commandline->numthreads--;
        pthread_mutex_unlock(&((tobServ_thread*)arg)->commandline->commandline_mutex);


        //mark thread as finished
        pthread_mutex_lock(((tobServ_thread*)arg)->mutex);
        pthread_cond_signal(((tobServ_thread*)arg)->finished);
        ((tobServ_thread*)arg)->threadID = 0;
        pthread_mutex_unlock(((tobServ_thread*)arg)->mutex);

        return 0;
    }
    ((tobServ_thread*)arg)->querry.requestheader = &request;  

    pathclone = malloc(strlen(request.path)+1);
    stringcpy(pathclone, request.path, strlen(request.path)+1);

    // access logger
    time_t rawtime;
    time(&rawtime);
    char log[512];
    snprintf(log, sizeof(log), "%.24s %s %s %s", ctime(&rawtime), ((tobServ_thread*)arg)->querry.IP, request.method, request.path);
    write_log("access.log", log);

    module = pathclone+1;
    action = NULL;

    state = 0;
    length = strlen(module);
    for(i=0; i<length; i++)
    {

        if(!state)
        {
            if(module[i] == '/')
            {
                module[i] = '\0';
                state = 1;
            }
        }
        else
        {
            action = module + i;
            break;
        }

    }

    if(length==0)
        module = "default";

    code = -1;
    for(i=0; i<request.numinfos; i++)
    {
        if(!strcmp(request.infos[i].name, "Cookie"))
        {
            code = GetSessionCodeFromCookie(request.infos[i].value);
        }
    }

    newcode = StartSession(((tobServ_thread*)arg)->querry.sessionlist, ((tobServ_thread*)arg)->querry.IP, code);

    if(newcode==-1)
        newcode = code; //if still in db use old code

    ((tobServ_thread*)arg)->querry.code = newcode;
    stringcpy(((tobServ_thread*)arg)->querry.module, module, 128);

    if((!strcmp(request.method, "GET") || !strcmp(request.method, "POST")) && (pathclone[0] == '\0' || pathclone[0] == '/'))
    {
        for(i=0; i<((tobServ_thread*)arg)->modulelist.count; i++)
        {
            if(!strcmp(((tobServ_thread*)arg)->modulelist.modules[i].name, module))
            {
		//store module path		
		strcpy(((tobServ_thread*)arg)->querry.modulepath, ((tobServ_thread*)arg)->modulelist.modules[i].path);
		    
                response = ((tobServ_thread*)arg)->modulelist.modules[i].querry_function(((tobServ_thread*)arg)->querry, action);

                if(!response.response || !response.type)
                {
                    snprintf(logger, 1024, "failed to get response of %s", ((tobServ_thread*)arg)->modulelist.modules[i].name);
                    write_log("error.txt", logger);
                }
                else
                {
                    send_response(connection, response.type, response.response, response.length, ((tobServ_thread*)arg)->querry.code, response.usecache, response.code);
                    FreeResponse(response);
                    break;
                }
            }
        }
        if(i==((tobServ_thread*)arg)->modulelist.count)
            send_response(connection, "text/html", "tobServ 404", strlen("tobServ 404"), ((tobServ_thread*)arg)->querry.code, 0, 404);
    }
    else
        send_response(connection, "text/html", "Invalid action", strlen("Invalid action"), ((tobServ_thread*)arg)->querry.code, 0, 400);



    close(connection);

    FreeHeader(request);

    free(pathclone);
    pathclone = NULL;

    pthread_mutex_lock(&((tobServ_thread*)arg)->commandline->commandline_mutex);

    ((tobServ_thread*)arg)->commandline->numthreads--;

    pthread_mutex_unlock(&((tobServ_thread*)arg)->commandline->commandline_mutex);




    pthread_mutex_lock(((tobServ_thread*)arg)->mutex);

    pthread_cond_signal(((tobServ_thread*)arg)->finished);
    ((tobServ_thread*)arg)->threadID = 0;

    pthread_mutex_unlock(((tobServ_thread*)arg)->mutex);

    return 0;
}

header get_header(int connection, tobServ_thread *arg)
{
    char *headerstring = NULL;
    unsigned int length;
    unsigned int size;
    unsigned int contentlength;
    unsigned int contentwritten;
    char *content = NULL;
    char *found, *getvarstring;
    char **lines;
    char **words;

    header result;
    char buffer[256];
    unsigned int n,i,a;
    unsigned int headerend;
    unsigned int numlines, num;
    unsigned int numwords;

    size=512;
    length=0;
    headerstring = malloc(size);
    headerstring[0] = '\0';
    result.success = 1;

    while((found = strstr(headerstring, "\r\n\r\n"))==NULL)
    {
        n = read(connection,buffer,255);
        if(n<1)
        {
            result.success = 0;
            free(headerstring);
            return result;
        }
        buffer[n] = '\0';

        if((length+n+1)>size)
        {
            size += 512;

            headerstring = realloc(headerstring, size);
            strcat(headerstring, buffer);
            length += n;
        }
        else
        {
            strcat(headerstring, buffer);
            length += n;
        }
        arg->last_active = time(NULL);

    }

    headerend = (size_t)found - (size_t)headerstring;

    headerend = headerend + strlen("\r\n\r\n");

    headerstring[headerend-1] = '\0';

    numlines = explode(&lines, headerstring, "\r\n");

    if(numlines<3)
    {
        result.numinfos = 0;
        stringcpy(result.method, "INVALID", sizeof(result.method));
        stringcpy(result.path, "DROPPED", sizeof(result.path));
        result.success = 0;
        free(lines);
        lines = NULL;
    }
    else
    {
        numwords = explode(&words, lines[0], " ");
        if(numwords!=3)
        {
            result.numinfos = 0;
            stringcpy(result.method, "INVALID", sizeof(result.method));
            stringcpy(result.path, "DROPPED", sizeof(result.path));
            result.success = 0;
            free(lines);
            free(words);
            lines = NULL;
            words = NULL;
        }
        else
        {
            stringcpy(result.method, words[0], sizeof(result.method));
            stringcpy(result.path, words[1], sizeof(result.path));
            stringcpy(result.version, words[2], sizeof(result.version));

            free(words);
            words = NULL;

            result.numinfos = numlines-2;

            result.infos = malloc(sizeof(tobServ_HeaderInfo)*result.numinfos);

            for(i=1; i<numlines-1; i++)
            {
                if(!strcmp(lines[i], ""))
                    break;

                numwords = explode(&words, lines[i], ": ");

                if(numwords!=2)
                {
                    result.numinfos = 0;
                    stringcpy(result.method, "INVALID", sizeof(result.method));
                    stringcpy(result.path, "DROPPED", sizeof(result.path));
                    free(result.infos);
                    result.infos = NULL;
                    result.success = 0;

                    free(words);
                    words = NULL;
                    break;
                }

                stringcpy(result.infos[i-1].name, words[0], sizeof(result.infos[i].name));
                stringcpy(result.infos[i-1].value, words[1], sizeof(result.infos[i].value));

                free(words);
                words = NULL;
            }
            free(lines);
            lines = NULL;
        }

        contentlength = 0;

        for(i=0; i<result.numinfos; i++)
        {
            if(!strcmp(result.infos[i].name, "Content-Length"))
            {
                contentlength = atoi(result.infos[i].value);
                break;
            }
        }

        content = NULL;
        if(contentlength)
        {
            content = malloc(sizeof(char)*(contentlength+1));

            contentwritten=0;

            for(i=0; i<(length-headerend); i++)
            {
                contentwritten++;
                content[i]=headerstring[headerend+i];
            }

            while(contentwritten<contentlength)
            {
                n = read(connection,buffer,255);
                buffer[n] = '\0';
                if (n < 0)
                    error("ERROR reading from socket");

                if((contentwritten+n)>contentlength)
                {
                    result.numinfos = 0;
                    stringcpy(result.method, "INVALID", sizeof(result.method));
                    stringcpy(result.path, "DROPPED", sizeof(result.path));
                    result.success = 0;
                    free(result.infos);
                    result.infos = NULL;
                    free(lines);
                    lines = NULL;
                    free(words);
                    words = NULL;
                    free(content);
                    content = NULL;
                    break;
                }


                for(i=0; i<n; i++)
                {
                    content[contentwritten]=buffer[i];
                    contentwritten++;
                }
            }

            if(content)
                content[contentwritten] = '\0';

        }

    }

    result.numpostdata = 0;
    result.postdata = NULL;

    if(!strcmp(result.method, "POST") && content)
    {
        result.numpostdata = explode(&lines, content, "&");
	if(result.numpostdata)
	    result.postdata = malloc(sizeof(tobServ_PostData)*result.numpostdata);

        for(i=0; i<result.numpostdata; i++)
        {
            numwords = explode(&words, lines[i], "=");

            if(numwords!=2)
            {
                free(result.postdata);

                result.numpostdata = -1;
                result.postdata = NULL;

                stringcpy(result.method, "INVALID", sizeof(result.method));
                stringcpy(result.path, "DROPPED", sizeof(result.path));
                result.success = 0;
                free(words);
                words = NULL;

                break;
            }
            else
            {
                result.postdata[i].name = malloc(sizeof(char)*(strlen(words[0])+1));
                strcpy(result.postdata[i].name, words[0]);
                result.postdata[i].value = malloc(sizeof(char)*(strlen(words[1])+1));
                strcpy(result.postdata[i].value, words[1]);
                urldecode(result.postdata[i].value);
            }

            free(words);
            words = NULL;
        }

        free(lines);
        lines = NULL;
    }

//parse get variables
result.getdata = NULL;
result.numgetdata = 0;

if((getvarstring = strchr(result.path, '?')))
{
    getvarstring++;//skip the "?"
	
    num = result.numgetdata = explode(&lines, getvarstring, "&");
    if(result.numgetdata)
	    result.getdata = malloc(sizeof(tobServ_GetData)*result.numgetdata);

	a=0;
	for(i=0;i<num;i++)
	{
	    numwords = explode(&words, lines[i], "=");

            if(numwords!=2)
            {
		//invalid get variable
                result.numgetdata--;
		result.getdata = realloc(result.getdata, sizeof(tobServ_GetData)*result.numgetdata);
            }
            else
            {
                result.getdata[a].name = malloc(sizeof(char)*(strlen(words[0])+1));
                strcpy(result.getdata[a].name, words[0]);
                result.getdata[a].value = malloc(sizeof(char)*(strlen(words[1])+1));
                strcpy(result.getdata[a].value, words[1]);
                urldecode(result.getdata[a].value);
		a++;
            }
	    free(words);
	    words = NULL;
	}
	
	free(lines);
	lines = NULL;
    }

    if(headerstring)
        free(headerstring);
    if(content)
        free(content);

    headerstring = NULL;
    content = NULL;

    return result;
}

void send_response(int connection, char *type, char *content, int size, int sessioncode, int usecache, int code)
{
    char headerstring[256];
    int totalsent, sent;
    char *status, *cache;

    switch(code)
    {
    case 200:
	status = "200 OK";
	break;
    case 303:
	status = "303 Moved Permanently";
	break;
    case 400:
	status = "400 Bad Request";
	break;
    case 401:
	status = "401 Unauthorized";
	break;
    case 403:
	status = "402 Forbidden";
	break;
    case 404:
	status = "404 Not Found";
	break;
    case 503:
	status = "503 Service Unavailable";
	break;
    case 451:
	status = "451 Unavailable For Legal Reasons";
	break;
    }

    headerstring[0] = '\0';
    if(usecache)
	cache = "Cache-Control: max-age=10000\r\n";
    else
	cache = "";

    
    snprintf(headerstring, 256, "HTTP/1.1 %s\r\nServer: tobServ V%s\r\nSet-Cookie: session=%i; Path=/; Max-Age=10000; Version=\"1\"\r\n%sContent-Length: %i\r\nContent-Language: de\r\nContent-Type: %s\nConnection: close\r\n\r\n", status, VERSION, sessioncode, cache, size, type);

    write(connection,headerstring,strlen(headerstring));

    sent = send(connection,content,size, MSG_NOSIGNAL);

    if(sent<0)
        return;

    totalsent = sent;

    while(totalsent<size)
    {
        sent = send(connection,content+totalsent,size-totalsent, MSG_NOSIGNAL);

        if(sent<0)
            return;

        totalsent += sent;
    }
}

tobServ_modulelist LoadModules(char *path)
{
    int i, a;
    char logger[1024];
    char *name, *modulepath, *error;
    tobServ_modulelist list;
    tobCONF_File modulefile;
    tobCONF_Section *configsection;

    //get the modules from file
    if(tobCONF_ReadFile(&modulefile, MODULEFILE)<0)
    {
	snprintf(logger, sizeof(logger), "ERROR on loading moduleconfigfile: %s", tobCONF_GetLastError(&modulefile));
	write_log("error.txt", logger);
	list.modules = NULL;
	list.count = -1;
	
	return list;
    }

    list.count = 0;
    list.modules = NULL;

    configsection = tobCONF_GetFirstSection(&modulefile);

    if(configsection)
    {
	do
	{
	    name = tobCONF_GetElement(configsection, "name");
	    modulepath = tobCONF_GetElement(configsection, "path");

	    if(name && path)
	    {
		list.count++;
		list.modules = realloc(list.modules, sizeof(tobServ_module)*list.count);

		stringcpy(list.modules[list.count-1].name, name, sizeof(list.modules[list.count-1].name));
		stringcpy(list.modules[list.count-1].path, modulepath, sizeof(list.modules[list.count-1].path));
	    }
	    else
	    {
		snprintf(logger, sizeof(logger), "ERROR on loading module in section \"%s\"", tobCONF_GetSectionName(configsection));
		write_log("error.txt", logger);
	    }
	} while((configsection = tobCONF_GetNextSection(&modulefile)));
    }

    snprintf(logger, sizeof(logger), "%i modules successfully parsed", list.count);
    write_log("log.txt", logger);

    //load modules
    dlerror(); //reset error var
    for(i=0; i<list.count; i++)
    {
        list.modules[i].handle = dlopen(list.modules[i].path, RTLD_LAZY);

	error = dlerror();
        if(error)
        {
            snprintf(logger, sizeof(logger), "failed on loading module: %s, REASON: %s", list.modules[i].name, error);
            write_log("error.txt", logger);

            free(list.modules);
            list.modules = NULL;

            list.count = -1;
            return list;
        }

        dlerror();
        list.modules[i].querry_function = dlsym(list.modules[i].handle, "tobModule_QuerryFunction");
        if(dlerror()!=NULL)
        {
            snprintf(logger, 1024, "failed on loading tobModule_QuerryFunction from %s", list.modules[i].name);
            write_log("error.txt", logger);

            free(list.modules);
            list.modules = NULL;

            list.count = -1;
            return list;
        }

	//remove the filename from path ex modules/test/test.so to modules/test/ to have a useable relativ path	
	for(a=strlen(list.modules[i].path) ; a>=0 ; a--)
	{
	    if(list.modules[i].path[a] == '/')
	    {
		list.modules[i].path[a+1] = '\0';
		break;
	    }
	}
	if(a<0) //same directory
	    list.modules[i].path[0] = '\0';
    }

    snprintf(logger, sizeof(logger), "%i modules successfully loaded", list.count);
    write_log("log.txt", logger);

    return list;
}

int FreeModules(tobServ_modulelist list)
{
    int i;

    for(i=0; i < list.count; i++)
    {
        dlclose(list.modules[i].handle);
    }

    if(list.modules)
        free(list.modules);

    list.modules = NULL;
    list.count = 0;

    return 0;
}

int FreeResponse(tobServ_response response)
{
    if(response.response)
        free(response.response);
    if(response.type)
        free(response.type);

    response.response = NULL;
    response.type = NULL;

    return 0;
}

int FreeHeader(header result)
{
    unsigned int i;

    if(result.infos)
        free(result.infos);

    for(i=0; i<result.numpostdata; i++)
    {
        if(result.postdata[i].name)
            free(result.postdata[i].name);

        if(result.postdata[i].value)
            free(result.postdata[i].value);
    }    

    if(result.postdata)
        free(result.postdata);

    for(i=0; i<result.numgetdata; i++)
    {
        if(result.getdata[i].name)
            free(result.getdata[i].name);

        if(result.getdata[i].value)
            free(result.getdata[i].value);
    }    

    if(result.getdata)
        free(result.getdata);

    return 0;
}

void *handle_commandline(void *arg)
{
    char *command;
    tobServ_commandline *commandline;

    commandline = (tobServ_commandline*)arg;

    while(1)
    {
        command = readline("> ");
        add_history(command);

        if(!strcmp(command, "help"))
            printf("available commands:\nhelp - shows this message\nreload - reloads the modules\nexit/shutdown - shuts the server down\n");
        else if(!strcmp(command, "shutdown") || !strcmp(command, "exit"))
        {
            commandline->doshutdown = 1;
            printf("tobServ going to shutdown......");
            pthread_kill(commandline->mainthreadID, SIGTERM);
            free(command);
            return 0;
        }

        else if(!strcmp(command, "reload"))
        {
            // TODO: Modulereload  
            printf("reloading modules\n");
            //commandline->domodulereload = 1;  
        }

	//the readings aren't thread safe but who cares. Read operations can't destroy anything
        else if(!strcmp(command, "stats"))
            printf("tobServ %s Current Status\nThreads: %i/%i PeakThreads: %i TotalRequests: %i\n", VERSION, commandline->numthreads, commandline->maxthreads, commandline->peakthreads, commandline->numrequests);
	else if(!strcmp(command, "cache stats"))
	    printf("Cached Files: %i/%i with a total size of %iKB\n", commandline->filecache->numfiles, commandline->filecache->maxfiles, GetTotalFileCacheSize(commandline->filecache)/1024);

	else if(!strcmp(command, "cache list"))
	    commandline_printCacheList(commandline->filecache);

        free(command);
    }
    return 0;
}

void commandline_printCacheList(tobServ_FileCache *filecache)
{
    unsigned int i, currenttime;

    currenttime = time(NULL);    

    printf("%-50s%-10s%-20s%-5s\n", "Path", "Size", "LastAccess in s", "usecount"); 

    pthread_rwlock_rdlock(&filecache->lock);
    for(i=0;i<filecache->numfiles;i++)
    {
	printf("\n%-50s%-10i%-20i%-5i", filecache->files[i].path, filecache->files[i].file->size, currenttime-filecache->files[i].lastaccess, filecache->files[i].usecount);
    }
    pthread_rwlock_unlock(&filecache->lock);

    if(i>0) //new line for the next cmd
	printf("\n");
}

int FreeSessions(tobServ_SessionList *sessionlist)
{
    int i;

    pthread_mutex_lock(sessionlist->mutex_session);

    for(i=0; i<sessionlist->num; i++)
    {
        if(sessionlist->sessions[i].variables)
            free(sessionlist->sessions[i].variables);
    }
    if(sessionlist->sessions)
        free(sessionlist->sessions);

    sessionlist->sessions = NULL;
    sessionlist->num = 0;

    pthread_mutex_unlock(sessionlist->mutex_session);

    return 0;
}

int StartSession(tobServ_SessionList *sessionlist, char *IP, int code) //-1 already exists else new code returned
{
    int i, a, newcode;

    int newnum, currenttime;
    tobServ_Session *newsessions;

    newcode = rand();
    currenttime = time(NULL);

    pthread_mutex_lock(sessionlist->mutex_session);

    if(sessionlist->sessions)
    {
        newnum = sessionlist->num;
        for(i=0; i<sessionlist->num; i++)
        {
            if(sessionlist->sessions[i].expire<currenttime)
                newnum--;
        }

        //if all sessions expired then null the session list
        if(!newnum)
        {
            free(sessionlist->sessions);
            sessionlist->sessions = NULL;
            sessionlist->num = 0;
        }
        else
        {
            newsessions = malloc(sizeof(tobServ_Session)*newnum);

            a=0;
            for(i=0; i<sessionlist->num; i++)
            {
                if(sessionlist->sessions[i].expire > currenttime)
                {
                    newsessions[a].code = sessionlist->sessions[i].code;
                    newsessions[a].num = sessionlist->sessions[i].num;
                    newsessions[a].variables = sessionlist->sessions[i].variables;
                    newsessions[a].expire = sessionlist->sessions[i].expire;
                    stringcpy(newsessions[a].IP, sessionlist->sessions[i].IP, 20);
                    a++;
                }
            }

            free(sessionlist->sessions);
            sessionlist->sessions = newsessions;
            sessionlist->num = newnum;
        }
    }

    for(i=0; i<sessionlist->num; i++)
    {
        if(!strcmp(sessionlist->sessions[i].IP, IP) && sessionlist->sessions[i].code==code)
        {
            sessionlist->sessions[i].expire = time(NULL)+10000;
            pthread_mutex_unlock(sessionlist->mutex_session);
            return -1;
        }
    }
    sessionlist->sessions = realloc(sessionlist->sessions, sizeof(tobServ_Session)*(sessionlist->num+1));

    stringcpy(sessionlist->sessions[sessionlist->num].IP, IP, 20);
    sessionlist->sessions[sessionlist->num].code = newcode;
    sessionlist->sessions[sessionlist->num].num = 0;
    sessionlist->sessions[sessionlist->num].expire = time(NULL)+10000;
    sessionlist->sessions[sessionlist->num].variables = NULL;

    sessionlist->num++;

    pthread_mutex_unlock(sessionlist->mutex_session);

    return newcode;
}

int GetSessionCodeFromCookie(char *cookiestring)
{
    char **cookies;
    int num, i, code;

    num = explode(&cookies, cookiestring, "; ");

    for(i=0; i<num; i++)
    {
        if(!strncmp(cookies[i], "session=", 8))
        {
            code = atoi(cookies[i]+8);
            free(cookies);
            return code;
        }
    }

    if(cookies)
        free(cookies);
    return -1;
}

char *urldecode(char *input)
{
    int i, a;
    int length;
    char buffer[3];
    char replacechar;

    length = strlen(input);

    a=0;

    for(i=0;i<length;i++)
    {
         if(input[i]=='%' && i<(length-2))
         {
             stringcpy(buffer, input+i+1, 3);
             replacechar = strtol(buffer, NULL, 16);
             input[a]=replacechar;
             i+=2;
         }
	 else if(input[i]=='+')
	     input[a] = ' ';
         else if(a < i)
             input[a]=input[i];

         a++;
    }
    input[a] = '\0';

    return input;
}

