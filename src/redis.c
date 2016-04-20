/* Redis - REmote DIctionary Server
 * Copyright (C) 2008 Salvatore Sanfilippo antirez at gmail dot com
 * All Rights Reserved. Under the GPL version 2. See the COPYING file. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <inttypes.h>
#include <arpa/inet.h>


#include "redis.h"
#include "command.h"
#include "db.h"

/* Global vars */
struct redisServer server; /* server global state */
struct redisCommand cmdTable[] = {
    {"get",getCommand,2,REDIS_CMD_INLINE},
    {"set",setCommand,3,REDIS_CMD_BULK},
    {"setnx",setnxCommand,3,REDIS_CMD_BULK},
    {"del",delCommand,2,REDIS_CMD_INLINE},
    {"exists",existsCommand,2,REDIS_CMD_INLINE},
    {"incr",incrCommand,2,REDIS_CMD_INLINE},
    {"decr",decrCommand,2,REDIS_CMD_INLINE},
    {"rpush",rpushCommand,3,REDIS_CMD_BULK},
    {"lpush",lpushCommand,3,REDIS_CMD_BULK},
    {"rpop",rpopCommand,2,REDIS_CMD_INLINE},
    {"lpop",lpopCommand,2,REDIS_CMD_INLINE},
    {"llen",llenCommand,2,REDIS_CMD_INLINE},
    {"lindex",lindexCommand,3,REDIS_CMD_INLINE},
    {"lrange",lrangeCommand,4,REDIS_CMD_INLINE},
    {"ltrim",ltrimCommand,4,REDIS_CMD_INLINE},
    {"randomkey",randomkeyCommand,1,REDIS_CMD_INLINE},
    {"select",selectCommand,2,REDIS_CMD_INLINE},
    {"move",moveCommand,3,REDIS_CMD_INLINE},
    {"rename",renameCommand,3,REDIS_CMD_INLINE},
    {"renamenx",renamenxCommand,3,REDIS_CMD_INLINE},
    {"keys",keysCommand,2,REDIS_CMD_INLINE},
    {"dbsize",dbsizeCommand,1,REDIS_CMD_INLINE},
    {"ping",pingCommand,1,REDIS_CMD_INLINE},
    {"echo",echoCommand,2,REDIS_CMD_BULK},
    {"save",saveCommand,1,REDIS_CMD_INLINE},
    {"bgsave",bgsaveCommand,1,REDIS_CMD_INLINE},
    {"shutdown",shutdownCommand,1,REDIS_CMD_INLINE},
    {"lastsave",lastsaveCommand,1,REDIS_CMD_INLINE},
    /* lpop, rpop, lindex, llen */
    /* dirty, lastsave, info */
    {"",NULL,0,0}
};

/*============================ Utility functions ============================ */

/* Glob-style pattern matching. */
int stringmatchlen(const char *pattern, int patternLen,
        const char *string, int stringLen, int nocase)
{
    while(patternLen) {
        switch(pattern[0]) {
        case '*':
            while (pattern[1] == '*') {
                pattern++;
                patternLen--;
            }
            if (patternLen == 1)
                return 1; /* match */
            while(stringLen) {
                if (stringmatchlen(pattern+1, patternLen-1,
                            string, stringLen, nocase))
                    return 1; /* match */
                string++;
                stringLen--;
            }
            return 0; /* no match */
            break;
        case '?':
            if (stringLen == 0)
                return 0; /* no match */
            string++;
            stringLen--;
            break;
        case '[':
        {
            int not, match;

            pattern++;
            patternLen--;
            not = pattern[0] == '^';
            if (not) {
                pattern++;
                patternLen--;
            }
            match = 0;
            while(1) {
                if (pattern[0] == '\\') {
                    pattern++;
                    patternLen--;
                    if (pattern[0] == string[0])
                        match = 1;
                } else if (pattern[0] == ']') {
                    break;
                } else if (patternLen == 0) {
                    pattern--;
                    patternLen++;
                    break;
                } else if (pattern[1] == '-' && patternLen >= 3) {
                    int start = pattern[0];
                    int end = pattern[2];
                    int c = string[0];
                    if (start > end) {
                        int t = start;
                        start = end;
                        end = t;
                    }
                    if (nocase) {
                        start = tolower(start);
                        end = tolower(end);
                        c = tolower(c);
                    }
                    pattern += 2;
                    patternLen -= 2;
                    if (c >= start && c <= end)
                        match = 1;
                } else {
                    if (!nocase) {
                        if (pattern[0] == string[0])
                            match = 1;
                    } else {
                        if (tolower((int)pattern[0]) == tolower((int)string[0]))
                            match = 1;
                    }
                }
                pattern++;
                patternLen--;
            }
            if (not)
                match = !match;
            if (!match)
                return 0; /* no match */
            string++;
            stringLen--;
            break;
        }
        case '\\':
            if (patternLen >= 2) {
                pattern++;
                patternLen--;
            }
            /* fall through */
        default:
            if (!nocase) {
                if (pattern[0] != string[0])
                    return 0; /* no match */
            } else {
                if (tolower((int)pattern[0]) != tolower((int)string[0]))
                    return 0; /* no match */
            }
            string++;
            stringLen--;
            break;
        }
        pattern++;
        patternLen--;
        if (stringLen == 0) {
            while(*pattern == '*') {
                pattern++;
                patternLen--;
            }
            break;
        }
    }
    if (patternLen == 0 && stringLen == 0)
        return 1;
    return 0;
}

void redisLog(int level, const char *fmt, ...)
{
    va_list ap;
    FILE *fp;

    fp = (server.logfile == NULL) ? stdout : fopen(server.logfile,"a");
    if (!fp) return;

    va_start(ap, fmt);
    if (level >= server.verbosity) {
        char *c = ".-*";
        fprintf(fp,"%c ",c[level]);
        vfprintf(fp, fmt, ap);
        fprintf(fp,"\n");
        fflush(fp);
    }
    va_end(ap);

    if (server.logfile) fclose(fp);
}

/*====================== Hash table type implementation  ==================== */

/* This is an hash table type that uses the SDS dynamic strings libary as
 * keys and radis objects as values (objects can hold SDS strings,
 * lists, sets). */

unsigned int sdsDictHashFunction(const void *key) {
    return dictGenHashFunction(key, sdslen((sds)key));
}

int sdsDictKeyCompare(void *privdata, const void *key1,
        const void *key2)
{
    int l1,l2;
    DICT_NOTUSED(privdata);

    l1 = sdslen((sds)key1);
    l2 = sdslen((sds)key2);
    if (l1 != l2) return 0;
    return memcmp(key1, key2, l1) == 0;
}

void sdsDictKeyDestructor(void *privdata, void *val)
{
    DICT_NOTUSED(privdata);

    sdsfree(val);
}

void sdsDictValDestructor(void *privdata, void *val)
{
    DICT_NOTUSED(privdata);

    decrRefCount(val);
}

dictType sdsDictType = {
    sdsDictHashFunction,       /* hash function */
    NULL,                      /* key dup */
    NULL,                      /* val dup */
    sdsDictKeyCompare,         /* key compare */
    sdsDictKeyDestructor,      /* key destructor */
    sdsDictValDestructor,      /* val destructor */
};

/* ========================= Random utility functions ======================= */

/* Redis generally does not try to recover from out of memory conditions
 * when allocating objects or strings, it is not clear if it will be possible
 * to report this condition to the client since the networking layer itself
 * is based on heap allocation for send buffers, so we simply abort.
 * At least the code will be simpler to read... */
void oom(const char *msg) {
    fprintf(stderr, "%s: Out of memory\n",msg);
    fflush(stderr);
    sleep(1);
    abort();
}

/* ====================== Redis server networking stuff ===================== */
void closeTimedoutClients(void) {
    redisClient *c;
    listIter *li;
    listNode *ln;
    time_t now = time(NULL);

    li = listGetIterator(server.clients,AL_START_HEAD);
    if (!li) return;
    while ((ln = listNextElement(li)) != NULL) {
        c = listNodeValue(ln);
        if (now - c->lastinteraction > server.maxidletime) {
            redisLog(REDIS_DEBUG,"Closing idle client");
            freeClient(c);
        }
    }
    listReleaseIterator(li);
}

int serverCron(struct aeEventLoop *eventLoop, long long id, void *clientData) {
    int j, size, used, loops = server.cronloops++;
    REDIS_NOTUSED(eventLoop);
    REDIS_NOTUSED(id);
    REDIS_NOTUSED(clientData);

    /* If the percentage of used slots in the HT reaches REDIS_HT_MINFILL
     * we resize the hash table to save memory */
    for (j = 0; j < server.dbnum; j++) {
        size = dictGetHashTableSize(server.dict[j]);
        used = dictGetHashTableUsed(server.dict[j]);
        if (!(loops % 5) && used > 0) {
            redisLog(REDIS_DEBUG,"DB %d: %d keys in %d slots HT.",j,used,size);
            // dictPrintStats(server.dict);
        }
        if (size && used && size > REDIS_HT_MINSLOTS &&
            (used*100/size < REDIS_HT_MINFILL)) {
            redisLog(REDIS_NOTICE,"The hash table %d is too sparse, resize it...",j);
            dictResize(server.dict[j]);
            redisLog(REDIS_NOTICE,"Hash table %d resized.",j);
        }
    }

    /* Show information about connected clients */
    if (!(loops % 5)) redisLog(REDIS_DEBUG,"%d clients connected",listLength(server.clients));

    /* Close connections of timedout clients */
    if (!(loops % 10))
        closeTimedoutClients();

    /* Check if a background saving in progress terminated */
    if (server.bgsaveinprogress) {
        int statloc;
        if (wait4(-1,&statloc,WNOHANG,NULL)) {
            int exitcode = WEXITSTATUS(statloc);
            if (exitcode == 0) {
                redisLog(REDIS_NOTICE,
                    "Background saving terminated with success");
                server.dirty = 0;
                server.lastsave = time(NULL);
            } else {
                redisLog(REDIS_WARNING,
                    "Background saving error");
            }
            server.bgsaveinprogress = 0;
        }
    } else {
        /* If there is not a background saving in progress check if
         * we have to save now */
         time_t now = time(NULL);
         for (j = 0; j < server.saveparamslen; j++) {
            struct saveparam *sp = server.saveparams+j;

            if (server.dirty >= sp->changes &&
                now-server.lastsave > sp->seconds) {
                redisLog(REDIS_NOTICE,"%d changes in %d seconds. Saving...",
                    sp->changes, sp->seconds);
                saveDbBackground("dump.rdb");
                break;
            }
         }
    }
    return 1000;
}

void createSharedObjects(void) {
    shared.crlf = createObject(REDIS_STRING,sdsnew("\r\n"));
    shared.ok = createObject(REDIS_STRING,sdsnew("+OK\r\n"));
    shared.err = createObject(REDIS_STRING,sdsnew("-ERR\r\n"));
    shared.zerobulk = createObject(REDIS_STRING,sdsnew("0\r\n\r\n"));
    shared.nil = createObject(REDIS_STRING,sdsnew("nil\r\n"));
    shared.zero = createObject(REDIS_STRING,sdsnew("0\r\n"));
    shared.one = createObject(REDIS_STRING,sdsnew("1\r\n"));
    shared.pong = createObject(REDIS_STRING,sdsnew("+PONG\r\n"));
}

void appendServerSaveParams(time_t seconds, int changes) {
    server.saveparams = realloc(server.saveparams,sizeof(struct saveparam)*(server.saveparamslen+1));
    if (server.saveparams == NULL) oom("appendServerSaveParams");
    server.saveparams[server.saveparamslen].seconds = seconds;
    server.saveparams[server.saveparamslen].changes = changes;
    server.saveparamslen++;
}

void ResetServerSaveParams() {
    free(server.saveparams);
    server.saveparams = NULL;
    server.saveparamslen = 0;
}

void initServerConfig() {
    server.dbnum = REDIS_DEFAULT_DBNUM;
    server.port = REDIS_SERVERPORT;
    server.verbosity = REDIS_DEBUG;
    server.maxidletime = REDIS_MAXIDLETIME;
    server.saveparams = NULL;
    server.logfile = NULL; /* NULL = log on standard output */
    ResetServerSaveParams();

    appendServerSaveParams(60*60,1);  /* save after 1 hour and 1 change */
    appendServerSaveParams(300,100);  /* save after 5 minutes and 100 changes */
    appendServerSaveParams(60,10000); /* save after 1 minute and 10000 changes */
}

void initServer() {
    int j;

    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    server.clients = listCreate();
    server.objfreelist = listCreate();
    createSharedObjects();
    server.el = aeCreateEventLoop();
    server.dict = malloc(sizeof(dict*)*server.dbnum);
    if (!server.dict || !server.clients || !server.el || !server.objfreelist)
        oom("server initialization"); /* Fatal OOM */
    server.fd = anetTcpServer(server.neterr, server.port, NULL);
    if (server.fd == -1) {
        redisLog(REDIS_WARNING, "Opening TCP port: %s", server.neterr);
        exit(1);
    }
    for (j = 0; j < server.dbnum; j++) {
        server.dict[j] = dictCreate(&sdsDictType,NULL);
        if (!server.dict[j])
            oom("server initialization"); /* Fatal OOM */
    }
    server.cronloops = 0;
    server.bgsaveinprogress = 0;
    server.lastsave = time(NULL);
    server.dirty = 0;
    aeCreateTimeEvent(server.el, 1000, serverCron, NULL, NULL);
}

/* I agree, this is a very rudimental way to load a configuration...
   will improve later if the config gets more complex */
void loadServerConfig(char *filename) {
    FILE *fp = fopen(filename,"r");
    char buf[REDIS_CONFIGLINE_MAX+1], *err = NULL;
    int linenum = 0;
    sds line = NULL;
    
    if (!fp) {
        redisLog(REDIS_WARNING,"Fatal error, can't open config file");
        exit(1);
    }
    while(fgets(buf,REDIS_CONFIGLINE_MAX+1,fp) != NULL) {
        sds *argv;
        int argc;

        linenum++;
        line = sdsnew(buf);
        line = sdstrim(line," \t\r\n");

        /* Skip comments and blank lines*/
        if (line[0] == '#' || line[0] == '\0') {
            sdsfree(line);
            continue;
        }

        /* Split into arguments */
        argv = sdssplitlen(line,sdslen(line)," ",1,&argc);

        /* Execute config directives */
        if (!strcmp(argv[0],"timeout") && argc == 2) {
            server.maxidletime = atoi(argv[1]);
            if (server.maxidletime < 1) {
                err = "Invalid timeout value"; goto loaderr;
            }
        } else if (!strcmp(argv[0],"save") && argc == 3) {
            int seconds = atoi(argv[1]);
            int changes = atoi(argv[2]);
            if (seconds < 1 || changes < 0) {
                err = "Invalid save parameters"; goto loaderr;
            }
            appendServerSaveParams(seconds,changes);
        } else if (!strcmp(argv[0],"dir") && argc == 2) {
            if (chdir(argv[1]) == -1) {
                redisLog(REDIS_WARNING,"Can't chdir to '%s': %s",
                    argv[1], strerror(errno));
                exit(1);
            }
        } else if (!strcmp(argv[0],"loglevel") && argc == 2) {
            if (!strcmp(argv[1],"debug")) server.verbosity = REDIS_DEBUG;
            else if (!strcmp(argv[1],"notice")) server.verbosity = REDIS_NOTICE;
            else if (!strcmp(argv[1],"warning")) server.verbosity = REDIS_WARNING;
            else {
                err = "Invalid log level. Must be one of debug, notice, warning";
                goto loaderr;
            }
        } else if (!strcmp(argv[0],"logfile") && argc == 2) {
            FILE *fp;

            server.logfile = strdup(argv[1]);
            if (!strcmp(server.logfile,"stdout")) server.logfile = NULL;
            if (server.logfile) {
                /* Test if we are able to open the file. The server will not
                 * be able to abort just for this problem later... */
                fp = fopen(server.logfile,"a");
                if (fp == NULL) {
                    err = sdscatprintf(sdsempty(),
                        "Can't open the log file: %s", strerror(errno));
                    goto loaderr;
                }
                fclose(fp);
            }
        } else if (!strcmp(argv[0],"databases") && argc == 2) {
            server.dbnum = atoi(argv[1]);
            if (server.dbnum < 1) {
                err = "Invalid number of databases"; goto loaderr;
            }
        } else {
            err = "Bad directive or wrong number of arguments"; goto loaderr;
        }
        sdsfree(line);
    }
    fclose(fp);
    return;

loaderr:
    fprintf(stderr, "\n*** FATAL CONFIG FILE ERROR ***\n");
    fprintf(stderr, "Reading the configuration file, at line %d\n", linenum);
    fprintf(stderr, ">>> '%s'\n", line);
    fprintf(stderr, "%s\n", err);
    exit(1);
}

void freeClientArgv(redisClient *c) {
    int j;

    for (j = 0; j < c->argc; j++)
        sdsfree(c->argv[j]);
    c->argc = 0;
}

void freeClient(redisClient *c) {
    listNode *ln;

    aeDeleteFileEvent(server.el,c->fd,AE_READABLE);
    aeDeleteFileEvent(server.el,c->fd,AE_WRITABLE);
    sdsfree(c->querybuf);
    listRelease(c->reply);
    freeClientArgv(c);
    close(c->fd);
    ln = listSearchKey(server.clients,c);
    assert(ln != NULL);
    listDelNode(server.clients,ln);
    free(c);
}

void sendReplyToClient(aeEventLoop *el, int fd, void *privdata, int mask) {
    redisClient *c = privdata;
    int nwritten = 0, totwritten = 0, objlen;
    robj *o;
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);

    while(listLength(c->reply)) {
        o = listNodeValue(listFirst(c->reply));
        objlen = sdslen(o->ptr);

        if (objlen == 0) {
            listDelNode(c->reply,listFirst(c->reply));
            continue;
        }

        nwritten = write(fd, o->ptr+c->sentlen, objlen - c->sentlen);
        if (nwritten <= 0) break;
        c->sentlen += nwritten;
        totwritten += nwritten;
        /* If we fully sent the object on head go to the next one */
        if (c->sentlen == objlen) {
            listDelNode(c->reply,listFirst(c->reply));
            c->sentlen = 0;
        }
    }
    if (nwritten == -1) {
        if (errno == EAGAIN) {
            nwritten = 0;
        } else {
            redisLog(REDIS_DEBUG,
                "Error writing to client: %s", strerror(errno));
            freeClient(c);
            return;
        }
    }
    if (totwritten > 0) c->lastinteraction = time(NULL);
    if (listLength(c->reply) == 0) {
        c->sentlen = 0;
        aeDeleteFileEvent(server.el,c->fd,AE_WRITABLE);
    }
}

struct redisCommand *lookupCommand(char *name) {
    int j = 0;
    while(cmdTable[j].name != NULL) {
        if (!strcmp(name,cmdTable[j].name)) return &cmdTable[j];
        j++;
    }
    return NULL;
}

/* resetClient prepare the client to process the next command */
void resetClient(redisClient *c) {
    freeClientArgv(c);
    c->bulklen = -1;
}

/* If this function gets called we already read a whole
 * command, argments are in the client argv/argc fields.
 * processCommand() execute the command or prepare the
 * server for a bulk read from the client.
 *
 * If 1 is returned the client is still alive and valid and
 * and other operations can be performed by the caller. Otherwise
 * if 0 is returned the client was destroied (i.e. after QUIT). */
int processCommand(redisClient *c) {
    redisLog(REDIS_DEBUG, "processCommand");
    struct redisCommand *cmd;

    sdstolower(c->argv[0]);
    /* The QUIT command is handled as a special case. Normal command
     * procs are unable to close the client connection safely */
    if (!strcmp(c->argv[0],"quit")) {
        freeClient(c);
        return 0;
    }
    cmd = lookupCommand(c->argv[0]);
    
    if (!cmd) {
        addReplySds(c,sdsnew("-ERR unknown command\r\n"));
        resetClient(c);
        return 1;
    } 
    redisLog(REDIS_DEBUG, "processCommand cmd name:%s, argv len:%d", cmd->name, c->argc);
    if (cmd->arity != c->argc) {
        addReplySds(c,sdsnew("-ERR wrong number of arguments\r\n"));
        resetClient(c);
        return 1;
    } 
    /* Exec the command */
    cmd->proc(c);
    resetClient(c);
    return 1;
}

void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, int mask) {
    
    redisLog(REDIS_DEBUG, "readQueryFromClient");
    
    redisClient *c = (redisClient*) privdata;
    char buf[REDIS_QUERYBUF_LEN];
    int nread;
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);

    nread = read(fd, buf, REDIS_QUERYBUF_LEN);
    if (nread == -1) {
        if (errno == EAGAIN) {
            nread = 0;
        } else {
            redisLog(REDIS_DEBUG, "Reading from client: %s",strerror(errno));
            freeClient(c);
            return;
        }
    } else if (nread == 0) {
        redisLog(REDIS_DEBUG, "Client closed connection");
        freeClient(c);
        return;
    }
    if (nread) {
        c->querybuf = sdscatlen(c->querybuf, buf, nread);
        c->lastinteraction = time(NULL);
    } else {
        return;
    }

again:
     
    if (c->bulklen == -1) {
        /* Read the first line of the query */
        char *p = strchr(c->querybuf,'\n');
        size_t querylen;
        if (p) {
            sds query, *argv;
            int argc, j;
            
            query = c->querybuf;
            c->querybuf = sdsempty();
            querylen = 1+(p-(query));
            if (sdslen(query) > querylen) {
                /* leave data after the first line of the query in the buffer */
                c->querybuf = sdscatlen(c->querybuf,query+querylen,sdslen(query)-querylen);
            }
            *p = '\0'; /* remove "\n" */
            if (*(p-1) == '\r') *(p-1) = '\0'; /* and "\r" if any */
            sdsupdatelen(query);

            /* Now we can split the query in arguments */
            if (sdslen(query) == 0) {
                /* Ignore empty query */
                sdsfree(query);
                return;
            }
            argv = sdssplitlen(query, sdslen(query), " ", 1, &argc);
            sdsfree(query);
            if (argv == NULL) oom("Splitting query in token");
            
            /**
             * 假如命令是：  echo hello
             * 那么处理的结果就是：c->argv[0] = "echo", c->argv[1] = "hello"
             **/
            for (j = 0; j < argc && j < REDIS_MAX_ARGS; j++) {
                if (sdslen(argv[j])) {
                    c->argv[c->argc] = argv[j];
                    c->argc++;
                } else {
                    sdsfree(argv[j]);
                }
            }
            free(argv); // 删除"\0"防止内存泄漏
            /* Execute the command. If the client is still valid
             * after processCommand() return and there is something
             * on the query buffer try to process the next command. */
            if (processCommand(c) && sdslen(c->querybuf)) goto again;
            return;
        } else if (sdslen(c->querybuf) >= 1024) {
            redisLog(REDIS_DEBUG, "Client protocol error");
            freeClient(c); 
            return;
        }
    } else {
        /* Bulk read handling. Note that if we are at this point
           the client already sent a command terminated with a newline,
           we are reading the bulk data that is actually the last
           argument of the command. */
        int qbl = sdslen(c->querybuf);

        if (c->bulklen <= qbl) {
            /* Copy everything but the final CRLF as final argument */
            c->argv[c->argc] = sdsnewlen(c->querybuf,c->bulklen-2);
            c->argc++;
            c->querybuf = sdsrange(c->querybuf,c->bulklen,-1);
            processCommand(c);
            return;
        }
    }
}

int selectDb(redisClient *c, int id) {
    if (id < 0 || id >= server.dbnum)
        return REDIS_ERR;
    c->dict = server.dict[id];
    return REDIS_OK;
}

int createClient(int fd) {
    redisClient *c = malloc(sizeof(*c));

    anetNonBlock(NULL,fd);
    anetTcpNoDelay(NULL,fd);
    if (!c) return REDIS_ERR;
    selectDb(c,0);
    c->fd = fd;
    c->querybuf = sdsempty();
    c->argc = 0;
    c->bulklen = -1;
    c->sentlen = 0;
    c->lastinteraction = time(NULL);
    if ((c->reply = listCreate()) == NULL) oom("listCreate");
    listSetFreeMethod(c->reply,decrRefCount);
    if (aeCreateFileEvent(server.el, c->fd, AE_READABLE,
        readQueryFromClient, c) == AE_ERR) {
        freeClient(c);
        return REDIS_ERR;
    }
    if (!listAddNodeTail(server.clients,c)) oom("listAddNodeTail");
    return REDIS_OK;
}

void addReply(redisClient *c, robj *obj) {
    if (listLength(c->reply) == 0 &&
        aeCreateFileEvent(server.el, c->fd, AE_WRITABLE,
        sendReplyToClient, c) == AE_ERR) return;
    if (!listAddNodeTail(c->reply,obj)) oom("listAddNodeTail");
    incrRefCount(obj);
}

void addReplySds(redisClient *c, sds s) {
    robj *o = createObject(REDIS_STRING,s);
    addReply(c,o);
    decrRefCount(o);
}

void acceptHandler(aeEventLoop *el, int fd, void *privdata, int mask) {
    int cport, cfd;
    char cip[128];
    REDIS_NOTUSED(el);
    REDIS_NOTUSED(mask);
    REDIS_NOTUSED(privdata);

    cfd = anetAccept(server.neterr, fd, cip, &cport);
    if (cfd == AE_ERR) {
        redisLog(REDIS_DEBUG,"Accepting client connection: %s", server.neterr);
        return;
    }
    redisLog(REDIS_DEBUG,"Accepted %s:%d", cip, cport);
    if (createClient(cfd) == REDIS_ERR) {
        redisLog(REDIS_WARNING,"Error allocating resoures for the client");
        close(cfd); /* May be already closed, just ingore errors */
        return;
    }
}

/* ======================= Redis objects implementation ===================== */
robj *createObject(int type, void *ptr) {
    robj *o;

    if (listLength(server.objfreelist)) {
        listNode *head = listFirst(server.objfreelist);
        o = listNodeValue(head);
        listDelNode(server.objfreelist,head);
    } else {
        o = malloc(sizeof(*o));
    }
    if (!o) oom("createObject");
    o->type = type;
    o->ptr = ptr;
    o->refcount = 1;
    return o;
}

robj *createListObject(void) {
    list *l = listCreate();

    if (!l) oom("createListObject");
    listSetFreeMethod(l,decrRefCount);
    return createObject(REDIS_LIST,l);
}

void freeStringObject(robj *o) {
    sdsfree(o->ptr);
}

void freeListObject(robj *o) {
    listRelease((list*) o->ptr);
}

void freeSetObject(robj *o) {
    /* TODO */
    o = o;
}

void incrRefCount(robj *o) {
    o->refcount++;
}

void decrRefCount(void *obj) {
    robj *o = obj;
    if (--(o->refcount) == 0) {
        switch(o->type) {
        case REDIS_STRING: freeStringObject(o); break;
        case REDIS_LIST: freeListObject(o); break;
        case REDIS_SET: freeSetObject(o); break;
        default: assert(0 != 0); break;
        }
        if (!listAddNodeHead(server.objfreelist,o))
            free(o);
    }
}


/* =================================== Main! ================================ */
int main(int argc, char **argv) {
    initServerConfig();
    initServer();
    if (argc == 2) {
        ResetServerSaveParams();
        loadServerConfig(argv[1]);
        redisLog(REDIS_NOTICE,"Configuration loaded");
    } else if (argc > 2) {
        fprintf(stderr,"Usage: ./redis-server [/path/to/redis.conf]\n");
        exit(1);
    }
    redisLog(REDIS_NOTICE,"Server started");
    if (loadDb("dump.rdb") == REDIS_OK)
        redisLog(REDIS_NOTICE,"DB loaded from disk");
    if (aeCreateFileEvent(server.el, server.fd, AE_READABLE,
        acceptHandler, NULL) == AE_ERR) oom("creating file event");
    redisLog(REDIS_NOTICE,"The server is now ready to accept connections");
    aeMain(server.el);
    aeDeleteEventLoop(server.el);
    return 0;
}
