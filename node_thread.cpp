/*************************************************************************
    > File Name: node_thread.cpp
    > Author: tao
    > Mail: tao_zhi126@126.com 
    > Created Time: 2014年11月07日 星期五 02时14分38秒
 ************************************************************************/
#include "global.h"

#include "node_thread.h"
#include "node_thread_server.h"

#include "resource_manager_client.h"

static NODE_THREAD *threads;

static int init_count = 0;
static pthread_mutex_t init_lock;
static pthread_cond_t init_cond;

static void wait_for_thread_registration(int nthreads) {
    while (init_count < nthreads) {
        pthread_cond_wait(&init_cond, &init_lock);
    }
}

static void register_thread_initialized(void) {
    pthread_mutex_lock(&init_lock);
    init_count++;
    pthread_cond_signal(&init_cond);
    pthread_mutex_unlock(&init_lock);
}

static void *node_thread(void *arg) {
    NODE_THREAD *me = static_cast<NODE_THREAD *>(arg);
	LOG_INFO << "pthread_t = " << pthread_self() << "; " << "tid = " << muduo::CurrentThread::tid();
	me->thread_id = pthread_self();
	me->tid = muduo::CurrentThread::tid();

	me->thread_initial_timestamp = muduo::Timestamp::now();

    register_thread_initialized();
	
	EventLoop loop;

	InetAddress listenAddr(me->port);


	//构建node thread server。
	InetAddress* server_addrs = new InetAddress[resource_manager_num];
	for (int i = 0; i < resource_manager_num; ++i) {
		server_addrs[i] = InetAddress(static_cast<uint16_t>(resource_manager_first_port + i));
	}
	
	NodeThreadServer server(&loop, listenAddr, resource_manager_num, server_addrs, me);
	delete[] server_addrs;

	server.start();
	
	loop.loop();

    return NULL;
}

static void create_worker(void *(*func)(void *), void *arg) {
    pthread_t       thread;
    pthread_attr_t  attr;
    int             ret;

    pthread_attr_init(&attr);

    if ((ret = pthread_create(&thread, &attr, func, arg)) != 0) {
        fprintf(stderr, "Can't create thread: %s\n",
                strerror(ret));
        exit(1);
    }
}

//创建n个线程，作为节点
void node_thread_init(int nthreads) {
	int i;

	pthread_mutex_init(&init_lock, NULL);
    pthread_cond_init(&init_cond, NULL);

	//注意，我只申请内存，但没有释放；实际上，问题不大。
    threads = static_cast<NODE_THREAD *>(calloc(nthreads, sizeof(NODE_THREAD)));
    if (! threads) {
        perror("Can't allocate thread descriptors");
        exit(1);
    }
	
	//初始化NODE_THREAD结构体。
	//从配置文件参数来获得端口范围
	uint16_t temp_port = static_cast<uint16_t>(node_thread_first_port + resource_manager_id * node_thread_num_for_each_manager);
    for (i = 0; i < nthreads; i++) {
        threads[i].port = temp_port++;
		threads[i].is_resource_fetch_started = false;
		threads[i].connected_resource_manager_num = 0;
		threads[i].resource_manager_connection_array = static_cast<TcpConnectionPtr *>(calloc(resource_manager_num, sizeof(TcpConnectionPtr)));
    }

    for (i = 0; i < nthreads; i++) {
        create_worker(node_thread, &threads[i]);
    }

    /* Wait for all the threads to set themselves up before returning. */
    pthread_mutex_lock(&init_lock);
    wait_for_thread_registration(nthreads);
    pthread_mutex_unlock(&init_lock);
}

