#include "ae.h"     /* Event driven programming library */
#include "sds.h"    /* Dynamic safe strings */
#include "anet.h"   /* Networking the easy way */
#include "dict.h"   /* Hash tables */
#include "adlist.h" /* Linked lists */
#include "command.h"
#include "db.h"

/*================================== Commands =============================== */

void pingCommand(redisClient *c) {
    addReply(c,shared.pong);
}

void echoCommand(redisClient *c) {
    redisLog(REDIS_DEBUG, "echoCommand");
    addReplySds(c,sdscatprintf(sdsempty(),"%d\r\n",(int)sdslen(c->argv[1])));
    addReplySds(c,c->argv[1]);
    addReply(c,shared.crlf);
    c->argv[1] = NULL;
}

void setGenericCommand(redisClient *c, int nx) {
    int retval;
    robj *o;

    o = createObject(REDIS_STRING,c->argv[2]);
    c->argv[2] = NULL;
    retval = dictAdd(c->dict,c->argv[1],o);
    if (retval == DICT_ERR) {
        if (!nx)
            dictReplace(c->dict,c->argv[1],o);
        else
            decrRefCount(o);
    } else {
        /* Now the key is in the hash entry, don't free it */
        c->argv[1] = NULL;
    }
    server.dirty++;
    addReply(c,shared.ok);
}

void setCommand(redisClient *c) {
    return setGenericCommand(c,0);
}

void setnxCommand(redisClient *c) {
    return setGenericCommand(c,1);
}

void getCommand(redisClient *c) {
    dictEntry *de;
    
    de = dictFind(c->dict,c->argv[1]);
    if (de == NULL) {
        addReply(c,shared.nil);
    } else {
        robj *o = dictGetEntryVal(de);
        
        if (o->type != REDIS_STRING) {
            char *err = "GET against key not holding a string value";
            addReplySds(c,
                sdscatprintf(sdsempty(),"%d\r\n%s\r\n",-((int)strlen(err)),err));
        } else {
            addReplySds(c,sdscatprintf(sdsempty(),"%d\r\n",(int)sdslen(o->ptr)));
            addReply(c,o);
            addReply(c,shared.crlf);
        }
    }
}

void delCommand(redisClient *c) {
    if (dictDelete(c->dict,c->argv[1]) == DICT_OK)
        server.dirty++;
    addReply(c,shared.ok);
}

void existsCommand(redisClient *c) {
    dictEntry *de;
    
    de = dictFind(c->dict,c->argv[1]);
    if (de == NULL)
        addReply(c,shared.zero);
    else
        addReply(c,shared.one);
}

void incrDecrCommand(redisClient *c, int incr) {
    dictEntry *de;
    sds newval;
    long long value;
    int retval;
    robj *o;
    
    de = dictFind(c->dict,c->argv[1]);
    if (de == NULL) {
        value = 0;
    } else {
        robj *o = dictGetEntryVal(de);
        
        if (o->type != REDIS_STRING) {
            value = 0;
        } else {
            char *eptr;

            value = strtoll(o->ptr, &eptr, 10);
        }
    }

    value += incr;
    newval = sdscatprintf(sdsempty(),"%lld",value);
    o = createObject(REDIS_STRING,newval);
    retval = dictAdd(c->dict,c->argv[1],o);
    if (retval == DICT_ERR) {
        dictReplace(c->dict,c->argv[1],o);
    } else {
        /* Now the key is in the hash entry, don't free it */
        c->argv[1] = NULL;
    }
    server.dirty++;
    addReply(c,o);
    addReply(c,shared.crlf);
}

void incrCommand(redisClient *c) {
    return incrDecrCommand(c,1);
}

void decrCommand(redisClient *c) {
    return incrDecrCommand(c,-1);
}

void selectCommand(redisClient *c) {
    int id = atoi(c->argv[1]);
    
    if (selectDb(c,id) == REDIS_ERR) {
        addReplySds(c,"-ERR invalid DB index\r\n");
    } else {
        addReply(c,shared.ok);
    }
}

void randomkeyCommand(redisClient *c) {
    dictEntry *de;
    
    de = dictGetRandomKey(c->dict);
    if (de == NULL) {
        addReply(c,shared.crlf);
    } else {
        addReply(c,dictGetEntryVal(de));
        addReply(c,shared.crlf);
    }
}

void keysCommand(redisClient *c) {
    dictIterator *di;
    dictEntry *de;
    sds keys, reply;
    sds pattern = c->argv[1];
    int plen = sdslen(pattern);

    di = dictGetIterator(c->dict);
    keys = sdsempty();
    while((de = dictNext(di)) != NULL) {
        sds key = dictGetEntryKey(de);
        if ((pattern[0] == '*' && pattern[1] == '\0') ||
            stringmatchlen(pattern,plen,key,sdslen(key),0)) {
            keys = sdscatlen(keys,key,sdslen(key));
            keys = sdscatlen(keys," ",1);
        }
    }
    dictReleaseIterator(di);
    keys = sdstrim(keys," ");
    reply = sdscatprintf(sdsempty(),"%lu\r\n",sdslen(keys));
    reply = sdscatlen(reply,keys,sdslen(keys));
    reply = sdscatlen(reply,"\r\n",2);
    sdsfree(keys);
    addReplySds(c,reply);
}

void dbsizeCommand(redisClient *c) {
    addReplySds(c,
        sdscatprintf(sdsempty(),"%lu\r\n",dictGetHashTableUsed(c->dict)));
}

void lastsaveCommand(redisClient *c) {
    addReplySds(c,
        sdscatprintf(sdsempty(),"%lu\r\n",server.lastsave));
}

void saveCommand(redisClient *c) {
    if (saveDb("dump.rdb") == REDIS_OK) {
        addReply(c,shared.ok);
    } else {
        addReply(c,shared.err);
    }
}

void bgsaveCommand(redisClient *c) {
    if (server.bgsaveinprogress) {
        addReplySds(c,sdsnew("-ERR background save already in progress\r\n"));
        return;
    }
    if (saveDbBackground("dump.rdb") == REDIS_OK) {
        addReply(c,shared.ok);
    } else {
        addReply(c,shared.err);
    }
}

void shutdownCommand(redisClient *c) {
    redisLog(REDIS_WARNING,"User requested shutdown, saving DB...");
    if (saveDb("dump.rdb") == REDIS_OK) {
        redisLog(REDIS_WARNING,"Server exit now, bye bye...");
        exit(1);
    } else {
        redisLog(REDIS_WARNING,"Error trying to save the DB, can't exit"); 
        addReplySds(c,sdsnew("-ERR can't quit, problems saving the DB\r\n"));
    }
}

void renameGenericCommand(redisClient *c, int nx) {
    dictEntry *de;
    robj *o;

    /* To use the same key as src and dst is probably an error */
    if (sdscmp(c->argv[1],c->argv[2]) == 0) {
        addReplySds(c,sdsnew("-ERR src and dest key are the same\r\n"));
        return;
    }

    de = dictFind(c->dict,c->argv[1]);
    if (de == NULL) {
        addReplySds(c,sdsnew("-ERR no such key\r\n"));
        return;
    }
    o = dictGetEntryVal(de);
    incrRefCount(o);
    if (dictAdd(c->dict,c->argv[2],o) == DICT_ERR) {
        if (nx) {
            decrRefCount(o);
            addReplySds(c,sdsnew("-ERR destination key exists\r\n"));
            return;
        }
        dictReplace(c->dict,c->argv[2],o);
    } else {
        c->argv[2] = NULL;
    }
    dictDelete(c->dict,c->argv[1]);
    server.dirty++;
    addReply(c,shared.ok);
}

void renameCommand(redisClient *c) {
    renameGenericCommand(c,0);
}

void renamenxCommand(redisClient *c) {
    renameGenericCommand(c,1);
}

void moveCommand(redisClient *c) {
    dictEntry *de;
    sds *key;
    robj *o;
    dict *src, *dst;

    /* Obtain source and target DB pointers */
    src = c->dict;
    if (selectDb(c,atoi(c->argv[2])) == REDIS_ERR) {
        addReplySds(c,sdsnew("-ERR target DB out of range\r\n"));
        return;
    }
    dst = c->dict;
    c->dict = src;

    /* If the user is moving using as target the same
     * DB as the source DB it is probably an error. */
    if (src == dst) {
        addReplySds(c,sdsnew("-ERR source DB is the same as target DB\r\n"));
        return;
    }

    /* Check if the element exists and get a reference */
    de = dictFind(c->dict,c->argv[1]);
    if (!de) {
        addReplySds(c,sdsnew("-ERR no such key\r\n"));
        return;
    }

    /* Try to add the element to the target DB */
    key = dictGetEntryKey(de);
    o = dictGetEntryVal(de);
    if (dictAdd(dst,key,o) == DICT_ERR) {
        addReplySds(c,sdsnew("-ERR target DB already contains the moved key\r\n"));
        return;
    }

    /* OK! key moved, free the entry in the source DB */
    dictDeleteNoFree(src,c->argv[1]);
    server.dirty++;
    addReply(c,shared.ok);
}

void pushGenericCommand(redisClient *c, int where) {
    robj *ele, *lobj;
    dictEntry *de;
    list *list;
    
    ele = createObject(REDIS_STRING,c->argv[2]);
    c->argv[2] = NULL;

    de = dictFind(c->dict,c->argv[1]);
    if (de == NULL) {
        lobj = createListObject();
        list = lobj->ptr;
        if (where == REDIS_HEAD) {
            if (!listAddNodeHead(list,ele)) oom("listAddNodeHead");
        } else {
            if (!listAddNodeTail(list,ele)) oom("listAddNodeTail");
        }
        dictAdd(c->dict,c->argv[1],lobj);

        /* Now the key is in the hash entry, don't free it */
        c->argv[1] = NULL;
    } else {
        lobj = dictGetEntryVal(de);
        if (lobj->type != REDIS_LIST) {
            decrRefCount(ele);
            addReplySds(c,sdsnew("-ERR push against existing key not holding a list\r\n"));
            return;
        }
        list = lobj->ptr;
        if (where == REDIS_HEAD) {
            if (!listAddNodeHead(list,ele)) oom("listAddNodeHead");
        } else {
            if (!listAddNodeTail(list,ele)) oom("listAddNodeTail");
        }
    }
    server.dirty++;
    addReply(c,shared.ok);
}

void lpushCommand(redisClient *c) {
    pushGenericCommand(c,REDIS_HEAD);
}

void rpushCommand(redisClient *c) {
    pushGenericCommand(c,REDIS_TAIL);
}

void llenCommand(redisClient *c) {
    dictEntry *de;
    list *l;
    
    de = dictFind(c->dict,c->argv[1]);
    if (de == NULL) {
        addReply(c,shared.zero);
        return;
    } else {
        robj *o = dictGetEntryVal(de);
        if (o->type != REDIS_LIST) {
            addReplySds(c,sdsnew("-1\r\n"));
        } else {
            l = o->ptr;
            addReplySds(c,sdscatprintf(sdsempty(),"%d\r\n",listLength(l)));
        }
    }
}

void lindexCommand(redisClient *c) {
    dictEntry *de;
    int index = atoi(c->argv[2]);
    
    de = dictFind(c->dict,c->argv[1]);
    if (de == NULL) {
        addReply(c,shared.nil);
    } else {
        robj *o = dictGetEntryVal(de);
        
        if (o->type != REDIS_LIST) {
            char *err = "LINDEX against key not holding a list value";
            addReplySds(c,
                sdscatprintf(sdsempty(),"%d\r\n%s\r\n",-((int)strlen(err)),err));
        } else {
            list *list = o->ptr;
            listNode *ln;
            
            ln = listIndex(list, index);
            if (ln == NULL) {
                addReply(c,shared.nil);
            } else {
                robj *ele = listNodeValue(ln);
                addReplySds(c,sdscatprintf(sdsempty(),"%d\r\n",(int)sdslen(ele->ptr)));
                addReply(c,ele);
                addReply(c,shared.crlf);
            }
        }
    }
}

void popGenericCommand(redisClient *c, int where) {
    dictEntry *de;
    
    de = dictFind(c->dict,c->argv[1]);
    if (de == NULL) {
        addReply(c,shared.nil);
    } else {
        robj *o = dictGetEntryVal(de);
        
        if (o->type != REDIS_LIST) {
            char *err = "POP against key not holding a list value";
            addReplySds(c,
                sdscatprintf(sdsempty(),"%d\r\n%s\r\n",-((int)strlen(err)),err));
        } else {
            list *list = o->ptr;
            listNode *ln;

            if (where == REDIS_HEAD)
                ln = listFirst(list);
            else
                ln = listLast(list);

            if (ln == NULL) {
                addReply(c,shared.nil);
            } else {
                robj *ele = listNodeValue(ln);
                addReplySds(c,sdscatprintf(sdsempty(),"%d\r\n",(int)sdslen(ele->ptr)));
                addReply(c,ele);
                addReply(c,shared.crlf);
                listDelNode(list,ln);
                server.dirty++;
            }
        }
    }
}

void lpopCommand(redisClient *c) {
    popGenericCommand(c,REDIS_HEAD);
}

void rpopCommand(redisClient *c) {
    popGenericCommand(c,REDIS_TAIL);
}

void lrangeCommand(redisClient *c) {
    dictEntry *de;
    int start = atoi(c->argv[2]);
    int end = atoi(c->argv[3]);
    
    de = dictFind(c->dict,c->argv[1]);
    if (de == NULL) {
        addReply(c,shared.nil);
    } else {
        robj *o = dictGetEntryVal(de);
        
        if (o->type != REDIS_LIST) {
            char *err = "LRANGE against key not holding a list value";
            addReplySds(c,
                sdscatprintf(sdsempty(),"%d\r\n%s\r\n",-((int)strlen(err)),err));
        } else {
            list *list = o->ptr;
            listNode *ln;
            int llen = listLength(list);
            int rangelen, j;
            robj *ele;

            /* convert negative indexes */
            if (start < 0) start = llen+start;
            if (end < 0) end = llen+end;
            if (start < 0) start = 0;
            if (end < 0) end = 0;

            /* indexes sanity checks */
            if (start > end || start >= llen) {
                /* Out of range start or start > end result in empty list */
                addReply(c,shared.zero);
                return;
            }
            if (end >= llen) end = llen-1;
            rangelen = (end-start)+1;

            /* Return the result in form of a multi-bulk reply */
            ln = listIndex(list, start);
            addReplySds(c,sdscatprintf(sdsempty(),"%d\r\n",rangelen));
            for (j = 0; j < rangelen; j++) {
                ele = listNodeValue(ln);
                addReplySds(c,sdscatprintf(sdsempty(),"%d\r\n",(int)sdslen(ele->ptr)));
                addReply(c,ele);
                addReply(c,shared.crlf);
                ln = ln->next;
            }
        }
    }
}

void ltrimCommand(redisClient *c) {
    dictEntry *de;
    int start = atoi(c->argv[2]);
    int end = atoi(c->argv[3]);
    
    de = dictFind(c->dict,c->argv[1]);
    if (de == NULL) {
        addReplySds(c,sdsnew("-ERR no such key\r\n"));
    } else {
        robj *o = dictGetEntryVal(de);
        
        if (o->type != REDIS_LIST) {
            addReplySds(c, sdsnew("-ERR LTRIM against key not holding a list value"));
        } else {
            list *list = o->ptr;
            listNode *ln;
            int llen = listLength(list);
            int j, ltrim, rtrim;

            /* convert negative indexes */
            if (start < 0) start = llen+start;
            if (end < 0) end = llen+end;
            if (start < 0) start = 0;
            if (end < 0) end = 0;

            /* indexes sanity checks */
            if (start > end || start >= llen) {
                /* Out of range start or start > end result in empty list */
                ltrim = llen;
                rtrim = 0;
            } else {
                if (end >= llen) end = llen-1;
                ltrim = start;
                rtrim = llen-end-1;
            }

            /* Remove list elements to perform the trim */
            for (j = 0; j < ltrim; j++) {
                ln = listFirst(list);
                listDelNode(list,ln);
            }
            for (j = 0; j < rtrim; j++) {
                ln = listLast(list);
                listDelNode(list,ln);
            }
            addReply(c,shared.ok);
        }
    }
}
