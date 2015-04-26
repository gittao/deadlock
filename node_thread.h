/*************************************************************************
    > File Name: node_thread.h
    > Author: tao
    > Mail: tao_zhi126@126.com 
    > Created Time: 2014年11月07日 星期五 02时07分25秒
 ************************************************************************/


#ifndef HAVE_NODE_THREAD_H
#define HAVE_NODE_THREAD_H

#include "global.h"

typedef std::tr1::unordered_map<IntPair, bool, IntPairHash, IntPairEqual> IntPair_Bool_Map;

typedef struct node_thread {
    pthread_t thread_id;        /* unique ID of this thread */
	int32_t tid;
	uint16_t port;
	muduo::Timestamp thread_initial_timestamp;  /* 线程启动时间，之后线程被死锁检测算法选为牺牲者时，会更新该时间，从而表示一个新的线程 */

	bool is_resource_fetch_started; /* 是否开始获取资源，已经建立好到各个resource manager的连接后设置，之后该参数不再使用。 */

	bool is_execution_started;   /* 当获取足够的资源后，节点可以执行任务了，同时启动一个定时器，定时完毕后释放所获得的资源。*/
	
	//死锁检测定时器，注意与上一句使用的定时器不同。
	bool is_deadlock_timeout_timer_started; /* 超时定时器是否启动。*/
	TimerId deadlock_timer_id;   /* 保存在这里，从而可以cancel. */
	
	// 用于死锁检测算法
	bool is_deadlock_detection_started;
	bool is_deadlock_detection_leader;
	Timestamp deadlock_detection_initialization_timestamp;
	double total_weight;

	// 用于解决全局死锁
	int num_of_responce;
	bool resource_manager_id_arr[100];  /*记录从哪些resource manager收到RESPONCE；假设资源管理节点不超过100*/
	std::map<int, ExpressionNodePtr> global_expression_map;

	//用于查询是否已经注册到本地映射表中，注意此时自己不一定是leader。
	//该表的内存并没有进行回收动作。
	boost::shared_ptr<IntPair_Bool_Map> register_map_ptr;

	int connected_resource_manager_num;
	TcpConnectionPtr* resource_manager_connection_array;

	//当前需要的资源表达关系，用于线程本身的运行。
	muduo::Timestamp current_resource_request_timestamp;
	int resource_num;
	std::vector<int> resource_ids;
	std::map<int, bool> resource_available_map;
	ExpressionNodePtr head;
	bool expression_result; //1表示资源表达式的与或结果为1，表示可以往下执行

	
} NODE_THREAD;

void node_thread_init(int nthreads);

#endif
