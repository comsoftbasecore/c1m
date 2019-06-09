
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <stdlib.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <string.h>
#include <sys/poll.h>
#include <time.h>
#include <unistd.h>

#include "ioutils.h"

#define EPOLL_CONTEXT_EVENT_SIZE 30000
#define EPOLL_CONTEXT_CLIENTS_SIZE 150000
#define EPOLL_CONTEXT_BUFFSIZE 1024

#define IO_CTX_WORKERS_MAX 10
#define TCP_CHANNEL_PORT_MAX_NUM 100

#define CLIENT_STATE_CLOSE 0
#define LIMIT_NUMS 400000
#define MAX_BUFFER 1024
#define MAX_THREAD 10


#define CLIENT_RIGHT_NOW_MASTER_COMMUNICATIOIN 1  //立即主动通信，不等待建立LIMIT_NUMS个连接 后主动通信
#define DIS_NAGLE 1

static long long total_connctions_count=0;

//master to send msg flag
int start_send =0;

#define ARRLEN(ARR) sizeof(ARR)/sizeof(ARR[0])



/** **** ******** **************** thread pool **************** ******** **** **/

#define LL_ADD(item, list) { \
	item->prev = NULL; \
	item->next = list; \
	list = item; \
}

#define LL_REMOVE(item, list) { \
	if (item->prev != NULL) item->prev->next = item->next; \
	if (item->next != NULL) item->next->prev = item->prev; \
	if (list == item) list = item->next; \
	item->prev = item->next = NULL; \
}

typedef struct worker {	
	pthread_t thread;	
	int terminate;	
	struct workqueue *workqueue;	
	struct worker *prev;	
	struct worker *next;
} worker_t;

typedef struct job {	
	void (*job_function)(struct job *job);	
	void *user_data;	
	struct job *prev;	
	struct job *next;
} job_t;

typedef struct workqueue {	
	struct worker *workers;	
	struct job *waiting_jobs;	
	pthread_mutex_t jobs_mutex;	
	pthread_cond_t jobs_cond;
} workqueue_t;

int c = 0;
static void *worker_function(void *ptr) {	
	worker_t *worker = (worker_t *)ptr;	
	job_t *job;	
	c++;
    printf("worker_function  %d.... \n",c);
	while (1) {			
		pthread_mutex_lock(&worker->workqueue->jobs_mutex);		
		while (worker->workqueue->waiting_jobs == NULL) {			
			if (worker->terminate) break;			
			pthread_cond_wait(&worker->workqueue->jobs_cond, &worker->workqueue->jobs_mutex);		
		}			
		if (worker->terminate) break;		
		job = worker->workqueue->waiting_jobs;		
		if (job != NULL) {			
			LL_REMOVE(job, worker->workqueue->waiting_jobs);		
		}		
		pthread_mutex_unlock(&worker->workqueue->jobs_mutex);		
			
		if (job == NULL) continue;	
		
		/* Execute the job. */		
		job->job_function(job);	
	}	
	
	free(worker);	
	pthread_exit(NULL);
}

int workqueue_init(workqueue_t *workqueue, int numWorkers) {	
	int i;	
	worker_t *worker;	
	pthread_cond_t blank_cond = PTHREAD_COND_INITIALIZER;	
	pthread_mutex_t blank_mutex = PTHREAD_MUTEX_INITIALIZER;	

	if (numWorkers < 1) numWorkers = 1;	
	
	memset(workqueue, 0, sizeof(*workqueue));	
	memcpy(&workqueue->jobs_mutex, &blank_mutex, sizeof(workqueue->jobs_mutex));	
	memcpy(&workqueue->jobs_cond, &blank_cond, sizeof(workqueue->jobs_cond));	

	for (i = 0; i < numWorkers; i++) {		
		if ((worker = malloc(sizeof(worker_t))) == NULL) {			
			perror("Failed to allocate all workers");			
			return 1;		
		}		

		memset(worker, 0, sizeof(*worker));		
		worker->workqueue = workqueue;		
		
		if (pthread_create(&worker->thread, NULL, worker_function, (void *)worker)) {			
			perror("Failed to start all worker threads");			
			free(worker);			
			return 1;		
		}		

		LL_ADD(worker, worker->workqueue->workers);	
	}	

	return 0;
}


void workqueue_shutdown(workqueue_t *workqueue) {	

	worker_t *worker = NULL;		
	for (worker = workqueue->workers; worker != NULL; worker = worker->next) {		
		worker->terminate = 1;	
	}	


	pthread_mutex_lock(&workqueue->jobs_mutex);	
	workqueue->workers = NULL;	
	workqueue->waiting_jobs = NULL;	
	pthread_cond_broadcast(&workqueue->jobs_cond);	
	pthread_mutex_unlock(&workqueue->jobs_mutex);

}


void workqueue_add_job(workqueue_t *workqueue, job_t *job) {	

	pthread_mutex_lock(&workqueue->jobs_mutex);	

	LL_ADD(job, workqueue->waiting_jobs);	

	pthread_cond_signal(&workqueue->jobs_cond);	
	pthread_mutex_unlock(&workqueue->jobs_mutex);

}

static workqueue_t workqueue;
void threadpool_init(void) {
	workqueue_init(&workqueue, MAX_THREAD);
}


/** **** ******** **************** thread pool **************** ******** **** **/













struct EPollContext ;
typedef struct EPollContext EPollContext_T;
struct Client;
typedef struct Client Client_t;
 struct EPollContext
{
    int epollfd;
    Client_t * clientlist[EPOLL_CONTEXT_CLIENTS_SIZE];
    struct epoll_event events[EPOLL_CONTEXT_EVENT_SIZE];
    int online_client_count;
} ;

typedef struct 
{
    EPollContext_T epollctx;
    pthread_t genEvents_pt;

}IOPollContxt;

IOPollContxt ioPollCxt[IO_CTX_WORKERS_MAX];


struct Client
{
    int sockfd;
    int ctxid;
    EPollContext_T *ctx;
    int state;

};

Client_t *newClient()
{
    Client_t *c = malloc(sizeof(Client_t));
    memset((char*)c, 0, sizeof(c));
    return c;
}


void client_close(Client_t* client)
{
    client->state = CLIENT_STATE_CLOSE;
    close(client->sockfd);
   if (client->ctx->online_client_count>0){
        --client->ctx->online_client_count;
    }
  if(total_connctions_count>0)
    {
        --total_connctions_count;
    }
	free(client);
    
}

static int nRecv(int sockfd, void *data, size_t length, int *count) {
	int left_bytes;
	int read_bytes;
	int res;
	int ret_code;

	unsigned char *p;

	struct pollfd pollfds;
	pollfds.fd = sockfd;
	pollfds.events = ( POLLIN | POLLERR | POLLHUP );

	read_bytes = 0;
	ret_code = 0;
	p = (unsigned char *)data;
	left_bytes = length;

	while (left_bytes > 0) {

		read_bytes = recv(sockfd, p, left_bytes, 0);
		if (read_bytes > 0) {
			left_bytes -= read_bytes;
			p += read_bytes;
			continue;
 		} else if (read_bytes < 0) {
			if (!(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
				ret_code = (errno != 0 ? errno : EINTR);
			}
		} else {
			ret_code = ENOTCONN;
			break;
		}
		
		res = poll(&pollfds, 1, 5);
		if (pollfds.revents & POLLHUP) {
			ret_code = ENOTCONN;
			break;
		}
		if (res < 0) {
			if (errno == EINTR) {
				continue;
			}
			ret_code = (errno != 0 ? errno : EINTR);
		} else if (res == 0) {
			ret_code = ETIMEDOUT;
			break;
		}

	}

	if (count != NULL) {
		*count = length - left_bytes;
	}
	//printf("nRecv:%s, ret_code:%d, count:%d\n", (char*)data, ret_code, *count);


	return ret_code;
}

static int nSend(int sockfd, const void *buffer, int length, int flags) {

	int wrotelen = 0;
	int writeret = 0;

	unsigned char *p = (unsigned char *)buffer;

	struct pollfd pollfds = {0};
	pollfds.fd = sockfd;
	pollfds.events = ( POLLOUT | POLLERR | POLLHUP );

	do {
		int result = poll( &pollfds, 1, 5);
		if (pollfds.revents & POLLHUP) {
			
			printf(" ntySend errno:%d, revent:%x\n", errno, pollfds.revents);
			return -1;
		}

		if (result < 0) {
			if (errno == EINTR) continue;

			printf(" ntySend errno:%d, result:%d\n", errno, result);
			return -1;
		} else if (result == 0) {
		
			printf(" ntySend errno:%d, socket timeout \n", errno);
			return -1;
		}

		writeret = send( sockfd, p + wrotelen, length - wrotelen, flags );
		if( writeret <= 0 )
		{
			break;
		}
		wrotelen += writeret ;

	} while (wrotelen < length);
	
	return wrotelen;
}
static int curfds = 1;


static char noReplyMsg[]="you have no say ,so I say ....\n";
char *msg[]={
	 "echo client 1client 1client 1client 1client 1client 1client 1client 1client 1client 1",
	 "echo client 2222client 2222client2222client2222client2222client2222client2222client2222client",
	 "echo client 3client 3client 3client 3client 3client 3client 3client 3client 3client 3client 3"
};

#if 0
static void client_job(job_t *job) {

	Client_t *client = (Client_t*)job->user_data;
	int clientfd = client->sockfd;

	char buffer[MAX_BUFFER];
	bzero(buffer, MAX_BUFFER);

	int length = 0;
	int ret = nRecv(clientfd, buffer, MAX_BUFFER, &length);
	if (length > 0) {	
	//e :echo
	//w :welcome
	
	 if (buffer[0] == 'e') {		
			printf("recv server data: len=%d  str:=%s  current-ctx-online_client_count= %d\n",length,buffer,client->ctx->online_client_count);	
		}else if ( buffer[0] == 'w')
		{
			printf("recv server data: len=%d  str:=%s  current-ctx-online_client_count= %d\n",length,buffer,client->ctx->online_client_count);	
			//nSend(clientfd, buffer, strlen(buffer), 0);
		}
		

	} else if (ret == ENOTCONN) {
		curfds --;
		close(clientfd);
	} else {
		nSend(clientfd, noReplyMsg, strlen(noReplyMsg), 0);
	}

	free(job);
}

#endif

char noreply[]="n_client msg to server ,主动询问你\n";

void client_data_process(Client_t* client) {

    int clientfd = client->sockfd;
	int msgid= clientfd%3;
	char buffer[MAX_BUFFER];
	bzero(buffer, MAX_BUFFER);
	int length = 0;
	int ret = nRecv(clientfd, buffer, MAX_BUFFER, &length);
	if (length > 0) {
		
		if (buffer[0] == 'e') {		
			printf("recv server echo-back-data: len=%d  str:=%s  current-ctx-online_client_count= %d\n",length,buffer,client->ctx->online_client_count);	
		}else{
			printf("recv server comm_data: len=%d  str:=%s  current-ctx-online_client_count= %d\n",length,buffer,client->ctx->online_client_count);	
		}
	} else if (ret == ENOTCONN) {
		curfds --;
		close(clientfd);
	} else {
		#if CLIENT_RIGHT_NOW_MASTER_COMMUNICATIOIN
		  nSend(clientfd, msg[msgid], strlen(msg[msgid]), 0);
		#else
		if (start_send)
		{    //主动通信
			nSend(clientfd, msg[msgid], strlen(msg[msgid]), 0);
			//usleep(1000);
		}
        #endif	
		
	}

}


static void stopNagle(int fd)
{
	const char chOpt = 1;
    setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&chOpt, sizeof(char)); 
}


int createConnection(int port,const char* ip)
{
    //tel
    int sockfd_tel = socket(AF_INET, SOCK_STREAM, 0);
    // wire
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);
    //printf("ip %s port %d \n",ip,port );
    
    if (connect(sockfd_tel, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("socket connect failed");
        return -1;
    }
    ntySetNonblock(sockfd_tel);
	#if  DIS_NAGLE
	stopNagle(sockfd_tel);
	#endif
    return sockfd_tel;
}

void epollContext_accpet_connection(EPollContext_T *ctx,int clientfd)
{
    ntySetNonblock(clientfd);
    Client_t * c = newClient();
    c->sockfd = clientfd;
    c->ctx = ctx;
     ++ctx->online_client_count;
    epoll_add(ctx->epollfd,clientfd,EPOLLIN|EPOLLOUT,c);
    total_connctions_count++;
    if (total_connctions_count%1000==0)
        printf("clientfd =%d ,ctx_count= %d, total_connctions_count=%lld\n",clientfd,ctx->online_client_count,total_connctions_count);
}


void* epollContext_start_generator_io(void * arg)
{

    EPollContext_T *ctx =(EPollContext_T*) arg; 
    char buffer[EPOLL_CONTEXT_BUFFSIZE];
    memset(buffer,0,EPOLL_CONTEXT_BUFFSIZE);
    while (ctx->online_client_count < EPOLL_CONTEXT_CLIENTS_SIZE)
    {
        int nready = epoll_wait(ctx->epollfd, ctx->events, ctx->online_client_count + 1, 100);
        if (nready > 0)
        {
            int i;
            for (i=0;i<nready;i++)
            {
				
				client_data_process(ctx->events[i].data.ptr);
				 /******生产任务 output jobs
					job_t *job = malloc(sizeof(job_t));
					job->job_function = client_job;
					job->user_data = ctx->events[i].data.ptr;
					workqueue_add_job(&workqueue, job);
					*******/
            }
        } 
    }
     return (void*)0;
}


void  connLoop(const char *ip,int baseport,int num)
{

    int i=0 ;
    int connfd =-1;
    EPollContext_T * ctx;

    while(1)
    {
        connfd =-1;
        if (i==TCP_CHANNEL_PORT_MAX_NUM)
        {
            i=0;
        }
        connfd = createConnection(baseport+i,ip);
         ++i;
        if(connfd>=0)
         {
                ctx = &(ioPollCxt[(baseport+i)%IO_CTX_WORKERS_MAX].epollctx);
                epollContext_accpet_connection(ctx,connfd);
         }
         if(total_connctions_count>=LIMIT_NUMS){
            start_send =1;
            break;
         }
        
         
    }
}


void initIOROLL()
{
     int i ;
     for (i=0;i<IO_CTX_WORKERS_MAX;i++)
     {
         ioPollCxt[i].epollctx.epollfd = epoll_create(EPOLL_CONTEXT_EVENT_SIZE);
         pthread_create(&(ioPollCxt[i].genEvents_pt),0,epollContext_start_generator_io,&(ioPollCxt[i].epollctx));
         printf("epoller context =%d.\n",i);
     }
}


int main(int argc, const char **argvs)
{
    if (argc < 3)
    {
        printf("please use ip ,port just like format ./app 192.168.1.128  40000\n");
        exit(1);
    }
    //initSendData();
    int baseport = atoi(argvs[2]);
    const char* ip = argvs[1];
	
	//threadpool_init(); 
    
	initIOROLL();

    connLoop(ip,baseport,TCP_CHANNEL_PORT_MAX_NUM);
    getchar();

    return 0;
}
