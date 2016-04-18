#include "ae.h"
#include "ae_epoll.h"

int aeApiCreate(aeEventLoop *eventLoop) {
    aeApiState *state = malloc(sizeof(aeApiState));
    
    if (!state) return -1;
    state->events = malloc(sizeof(struct epoll_event) * eventLoop->setsize);
    if (!state->events) {
        free(state);
        return -1;
    }
    
    state->epfd = epoll_create(1024); /**/
    if (state->epfd == -1) {
        free(state->events);
        free(state);
        return -1;
    }
    
    eventLoop->apidata = state;
    return 0;
}

int aeApiResize(aeEventLoop *eventLoop, int setsize) {
    aeApiState *state = eventLoop->apidata;
    
    state->events = realloc(state->events, sizeof(struct epoll_event) * setsize);
    return 0;
}


void aeApiFree(aeEventLoop *eventLoop) {
    aeApiState *state = eventLoop->apidata;
    
    close(state->epfd);
    free(state->events);
    free(state);
}

int aeApiAddEvent(aeEventLoop *eventLoop, int fd, int mask) {
    aeApiState * state = eventLoop->apidata;
    struct epoll_event ee;
    int op = eventLoop->events[fd].mask == AE_NONE ?
            EPOLL_CTL_ADD : EPOLL_CTL_MOD;   
            
    ee.events = 0;
    mask |= eventLoop->events[fd].mask; /*merge old events*/
    if (mask & AE_READABLE) ee.events |= EPOLLIN;
    if (mask & AE_WRITABLE) ee.events |= EPOLLOUT;
    ee.data.u64 = 0;
    ee.data.fd = fd;
    if (epoll_ctl(state->epfd, op, fd, &ee) == -1) return -1;
    return 0;
}

int aeApiDelEvent(aeEventLoop *eventLoop, int fd, int delmask) {
    aeApiState * state = eventLoop->apidata;
    struct epoll_event ee;
    int mask = eventLoop->events[fd].mask & (~delmask);
    
    ee.events = 0;
    if (mask & AE_READABLE) ee.events |= EPOLLIN;
    if (mask & AE_WRITABLE) ee.events |= EPOLLOUT;
    ee.data.u64 = 0;
    ee.data.fd = fd;
    if (mask | AE_NONE) {
        epoll_ctl(state->epfd, EPOLL_CTL_MOD, fd, &ee);
    } else {
        epoll_ctl(state->epfd, EPOLL_CTL_DEL, fd, &ee);
    }
    return 0;
}


int aeApiPoll(aeEventLoop *eventLoop, struct timeval *tvp) {
    aeApiState *state = eventLoop->apidata;
    int retval, numevents = 0;
    
    /* state->epfd:由epoll_create生成的epoll专用的文件描述符，系统也要知道是哪个epoll
     * state->events:用于回传代理事件的数组
     * eventLoop->setsize:每次能处理的事件数
     * timeout:等待IO事件发生的超时值（单位毫秒），-1表示一直等待，0表示不等待
     */
    retval = epoll_wait(state->epfd, state->events, eventLoop->setsize,
                        tvp ? (tvp->tv_sec * 1000 + tvp->tv_usec/1000) : -1);
    if (retval > 0) {
        int j;
        
        numevents = retval;
        for (j = 0; j < numevents; j++) {
            int mask = 0;
            struct epoll_event *e = state->events + j;
            
            if (e->events & EPOLLIN) mask |= AE_READABLE;
            if (e->events & EPOLLOUT) mask |= AE_WRITABLE;
            if (e->events & EPOLLERR) mask |= AE_WRITABLE;
            if (e->events & EPOLLHUP) mask |= AE_WRITABLE;
            eventLoop->fired[j].fd = e->data.fd;
            eventLoop->fired[j].mask = mask;
        }
    }
    
    return numevents;
}

char *aeApiName(void) {
    return "epoll";
}