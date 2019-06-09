#include <sys/types.h>
#define __USE_GNU
#include "cpubindstand.h"

int  bindCpuCore(pthread_t thrd,int core)
{
	cpu_set_t m;
	cpu_set_t *mask=&m ; 
	
	cpu_set_t g;
	cpu_set_t *get= &g; 

	CPU_ZERO(mask);//设置位图
	CPU_SET(core, mask);//获取位图
	CPU_ZERO(get);
	pthread_setaffinity_np(thrd, sizeof(cpu_set_t), mask);
	pthread_getaffinity_np(thrd, sizeof(cpu_set_t), get);
	return  CPU_ISSET(core, get);
}

int findThreadAtWhichCpuCore(pthread_t thrd,int MAXCPUCORE)
{
	cpu_set_t g;
	cpu_set_t *get =&g;   
	CPU_ZERO(get);
	pthread_getaffinity_np(thrd, sizeof(cpu_set_t), get);
	int i;
	for (i = 0;i < MAXCPUCORE;i ++) {
		 if (CPU_ISSET(i, get)) {
				return i;
			}
	 }
	 return -1; //没有绑定
}





