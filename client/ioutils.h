#ifndef __IO_UTILS__H
#define __IO_UTILS__H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <errno.h>
#include <sys/time.h>      //添加头文件
#include <unistd.h>


#define  BUFSIZE   1024
#define  SUCCESS   0
#define  FAILURE   -2000001

#define  FAILURE_PLEASE_CLOSE_SELF_SOCK   -2000000

int write_to_file (int fd, const char * buf, const size_t nbytes){
    size_t left =  nbytes;
    size_t written_len = 0;
    const char * p_tmp = buf;


    while(left > 0){
        written_len = write(fd, p_tmp,left);
        
        if(written_len<0){
	    written_len = -errno;
            
            if(written_len == EINTR || written_len == EAGAIN ){
                continue;
            }
            if(written_len == EBADF){
                //重新open 这个文件，对它进行重写 
                break;
            }
            //
            fprintf(stderr,"write failed. reason:%s\n",strerror(errno));
            return FAILURE_PLEASE_CLOSE_SELF_SOCK;

	}else if( 0 == written_len ){
            return FAILURE_PLEASE_CLOSE_SELF_SOCK;
        }

        left -= written_len;
        p_tmp += written_len;
    }

    if( 0 != left) return FAILURE;
    return SUCCESS;

}

 
 struct timeval tv;  
 //t_end = GetTickCount(); 类似于windows
int64_t getCurrentTime()      //直接调用这个函数就行了，返回值最好是int64_t，long long应该也可以
{    
    gettimeofday(&tv,NULL);    //该函数在sys/time.h头文件中
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;    
}    
       
int read_from_fileAndCheckClose (int fd,  char * buf, const size_t nbytes){
     int nready = read(fd,buf,nbytes);
    
    if (nready==0)
     {
             return FAILURE_PLEASE_CLOSE_SELF_SOCK;
     }else if (nready<0){
           int err = -errno;
           if (err==EINTR || err==EAGAIN)
           {
               

           }else if (err==EPIPE || err==ENOTCONN)
           {

             return FAILURE_PLEASE_CLOSE_SELF_SOCK;
           }
           return err;
     }

    return nready;

}



static int ntySetNonblock(int fd) {
	int flags;

	flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0) return flags;
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0) return -1;
	return 0;
}

static int ntySetReUseAddr(int fd) {
	int reuse = 1;
	return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
}

static int ntySetAlive(int fd) {
	int alive = 1;
	int idle = 60;
	int interval = 5;
	int count = 2;

	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void*)&alive, sizeof(alive));
	setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, (void*)&idle, sizeof(idle));
	setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, (void*)&interval, sizeof(interval));
	setsockopt(fd, SOL_TCP, TCP_KEEPCNT, (void*)&count, sizeof(count));
}


static void epoll_add(int epfd,int targetfd,int state,void* savedataptr)
{
    struct epoll_event ev;
    ev.events = state;
    if (savedataptr==0)
    {
        ev.data.fd = targetfd;
    }else
    {
        ev.data.ptr = savedataptr;
    }
	
	epoll_ctl(epfd, EPOLL_CTL_ADD, targetfd, &ev);
}

static void epoll_modify(int epfd,int targetfd,int state,void* savedataptr)
{
    struct epoll_event ev;
    ev.events = state;
    if (savedataptr==0)
    {
        ev.data.fd = targetfd;
    }else
    {
        ev.data.ptr = savedataptr;
    }
	epoll_ctl(epfd, EPOLL_CTL_MOD, targetfd, &ev);
    
}

static void epoll_del(int epfd,int targetfd,int state)
{
    struct epoll_event ev;
    ev.events =state;
	ev.data.fd = targetfd;
	epoll_ctl(epfd, EPOLL_CTL_DEL, targetfd, &ev);
}
#endif

