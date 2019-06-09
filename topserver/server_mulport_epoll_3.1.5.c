
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/poll.h>

#include <errno.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include "cpubindstand.h"


#define SERVER_PORT		9000
#define MAX_BUFFER		1024
#define MAX_EPOLLSIZE	100000
#define MAX_THREAD		32
#define MAX_PORT		100
#define LIMIT_NUM 800000

#define CPU_CORES_SIZE	8

#define TIME_SUB_MS(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)

#define DIS_NAGLE 0

#define MSG_LOG 1
static int cpu_core_num;
static int ntySetNonblock(int fd) {
	int flags;

	flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0) return flags;
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0) return -1;
	return 0;
}


static void stopNagle(int fd)
{
	const char chOpt = 1;
    setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&chOpt, sizeof(char)); 
}

static int ntySetReUseAddr(int fd) {
	
	
	#if  DIS_NAGLE
	stopNagle(fd);
	#endif

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





typedef struct client {
	int fd;
	char rBuffer[MAX_BUFFER];
	int length;
} client_t;

void *client_cb(void *arg) {
	int clientfd = *(int *)arg;
	char buffer[MAX_BUFFER] = {0};

	int childpid = getpid();

	while (1) {
		bzero(buffer, MAX_BUFFER);
		ssize_t length = recv(clientfd, buffer, MAX_BUFFER, 0); //bio
		if (length > 0) {
			//printf(" PID:%d --> buffer: %s\n", childpid, buffer);

			int sLen = send(clientfd, buffer, length, 0);
			//printf(" PID:%d --> sLen: %d\n", childpid, sLen);
		} else if (length == 0) {
			printf(" PID:%d client disconnect\n", childpid);
			break;
		} else {
			printf(" PID:%d errno:%d\n", childpid, errno);
			break;
		}
	}
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

int listenfd(int fd, int *fds) {
	int i = 0;

	for (i = 0;i < MAX_PORT;i ++) {
		if (fd == *(fds+i)) return *(fds+i);
	}
	return 0;
}

static int curfds = 1;
static int nRun = 0;
//server socket
int sockfds[MAX_PORT] = {0};
char welcomeBuff[]="服务器主动问一声好....\n";

void client_job(job_t *job) {

	client_t *rClient = (client_t*)job->user_data;
	int clientfd = rClient->fd;

	char buffer[MAX_BUFFER];
	bzero(buffer, MAX_BUFFER);

	int length = 0;
	int ret = nRecv(clientfd, buffer, MAX_BUFFER, &length);
	if (length > 0) {	
	 
		if (buffer[0] == 'e'){
			nSend(clientfd, buffer, strlen(buffer), 0);
			#if MSG_LOG
			printf(" recv from  client id echo data -:=%d ,TcpRecv --> curfds : %d, buffer: %s\n", clientfd,curfds, buffer);
			#endif
		}else{
			nSend(clientfd, buffer, strlen(buffer), 0);
			#if MSG_LOG
			printf(" recv from  client-commdata ,id=%d ,TcpRecv --> curfds : %d, buffer: %s\n", clientfd,curfds, buffer);
			#endif
		}
		

	} else if (ret == ENOTCONN) {
		curfds --;
		close(clientfd);
	} else {
		
		nSend(clientfd, welcomeBuff, strlen(welcomeBuff), 0);
	}

	free(rClient);
	free(job);
}

void client_data_process(int clientfd) {

	char buffer[MAX_BUFFER];
	bzero(buffer, MAX_BUFFER);
	int length = 0;
	int ret = nRecv(clientfd, buffer, MAX_BUFFER, &length);
	if (length > 0) {	
	   if (buffer[0] == 'e'){
			nSend(clientfd, buffer, strlen(buffer), 0);
			#if MSG_LOG
			printf(" recv from  client id echo data -:=%d ,TcpRecv --> curfds : %d, buffer: %s\n", clientfd,curfds, buffer);
			#endif
		}else{
			#if MSG_LOG
			printf(" recv from  client-commdata ,id=%d ,TcpRecv --> curfds : %d, buffer: %s\n", clientfd,curfds, buffer);
			#endif
		}

	} else if (ret == ENOTCONN) {
		curfds --;
		close(clientfd);
	} else {
		
	}

}





static void checkState()
{
		if ((curfds ++ >LIMIT_NUM) && nRun==0) {
			nRun = 1;
			printf("switch jobs worker state ...\n");
		}

		if (curfds % 1000 == 999) {
			printf("connections: %d\n", curfds);	
		}
}

//初始化接入socket
static void initAcceptClient(int accpetorEpollfd,int clientfd )
{
	//set noblock io
		ntySetNonblock(clientfd);
		//set resuseaddr
		ntySetReUseAddr(clientfd);
		//init listener events
		struct epoll_event ev;
		ev.events = EPOLLIN | EPOLLET | EPOLLOUT;
		ev.data.fd = clientfd;
		epoll_ctl(accpetorEpollfd, EPOLL_CTL_ADD, clientfd, &ev);
}

//侦听io事件

void *listen_thread(void *arg) {

	int i = 0;
	int epoll_fd = *(int *)arg;
	struct epoll_event events[MAX_EPOLLSIZE];
    bindCpuCore(pthread_self(),pthread_self()%cpu_core_num);
	cpu_core_num
	while (1) {

		int nfds = epoll_wait(epoll_fd, events, curfds, 5);  
		if (nfds == -1) {
			perror("epoll_wait");
			break;
		}
		for (i = 0;i < nfds;i ++) {

			int sockfd = listenfd(events[i].data.fd, sockfds);
			//侦听server socket
			if (sockfd) {
				struct sockaddr_in client_addr;
				memset(&client_addr, 0, sizeof(struct sockaddr_in));
				socklen_t client_len = sizeof(client_addr);
				//主机serversocket 只有正常情况(非异常事件)只有接入事件
				int clientfd = accept(sockfd, (struct sockaddr*)&client_addr, &client_len);
				if (clientfd < 0) {
					perror("accept");
					return NULL;
				}
				checkState();
				initAcceptClient(epoll_fd,clientfd);
				
			} else {

				int clientfd = events[i].data.fd;
				//两种工作模式
				
				if (nRun && 0) {
					client_data_process(clientfd);
				} else {
			        //生产任务 output jobs
					client_t *rClient = (client_t*)malloc(sizeof(client_t));
					memset(rClient, 0, sizeof(client_t));				
					rClient->fd = clientfd;
					
					job_t *job = malloc(sizeof(job_t));
					job->job_function = client_job;
					job->user_data = rClient;
					workqueue_add_job(&workqueue, job);
					
				}
			}
		}
		
	}
	
}

int main(void) {
	
    cpu_core_num = sysconf(_SC_NPROCESSORS_CONF);
	int i = 0;
	//printf("C1000K Server Start\n");
	//init job workers
	threadpool_init(); //

	
	////////////////////////////////bind acceptor-epoller-to-epoll worker  start////////////////////////////////////////////// 
	int epoll_fds[CPU_CORES_SIZE] = {0};
	pthread_t thread_id[CPU_CORES_SIZE] = {0};

	for (i = 0;i < CPU_CORES_SIZE;i ++) {
		epoll_fds[i] = epoll_create(MAX_EPOLLSIZE);

		pthread_create(&thread_id[i], NULL, listen_thread, &epoll_fds[i]);
	}

	for (i = 0;i < MAX_PORT;i ++) {

		int sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sockfd < 0) {
			perror("socket");
			return 1;
		}

		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(struct sockaddr_in));
		
		addr.sin_family = AF_INET;
		addr.sin_port = htons(SERVER_PORT+i);
		addr.sin_addr.s_addr = INADDR_ANY;
        ntySetReUseAddr(sockfd);
		if (bind(sockfd, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)) < 0) {
			perror("bind");
			return 2;
		}

		if (listen(sockfd, 50000) < 0) {
			perror("listen");
			return 3;
		}

		sockfds[i] = sockfd;
		printf(" Server Listen on Port:%d\n", SERVER_PORT+i);

		struct epoll_event ev;
		 
		ev.events = EPOLLIN | EPOLLET; //EPOLLLT
		ev.data.fd = sockfd;
		epoll_ctl(epoll_fds[i%CPU_CORES_SIZE], EPOLL_CTL_ADD, sockfd, &ev);  
	}
	
	/////////////////////////////bind accpet-epoller-to-epoll worker  end////////////////////////////////////////////////// 
	
	
	
	/////////////////////pause thread workers/////////////////////////////////
	
	for (i = 0;i < CPU_CORES_SIZE;i ++) {
		pthread_join(thread_id[i], NULL);
	}


	getchar();
	printf("end\n");
}






