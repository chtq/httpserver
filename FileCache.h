#ifndef FILECACHE_H
#define FILECACHE_H

#include <pthread.h>
#include <stdint.h>

#define DO_PARSE 1
#define DONT_PARSE 0
#define DO_CACHE 1
#define DONT_CACHE 0

struct _tobServ_file;

typedef struct _tobServ_CachedFile
{
    struct _tobServ_file *file;
    uint32_t lastaccess;
    char *path;

    uint32_t usecount;
    pthread_mutex_t filelock;
} tobServ_CachedFile;

typedef struct _tobServ_FileCache
{
    uint32_t maxfiles;
    uint32_t maxfilesize;
    uint32_t active; //specifies if it can be used right now
    
    uint32_t numfiles;
    tobServ_CachedFile *files;

    pthread_rwlock_t lock;

    //used by threads waiting for a resource
    uint32_t freeTookPlace;
    pthread_cond_t freeTookPlaceCond;
    pthread_mutex_t freeTookPlaceMutex;
} tobServ_FileCache;

//fills a file with default values
//cannot fail
int32_t InitializeFile(struct _tobServ_file *file);

//0 on success, -1 invalid maxfiles, -2 invalid maxfilesize
int32_t InitializeFileCache(tobServ_FileCache *filecache, uint32_t maxfiles, uint32_t maxfilesize);

int32_t FreeFileCache(tobServ_FileCache *filecache);

//0 added to cache, -1 file too large for cache, -2 all files in cache are in use cannot cache -3 fatal error
int32_t AddFileToCache(tobServ_FileCache* filecache, char *path, struct _tobServ_file file);

//checks if it is cached if not adds it to the cache, if file doesn't exist returns file filled with null
//parse determines if it should be parsed
struct _tobServ_file GetFileFromFileCache(tobServ_FileCache* filecache, char *path, uint32_t parse);

//marks file as not used anymore
int32_t FreeFileFromFileCache(tobServ_FileCache* filecache, struct _tobServ_file* file);

//returns the sum of all filesizes
int32_t GetTotalFileCacheSize(tobServ_FileCache* filecache);

struct _tobServ_file LoadFileFromDisk(char *path);

//using 0 for maxfiles or maxfilesize makes it keep the old value
int32_t AlterAndEmptyFileCache(tobServ_FileCache* filecache, uint32_t maxfiles, uint32_t maxfilesize);

//parse determines if it should be parsed
//cache determines if it can be cached (it will only try to cache it)
struct _tobServ_file get_file(tobServ_FileCache* filecache, char *filename, uint32_t parse, uint32_t cache);


int32_t free_file(tobServ_FileCache *cache, struct _tobServ_file*);
int32_t get_file_type(char *type, uint32_t size, char *path);

#endif
