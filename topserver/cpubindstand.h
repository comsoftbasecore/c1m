
#ifndef __CPU_BINDS_STAND__
#define __CPU_BINDS_STAND__
#include <pthread.h>
extern int  bindCpuCore(pthread_t  t,int core);
extern int findThreadAtWhichCpuCore(pthread_t thrd,int MAXCPUCORE);

/**** example

#include <stdio.h>
#include <unistd.h>
#include <sys/sysinfo.h>
#include "cpubindstand.h"


#define MAX_CPU_CORES  		16
int num = 0;


void *thread_func(void *arg) {
	 int core = *(int*)arg;

	 bindCpuCore(pthread_self(),core);
	 int retCore= findThreadAtWhichCpuCore(pthread_self(),num);
	 if (retCore>=0)
         printf("find Thread %ld run at core : %d\n", pthread_self(),retCore);
 
	return NULL;
}

int main (int argc, char *argv[]) {

	num = sysconf(_SC_NPROCESSORS_CONF);
	printf(" cpu cores : %d\n", num);
	num /= 2;

	pthread_t thread_id[MAX_CPU_CORES];
	int cores[MAX_CPU_CORES] = {0};
	int i = 0;
	for (i = 0;i < num;i ++) {
		cores[i] = i;
		pthread_create(&thread_id[i], NULL, thread_func, &cores[i]);
	}

	for (i = 0;i < num;i ++) {
		pthread_join(thread_id[i], NULL);
	}

	return 0;
}

**********/

#endif