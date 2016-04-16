#ifndef AE_EPOLL_H
#define AE_EPOLL_H

#include <sys/epoll.h>

typedef struct aeApiState {
    int epfd;
    struct epoll_event *events;
} aeApiState;


#endif