/*************************************************************************
    > File Name: global.cpp
    > Author: tao
    > Mail: tao_zhi126@126.com 
    > Created Time: 2014年11月09日 星期日 23时18分44秒
 ************************************************************************/

#include "global.h"

int resource_manager_num;
uint16_t resource_manager_first_port;
int resource_manager_resource_num;
int node_thread_num_for_each_manager;
uint16_t node_thread_first_port;

//For each process
int resource_manager_id;

int fd_for_lock; //用于进程同步

pthread_mutex_t mutex_for_lock = PTHREAD_MUTEX_INITIALIZER;

/*
void wait_before_send(int local_resource_manager_id, int to_resource_manager_id)
{
	if (local_resource_manager_id == to_resource_manager_id)
		return;
	usleep(80 * 1000);
}
*/

double wait_time_before_send(int local_resource_manager_id, int to_resource_manager_id)
{

	if (local_resource_manager_id == to_resource_manager_id)
		return -1;
	else
		return 20.0 / 1000;   //20ms
}
