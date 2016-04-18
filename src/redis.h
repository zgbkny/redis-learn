#ifndef REDIS_H
#define REDIS_H

#include "ae.h"     /* Event driven programming library */
#include "sds.h"    /* Dynamic safe strings */
#include "anet.h"   /* Networking the easy way */
#include "dict.h"   /* Hash tables */
#include "adlist.h" /* Linked lists */
/* Error codes */
#define REDIS_OK                0
#define REDIS_ERR               -1

/* server configuration */
#define REDIS_SERVERPORT        6379    /* TCP port */
#define REDIS_MAXIDLETIME       (60*5)  /* default client timeout */
#define REDIS_QUERYBUF_LEN      1024
#define REDIS_LOADBUF_LEN       1024
#define REDIS_MAX_ARGS          16
#define REDIS_DEFAULT_DBNUM     16
#define REDIS_CONFIGLINE_MAX    1024

/* Hash table parameters */
#define REDIS_HT_MINFILL        10      /* Minimal hash table fill 10% */
#define REDIS_HT_MINSLOTS       16384   /* Never resize the HT under this */

/* Command types */
#define REDIS_CMD_BULK          1
#define REDIS_CMD_INLINE        0

/* Object types */
#define REDIS_STRING 0
#define REDIS_LIST 1
#define REDIS_SET 2
#define REDIS_SELECTDB 254
#define REDIS_EOF 255

/* List related stuff */
#define REDIS_HEAD 0
#define REDIS_TAIL 1

/* Log levels */
#define REDIS_DEBUG 0
#define REDIS_NOTICE 1
#define REDIS_WARNING 2

/* Anti-warning macro... */
#define REDIS_NOTUSED(V) ((void) V)

/*================================= Data types ============================== */

/* With multiplexing we need to take per-clinet state.
 * Clients are taken in a liked list. */
typedef struct redisClient {
    int fd;
    dict *dict;
    sds querybuf;
    sds argv[REDIS_MAX_ARGS];
    int argc;
    int bulklen;    /* bulk read len. -1 if not in bulk read mode */
    list *reply;
    int sentlen;
    time_t lastinteraction; /* time of the last interaction, used for timeout */
} redisClient;

/* A redis object, that is a type able to hold a string / list / set */
typedef struct redisObject {
    int type;
    void *ptr;
    int refcount;
} robj;

struct saveparam {
    time_t seconds;
    int changes;
};

/* Global server state structure */
struct redisServer {
    int port;
    int fd;
    dict **dict;
    long long dirty;            /* changes to DB from the last save */
    list *clients;
    char neterr[ANET_ERR_LEN];
    aeEventLoop *el;
    int verbosity;
    int cronloops;
    int maxidletime;
    int dbnum;
    list *objfreelist;          /* A list of freed objects to avoid malloc() */
    int bgsaveinprogress;
    time_t lastsave;
    struct saveparam *saveparams;
    int saveparamslen;
    char *logfile;
};


struct sharedObjectsStruct {
    robj *crlf, *ok, *err, *zerobulk, *nil, *zero, *one, *pong;
} shared;


/*================================ Prototypes =============================== */

void freeStringObject(robj *o);
void freeListObject(robj *o);
void freeSetObject(robj *o);
void decrRefCount(void *o);
robj *createObject(int type, void *ptr);
void freeClient(redisClient *c);
void addReply(redisClient *c, robj *obj);
void addReplySds(redisClient *c, sds s);
void incrRefCount(robj *o);
int selectDb(redisClient *c, int id);


int stringmatchlen(const char *pattern, int patternLen,
        const char *string, int stringLen, int nocase);

void redisLog(int level, const char *fmt, ...);
void oom(const char *msg);

robj *createListObject(void);


extern struct redisServer server;

#endif