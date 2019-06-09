
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
#include "ioutils.h"

#define EPOLL_CONTEXT_EVENT_SIZE 30000
#define EPOLL_CONTEXT_CLIENTS_SIZE 150000
#define EPOLL_CONTEXT_BUFFSIZE 1024

#define IO_CTX_WORKERS_MAX 100
#define TCP_CHANNEL_PORT_MAX_NUM 100

#define CLIENT_STATE_CLOSE 0
#define LIMIT_NUMS 200000
static long long total_connctions_count=0;




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
    memset(c, 0, sizeof(c));
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


void client_handler_read(Client_t* client)
{
     //printf("can read ......%ld\n",getCurrentTime());
    char buffer[EPOLL_CONTEXT_BUFFSIZE]={0};
     int epfd =client->ctx->epollfd;
    int nread=read_from_fileAndCheckClose(client->sockfd,buffer,EPOLL_CONTEXT_BUFFSIZE);
    if (nread>0)
    {
        printf("recv server data: len=%d  str:=%s  current-ctx-online_client_count= %d\n",nread,buffer,client->ctx->online_client_count);
    }else if (nread==FAILURE_PLEASE_CLOSE_SELF_SOCK)
    {
       
        epoll_del(epfd,client->sockfd,EPOLLIN);
        client_close(client);
        return ;
    }
       //epoll_modify(epfd,client->sockfd,EPOLLOUT,client);
}

const char msg[][1024]={
    "client say : hellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellohellobenjourbenjourbenjourbenjour",
    "client say: benjourbenjourbenjourbenjourbenjourbenjourbenjourbenjourbenjourbenjourbenjourbenjourbenjourbenjourbenjourbenjourbenjourbenjourbenjourbenjourbenjourbenjourbenjourbenjourbenjourbenjourbenjour",
    "client say :seachseachseachseachseachseachseachseachseachseachseachseachseachseachseachseachseachseachseachseachseachseachseachseachseachseachseachseachseachseachseachseachseachseachseachseachseachseachseachseach"
};

int start_send =0;

#define ARRLEN(ARR) sizeof(ARR)/sizeof(ARR[0])

void client_handler_write(Client_t* client)
{
   //printf("can write ......%ld\n",getCurrentTime());
   //*****
  
   if (total_connctions_count>=LIMIT_NUMS || start_send)
   {
        char buffer[EPOLL_CONTEXT_BUFFSIZE]={0};
        int msgid = client->sockfd%3;
        int res= write_to_file(client->sockfd,msg[msgid],strlen(msg[msgid]));
        if (res==FAILURE_PLEASE_CLOSE_SELF_SOCK)
        {
            epoll_del(client->ctx->epollfd,client->sockfd,EPOLLOUT);
            client_close(client);
            return ;
        }
    }
   // *****/
   //epoll_modify(client->ctx->epollfd,client->sockfd,EPOLLIN,client);
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
    return sockfd_tel;
}

int epollContext_accpet_connection(EPollContext_T *ctx,int clientfd)
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
             if (ctx->events[i].events & EPOLLIN) //can read
                {
                     client_handler_read(ctx->events[i].data.ptr);
                    
                }else if (ctx->events[i].events & EPOLLOUT)
                {
                    client_handler_write(ctx->events[i].data.ptr);
                }
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
         usleep(1000);
         
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

    int baseport = atoi(argvs[2]);
    const char* ip = argvs[1];
    initIOROLL();

    connLoop(ip,baseport,TCP_CHANNEL_PORT_MAX_NUM);
    getchar();

    return 0;
}
