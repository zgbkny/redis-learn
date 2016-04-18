#ifndef COMMAND_H
#define COMMAND_H

#include "redis.h"

typedef void redisCommandProc(redisClient *c);
struct redisCommand {
    char *name;
    redisCommandProc *proc;
    int arity;
    int type;
};

void pingCommand(redisClient *c);
void echoCommand(redisClient *c);
void setCommand(redisClient *c);
void setnxCommand(redisClient *c);
void getCommand(redisClient *c);
void delCommand(redisClient *c);
void existsCommand(redisClient *c);
void incrCommand(redisClient *c);
void decrCommand(redisClient *c);
void selectCommand(redisClient *c);
void randomkeyCommand(redisClient *c);
void keysCommand(redisClient *c);
void dbsizeCommand(redisClient *c);
void lastsaveCommand(redisClient *c);
void saveCommand(redisClient *c);
void bgsaveCommand(redisClient *c);
void shutdownCommand(redisClient *c);
void moveCommand(redisClient *c);
void renameCommand(redisClient *c);
void renamenxCommand(redisClient *c);
void lpushCommand(redisClient *c);
void rpushCommand(redisClient *c);
void lpopCommand(redisClient *c);
void rpopCommand(redisClient *c);
void llenCommand(redisClient *c);
void lindexCommand(redisClient *c);
void lrangeCommand(redisClient *c);
void ltrimCommand(redisClient *c);

#endif 