#ifndef FILECACHE_H
#define FILECACHE_H

#include <pthread.h>

#define DO_PARSE 1
#define DONT_PARSE 0
#define DO_CACHE 1
#define DONT_CACHE 0

struct _tobServ_file;

typedef struct _tobServ_CachedFile
{
    struct _tobServ_file *file;
    int lastaccess;
    char *path;

    int usecount;
    pthread_mutex_t filelock;
} tobServ_CachedFile;

typedef struct _tobServ_FileCache
{
    int maxfiles;
    int maxfilesize;
    int active; //specifies if it can be used right now
    
    int numfiles;
    tobServ_CachedFile *files;

    pthread_rwlock_t lock;

    int freeTookPlace;
    pthread_cond_t freeTookPlaceCond;
    pthread_mutex_t freeTookPlaceMutex;
} tobServ_FileCache;

//0 on success, 1 invalid maxfiles, 2 invalid maxfilesize
int InitializeFileCache(tobServ_FileCache *filecache, int maxfiles, int maxfilesize);

int FreeFileCache(tobServ_FileCache *filecache);

//0 added to cache, -1 file too large for cache, -2 all files in cache are in use cannot cache -3 unknown error(shouldn't happen)
int AddFileToCache(tobServ_FileCache* filecache, char *path, struct _tobServ_file file);

//checks if it is cached if not adds it to the cache, if file doesn't exist returns file filled with null
//parse determines if it should be parsed
struct _tobServ_file GetFileFromFileCache(tobServ_FileCache* filecache, char *path, int parse);

//marks file as not used anymore
int FreeFileFromFileCache(tobServ_FileCache* filecache, struct _tobServ_file* file);

//returns the sum of all filesizes
int GetTotalFileCacheSize(tobServ_FileCache* filecache);

struct _tobServ_file LoadFileFromDisk(char *path);

//using 0 for maxfiles or maxfilesize makes it keep the old value
int AlterAndEmptyFileCache(tobServ_FileCache* filecache, int maxfiles, int maxfilesize);

//parse determines if it should be parsed
//cache determines if it can be cached (it will only try to cache it)
struct _tobServ_file get_file(tobServ_FileCache* filecache, char *filename, int parse, int cache);


int free_file(tobServ_FileCache *cache, struct _tobServ_file*);
int get_file_type(char *type, int size, char *path);

#endif
