/*************************************************************************
    > File Name: global.h
    > Author: tao
    > Mail: tao_zhi126@126.com 
    > Created Time: 2014年11月08日 星期六 00时59分36秒
 ************************************************************************/


#ifndef HAVE_GLOBAL_H
#define HAVE_GLOBAL_H

#include <cstdio>
#include <cstdlib>
#include <stdint.h>
#include <cassert>
#include <fcntl.h>

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>

#include <boost/bind.hpp>
#include <boost/function.hpp>

#include <pthread.h>

#include <boost/serialization/vector.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/version.hpp>
#include <boost/serialization/split_member.hpp>
#include <boost/serialization/shared_ptr.hpp>
#include <boost/functional/hash.hpp>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

// 发现boost的unordered_map与<cstdint>不太兼容。
#include <tr1/unordered_map>
#include <tr1/unordered_set>

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

#include <vector>
#include <map>
#include <set>
#include <iterator>

using namespace muduo;
using namespace muduo::net;

#include "expression_node.h"

extern int resource_manager_num;
extern uint16_t resource_manager_first_port;
extern int resource_manager_resource_num;
extern int node_thread_num_for_each_manager;
extern uint16_t node_thread_first_port;

extern int resource_manager_id;

extern int fd_for_lock;
extern pthread_mutex_t mutex_for_lock;

//extern void wait_before_send(int local_resource_manager_id, int to_resource_manager_id);
extern double wait_time_before_send(int local_resource_manager_id, int to_resource_manager_id);

typedef std::pair<int32_t, int64_t> IntPair;

struct IntPairHash {
	template <typename T, typename U>
	std::size_t operator()(const std::pair<T, U> &x) const {
		return std::tr1::hash<T>()(x.first) ^ std::tr1::hash<U>()(x.second);
	}
};

struct IntPairEqual {
	bool operator()(const IntPair& a, const IntPair& b) const {
		return a.first == b.first && a.second == b.second;
	}
};

//command type
//len + type + data
enum COMMAND {
	/* resource_requesrt_timestamp + tid + node_thread_listen_port + resource_num + resource_id*n. */
	RESOURCE_REQUEST,
	/* resource_request_timestamp + tid + resource_manager_id + resource_num + available_num +(resource_id+state)*n. */
	RESOURCE_REQUEST_REPLY,
	/* resource_request_timestamp + tid + node_thread_listen_port + resource_num + resource_id*n */
	RESOURCE_RELEASE,

	//REGISTER由node thread发送给resource manager，注意首次init_listen_port必须发送。
	/* leader_tid + init_time + init_listen_port + sender_tid + len + denpendency_string */
	REGISTER,

	/* (leader_tid+init_time+init_listen_port) + sender_tid + resource_id +request_time + weight */
	DEADLOCK_DETECTION_PROBE_TO_RESOURCE_MANAGER,

	/* (leader_tid+init_time+init_listen_port) + send_resource_id + weight */
	DEADLOCK_DETECTION_PROBE_TO_NODE_THREAD,

	/* (leader_tid+init_time+init_listen_port) + send_id(resource_id/-tid) + weight*/
	DEADLOCK_DETECTION_REPORT_TO_RESOURCE_MANAGER,  //用于转发，简化处理。
	/* 同上 */
	DEADLOCK_DETECTION_REPORT_TO_NODE_THREAD,
		
	/* (leader_tid+init_time+init_listen_port) + resource_manager_id */
	DEADLOCK_DETECTION_REQUEST,

	/* (leader_tid+init_time+init_listen_port) + reource_manager_id + num +(resource_id/-tid+len+condition)*n */
	DEADLOCK_DETECTION_RESPONCE, 

	/* (leader_tid+init_time+init_listen_port) + tid */
	CANCEL_LOCAL_TID,  //资源线程cancel本地node thread

	/* 未实现，假设消息肯定会到达。*/
	CANCEL_LOCAL_TID_REPLY
};

const size_t kMaxPacketLen = 255;
const size_t kHeaderLen = 4;
const size_t kCommandLen = 4;



#endif
