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

#include "tobFUNC.h"

#include "tobServModule.h"

#define VERSION "0.1"

typedef struct _tobServ_commandline
{
    pthread_t mainthreadID;
    pthread_t commandthreadID;
    int doshutdown;
    int domodulereload;

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
    printf("%s: %s", file, string);
    return;
}

void *handle_request(void *arg);
void send_response(int connection, char *type, char *content, int size, int sessioncode, int usecache);
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

    //code
    srand(time(NULL));

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

    if (argc < 3)
    {
        fprintf(stderr, "ERROR, missing arguments 1: port 2: max threads\n");
        exit(1);
    }

    thread_num = atoi(argv[2]);
    portno = atoi(argv[1]);

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

    pathclone = malloc(strlen(request.path)+1);

    stringcpy(pathclone, request.path, strlen(request.path)+1);

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
                response = ((tobServ_thread*)arg)->modulelist.modules[i].querry_function(request, ((tobServ_thread*)arg)->querry, action);

                if(!response.response || !response.type)
                {
                    snprintf(logger, 1024, "failed to get response of %s", ((tobServ_thread*)arg)->modulelist.modules[i].name);
                    write_log("error.txt", logger);
                }
                else
                {
                    send_response(connection, response.type, response.response, response.length, ((tobServ_thread*)arg)->querry.code, response.usecache);
                    FreeResponse(response);
                    break;
                }
            }
        }
        if(i==((tobServ_thread*)arg)->modulelist.count)
            send_response(connection, "text/html", "tobServ 404", strlen("tobServ 404"), ((tobServ_thread*)arg)->querry.code, 0);
    }
    else
        send_response(connection, "text/html", "Invalid action", strlen("Invalid action"), ((tobServ_thread*)arg)->querry.code, 0);



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
    char *found;
    char **lines;
    char **words;

    header result;
    char buffer[256];
    int n,i;
    unsigned int headerend;
    unsigned int numlines;
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

    if(!numlines)
    {
        result.numinfos = 0;
        stringcpy(result.method, "INVALID", sizeof(result.method));
    }
    else
    {
        numwords = explode(&words, lines[0], " ");
        if(numwords!=3)
        {
            result.numinfos = 0;
            stringcpy(result.method, "INVALID", sizeof(result.method));
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
                    free(result.infos);
                    result.infos = NULL;

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
                    free(result.infos);
                    result.infos = NULL;

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
                result.postdata[i].value = urldecode(result.postdata[i].value);
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

void send_response(int connection, char *type, char *content, int size, int sessioncode, int usecache)
{
    char headerstring[256];
    int totalsent, sent;

    headerstring[0] = '\0';
    if(usecache)
        snprintf(headerstring, 256, "HTTP/1.1 200 OK\r\nServer: tobServ V%s\r\nSet-Cookie: session=%i; Path=/; Max-Age=10000; Version=\"1\"\r\nCache-Control: max-age=10000\r\nContent-Length: %i\r\nContent-Language: de\r\nContent-Type: %s\nConnection: close\r\n\r\n", VERSION, sessioncode, size, type);
    else
        snprintf(headerstring, 256, "HTTP/1.1 200 OK\r\nServer: tobServ V%s\r\nSet-Cookie: session=%i; Path=/; Max-Age=10000; Version=\"1\"\r\nContent-Length: %i\r\nContent-Language: de\r\nContent-Type: %s\nConnection: close\r\n\r\n", VERSION, sessioncode, size, type);

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
    FILE *fp;
    int size;
    int i, a;
    char *line;
    char *rest;
    int result;
    char *buffer, *originalbuffer;
    char logger[1024];
    tobServ_modulelist list;

    fp = fopen(path, "rb");

    if(!fp)
    {
        write_log("error.txt", "ERROR on loading ModuleList, couldn't open file");
        list.count = -1;

        return list;
    }

    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    rewind (fp);

    buffer = malloc(size+1);

    result = fread(buffer,1,size,fp);
    if(result != size)
    {
        free(buffer);
        buffer = NULL;

        write_log("error.txt", "ERROR on loading ModuleList, couldn't read everything\n");
        list.count = -1;

        return list;
    }
    fclose(fp);

    buffer[size] = '\0';

    list.count = 0;
    list.modules = NULL;

    originalbuffer = buffer;


    line = strtok_r(buffer, "\n", &rest);

    while(line != NULL)
    {
        i = 0;

        if(!(line[i]=='\r' || line[i]=='#'))
        {
            list.count++;
            list.modules = realloc(list.modules, sizeof(tobServ_module)*list.count);

            while(line[i] == ' ')
                i++;

            if(isalnum(line[i]))
            {
                for(a=0; isalnum(line[i]); a++)
                {
                    if(a>126)
                    {
                        free(list.modules);
                        list.modules = NULL;

                        list.count = -1;
                        write_log("error.txt", "ERROR on parsing modules");
                        return list;
                    }
                    list.modules[list.count-1].name[a] = line[i];
                    i++;
                }
                list.modules[list.count-1].name[a] = '\0';

                while(line[i] == ' ')
                    i++;

                if(isalnum(line[i]))
                {
                    for(a=0; isalnum(line[i]) || line[i]=='/' || line[i]=='.'; a++)
                    {
                        if(a>254)
                        {
                            free(list.modules);
                            list.modules = NULL;

                            list.count = -1;
                            write_log("error.txt", "ERROR on parsing modules");
                            return list;
                        }
                        list.modules[list.count-1].path[a] = line[i];
                        i++;
                    }
                    list.modules[list.count-1].path[a] = '\0';
                }
                else
                {
                    free(list.modules);
                    list.modules = NULL;

                    list.count = -1;
                    write_log("error.txt", "ERROR on parsing modules");
                    return list;
                }
            }
            else
            {
                free(list.modules);
                list.modules = NULL;

                list.count = -1;
                write_log("error.txt", "ERROR on parsing modules");
                return list;
            }
        }
        line = strtok_r(NULL, "\n", &rest);
    }

    free(originalbuffer);
    originalbuffer = NULL;

    snprintf(logger, 1024, "%i modules successfully parsed", list.count);
    write_log("log.txt", logger);

    dlerror();
    for(i=0; i<list.count; i++)
    {
        list.modules[i].handle = dlopen(list.modules[i].path, RTLD_LAZY);

        if(dlerror()!=NULL)
        {
            snprintf(logger, 1024, "failed on loading module: %s", list.modules[i].name);
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
    }


    return list;
}

int FreeModules(tobServ_modulelist list)
{
    int i;

    for(i=0; i < list.count; i++)
    {
        //dlclose(list.modules[i].handle);
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
        else if(!strcmp(command, "stats"))
            printf("tobServ %s Current Status\nThreads: %i/%i PeakThreads: %i TotalRequests: %i\n", VERSION, commandline->numthreads, commandline->maxthreads, commandline->peakthreads, commandline->numrequests);

        free(command);
    }
    return 0;
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
    char *result;
    char replacechar;

    input = stringreplace(input, "+", " ");

    length = strlen(input);
    result = malloc(length+1);

    a=0;

    // length-2 because it doesn't make sense to check for %XX at the last two characters of the string
    for(i=0;i<length;i++)
    {
         if(input[i]=='%' && i<(length-2))
         {
             stringcpy(buffer, input+i+1, 3);
             replacechar = strtol(buffer, NULL, 16);
             result[a]=replacechar;
             i+=2;
         }
         else
             result[a]=input[i];

         a++;
    }
    result[a]='\0';

    strcpy(input, result);
    free(result);

    return input;
}


