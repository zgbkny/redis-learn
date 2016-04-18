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

#include "db.h"
#include "redis.h"
#include "dict.h"

/*============================ DB saving/loading ============================ */

/* Save the DB on disk. Return REDIS_ERR on error, REDIS_OK on success */
int saveDb(char *filename) {
    dictIterator *di = NULL;
    dictEntry *de;
    uint32_t len;
    uint8_t type;
    FILE *fp;
    char tmpfile[256];
    int j;

    snprintf(tmpfile,256,"temp-%d.%ld.rdb",(int)time(NULL),(long int)random());
    fp = fopen(tmpfile,"w");
    if (!fp) {
        redisLog(REDIS_WARNING, "Failed saving the DB: %s", strerror(errno));
        return REDIS_ERR;
    }
    if (fwrite("REDIS0000",9,1,fp) == 0) goto werr;
    for (j = 0; j < server.dbnum; j++) {
        dict *dict = server.dict[j];
        if (dictGetHashTableUsed(dict) == 0) continue;
        di = dictGetIterator(dict);
        if (!di) {
            fclose(fp);
            return REDIS_ERR;
        }

        /* Write the SELECT DB opcode */
        type = REDIS_SELECTDB;
        len = htonl(j);
        if (fwrite(&type,1,1,fp) == 0) goto werr;
        if (fwrite(&len,4,1,fp) == 0) goto werr;

        /* Iterate this DB writing every entry */
        while((de = dictNext(di)) != NULL) {
            sds key = dictGetEntryKey(de);
            robj *o = dictGetEntryVal(de);

            type = o->type;
            len = htonl(sdslen(key));
            if (fwrite(&type,1,1,fp) == 0) goto werr;
            if (fwrite(&len,4,1,fp) == 0) goto werr;
            if (fwrite(key,sdslen(key),1,fp) == 0) goto werr;
            if (type == REDIS_STRING) {
                /* Save a string value */
                sds sval = o->ptr;
                len = htonl(sdslen(sval));
                if (fwrite(&len,4,1,fp) == 0) goto werr;
                if (fwrite(sval,sdslen(sval),1,fp) == 0) goto werr;
            } else if (type == REDIS_LIST) {
                /* Save a list value */
                list *list = o->ptr;
                listNode *ln = list->head;

                len = htonl(listLength(list));
                if (fwrite(&len,4,1,fp) == 0) goto werr;
                while(ln) {
                    robj *eleobj = listNodeValue(ln);
                    len = htonl(sdslen(eleobj->ptr));
                    if (fwrite(&len,4,1,fp) == 0) goto werr;
                    if (fwrite(eleobj->ptr,sdslen(eleobj->ptr),1,fp) == 0)
                        goto werr;
                    ln = ln->next;
                }
            } else {
                assert(0 != 0);
            }
        }
        dictReleaseIterator(di);
    }
    /* EOF opcode */
    type = REDIS_EOF;
    if (fwrite(&type,1,1,fp) == 0) goto werr;
    fclose(fp);
    
    /* Use RENAME to make sure the DB file is changed atomically only
     * if the generate DB file is ok. */
    if (rename(tmpfile,filename) == -1) {
        redisLog(REDIS_WARNING,"Error moving temp DB file on the final destionation: %s", strerror(errno));
        unlink(tmpfile);
        return REDIS_ERR;
    }
    redisLog(REDIS_NOTICE,"DB saved on disk");
    server.dirty = 0;
    server.lastsave = time(NULL);
    return REDIS_OK;

werr:
    fclose(fp);
    redisLog(REDIS_WARNING,"Error saving DB on disk: %s", strerror(errno));
    if (di) dictReleaseIterator(di);
    return REDIS_ERR;
}

int saveDbBackground(char *filename) {
    pid_t childpid;

    if (server.bgsaveinprogress) return REDIS_ERR;
    if ((childpid = fork()) == 0) {
        /* Child */
        close(server.fd);
        if (saveDb(filename) == REDIS_OK) {
            exit(0);
        } else {
            exit(1);
        }
    } else {
        /* Parent */
        redisLog(REDIS_NOTICE,"Background saving started by pid %d",childpid);
        server.bgsaveinprogress = 1;
        return REDIS_OK;
    }
    return REDIS_OK; /* unreached */
}

int loadDb(char *filename) {
    FILE *fp;
    char buf[REDIS_LOADBUF_LEN];    /* Try to use this buffer instead of */
    char vbuf[REDIS_LOADBUF_LEN];   /* malloc() when the element is small */
    char *key = NULL, *val = NULL;
    uint32_t klen,vlen,dbid;
    uint8_t type;
    int retval;
    dict *dict = server.dict[0];

    fp = fopen(filename,"r");
    if (!fp) return REDIS_ERR;
    if (fread(buf,9,1,fp) == 0) goto eoferr;
    if (memcmp(buf,"REDIS0000",9) != 0) {
        fclose(fp);
        redisLog(REDIS_WARNING,"Wrong signature trying to load DB from file");
        return REDIS_ERR;
    }
    while(1) {
        robj *o;

        /* Read type. */
        if (fread(&type,1,1,fp) == 0) goto eoferr;
        if (type == REDIS_EOF) break;
        /* Handle SELECT DB opcode as a special case */
        if (type == REDIS_SELECTDB) {
            if (fread(&dbid,4,1,fp) == 0) goto eoferr;
            dbid = ntohl(dbid);
            if (dbid >= (unsigned)server.dbnum) {
                redisLog(REDIS_WARNING,"FATAL: Data file was created with a Redis server compiled to handle more than %d databases. Exiting\n", server.dbnum);
                exit(1);
            }
            dict = server.dict[dbid];
            continue;
        }
        /* Read key */
        if (fread(&klen,4,1,fp) == 0) goto eoferr;
        klen = ntohl(klen);
        if (klen <= REDIS_LOADBUF_LEN) {
            key = buf;
        } else {
            key = malloc(klen);
            if (!key) oom("Loading DB from file");
        }
        if (fread(key,klen,1,fp) == 0) goto eoferr;

        if (type == REDIS_STRING) {
            /* Read string value */
            if (fread(&vlen,4,1,fp) == 0) goto eoferr;
            vlen = ntohl(vlen);
            if (vlen <= REDIS_LOADBUF_LEN) {
                val = vbuf;
            } else {
                val = malloc(vlen);
                if (!val) oom("Loading DB from file");
            }
            if (fread(val,vlen,1,fp) == 0) goto eoferr;
            o = createObject(REDIS_STRING,sdsnewlen(val,vlen));
        } else if (type == REDIS_LIST) {
            /* Read list value */
            uint32_t listlen;
            if (fread(&listlen,4,1,fp) == 0) goto eoferr;
            listlen = ntohl(listlen);
            o = createListObject();
            /* Load every single element of the list */
            while(listlen--) {
                robj *ele;

                if (fread(&vlen,4,1,fp) == 0) goto eoferr;
                vlen = ntohl(vlen);
                if (vlen <= REDIS_LOADBUF_LEN) {
                    val = vbuf;
                } else {
                    val = malloc(vlen);
                    if (!val) oom("Loading DB from file");
                }
                if (fread(val,vlen,1,fp) == 0) goto eoferr;
                ele = createObject(REDIS_STRING,sdsnewlen(val,vlen));
                if (!listAddNodeTail((list*)o->ptr,ele))
                    oom("listAddNodeTail");
                /* free the temp buffer if needed */
                if (val != vbuf) free(val);
                val = NULL;
            }
        } else {
            assert(0 != 0);
        }
        /* Add the new object in the hash table */
        retval = dictAdd(dict,sdsnewlen(key,klen),o);
        if (retval == DICT_ERR) {
            redisLog(REDIS_WARNING,"Loading DB, duplicated key found! Unrecoverable error, exiting now.");
            exit(1);
        }
        /* Iteration cleanup */
        if (key != buf) free(key);
        if (val != vbuf) free(val);
        key = val = NULL;
    }
    fclose(fp);
    return REDIS_OK;

eoferr: /* unexpected end of file is handled here with a fatal exit */
    if (key != buf) free(key);
    if (val != vbuf) free(val);
    redisLog(REDIS_WARNING,"Short read loading DB. Unrecoverable error, exiting now.");
    exit(1);
    return REDIS_ERR; /* Just to avoid warning */
}
