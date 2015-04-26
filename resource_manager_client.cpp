/*************************************************************************
    > File Name: resource_manager_client.cpp
    > Author: tao
    > Mail: tao_zhi126@126.com 
    > Created Time: 2014年11月10日 星期一 18时59分12秒
 ************************************************************************/

#include "global.h"

#include "node_thread.h"
#include "resource_manager_client.h"
#include "deadlock_detection.h"

#include <vector>
#include <algorithm>
#include <ctime>
#include <unistd.h>

void node_thread_execution_timeout_handler(NODE_THREAD* me, EventLoop* loop);
void deadlock_detection_timeout_handler(NODE_THREAD* me, Timestamp& request_time, EventLoop* loop);

void send_buf_to_conn_handler(TcpConnectionPtr& conn, boost::shared_ptr<Buffer>& buffer);

void register_dependency_at_local(int32_t leader_tid, Timestamp& init_time, uint16_t listen_port, NODE_THREAD* me);
void send_deadlock_detection_probe_to_resource_manager(int leader_tid, uint16_t listen_port, Timestamp& init_time, NODE_THREAD* me, int resource_id, double weight, EventLoop* loop);

#define AND_RESOURCE_NUM 5 //每次同时请求5个资源
static void get_random_resources_with_AND_type(NODE_THREAD* me, EventLoop* loop);
static void get_random_and_sorted_ids(int* ids, int low, int high, int num);
static int send_resource_request_to_a_resource_manager(NODE_THREAD* me,int resource_manager_id, int* resource_ids, int low, int high);

static void send_resource_release_to_resource_managers(NODE_THREAD* me);
static int send_resource_release_to_a_resource_manager(NODE_THREAD* me,int resource_manager_id, int* resource_ids, int low, int high);


void send_buf_to_conn_handler(TcpConnectionPtr& conn, boost::shared_ptr<Buffer>& buffer)
{
	conn->send(&(*buffer));
}

ResourceManagerClient::ResourceManagerClient(EventLoop* loop, 
                                             const InetAddress& listenAddr,
											 int id,
											 NODE_THREAD* me
											 )
	: loop_(loop),
      client_(loop, listenAddr, "ResourceClientClient"),
	  connect_resource_manager_id_(id),
	  node_thread_data_(me)
{
    client_.setConnectionCallback(
        boost::bind(&ResourceManagerClient::onConnection, this, _1));
    client_.setMessageCallback(
        boost::bind(&ResourceManagerClient::onMessage, this, _1, _2, _3));
}

void ResourceManagerClient::connect()
{
	client_.connect();
}

void ResourceManagerClient::onConnection(const TcpConnectionPtr& conn)
{
    if (conn->connected()) {
		node_thread_data_->connected_resource_manager_num++;
		node_thread_data_->resource_manager_connection_array[connect_resource_manager_id_] = conn;   //该conn只能被一个node_thread使用，所以无需加锁。
	//	LOG_INFO << "Connecting resource manager " << connect_resource_manager_id_;
		if (node_thread_data_->connected_resource_manager_num == resource_manager_num) {
			if (node_thread_data_->is_resource_fetch_started == false) {
				node_thread_data_->is_resource_fetch_started = true;
	//		LOG_INFO << "GET_RANDOM_RESOURCES";
		//		if (resource_manager_id == 0)
				get_random_resources_with_AND_type(node_thread_data_, loop_);
			}
			else {
				LOG_ERROR << "The resource fetching has started!!!!!";
			}
		}

	} else {
		node_thread_data_->connected_resource_manager_num--;
		node_thread_data_->resource_manager_connection_array[connect_resource_manager_id_].reset();  //变为NULL。
	}
}

void ResourceManagerClient::onMessage(const TcpConnectionPtr& conn, 
                                      Buffer* buf,
									  Timestamp time)
{
	while (buf->readableBytes() >= kHeaderLen) {

		int32_t len = buf->peekInt32();  //内部已经实现字节序转变。

		if (len > 65535 || len < 0) {
			LOG_ERROR <<"Invalid length " << len;
			break;
		} else if (buf->readableBytes() >= len + kHeaderLen) {
			buf->retrieve(kHeaderLen);
			int32_t command_type = buf->peekInt32();
			buf->retrieve(kCommandLen);
			doCommand(conn, command_type, buf, len - kCommandLen, time);
		} else {
			break;
		}
	}	
}

void ResourceManagerClient::doCommand(const TcpConnectionPtr& conn,
                                      int32_t command_type,
                                      Buffer* buf,
									  int len,
									  Timestamp time)
{
	assert(len > 0);

	switch (command_type) {
		case RESOURCE_REQUEST_REPLY:
			{
				muduo::Timestamp temp_request_time(buf->readInt64());
				int32_t temp_tid = buf->readInt32();

				assert(temp_tid == node_thread_data_->tid);

				//防止过期的资源回复；如果expression_result已经为1，就不在处理新的资源回复了。我们没有采用可以执行就释放其他资源的策略，这可能会造成容易导致怀疑发生死锁。但这不是问题，适当的增大死锁定时器超时的时间即可。
				//注意：我们总是一次性释放node thread占有的所有资源，这发生在正常执行完毕，或死锁解决算法强制释放。
				if (node_thread_data_->expression_result == 1 || !(temp_request_time == node_thread_data_->current_resource_request_timestamp)) {
					// 应当释放该资源。
					buf->retrieve(len - 8 - 4);
					break;
				}

				int from_resource_manager_id = buf->readInt32();

				int resource_num = buf->readInt32();
				int available_num = buf->readInt32();
				assert(resource_num >= available_num);
				int* resource_ids = new int[resource_num];
				int* available_ids = new int[resource_num];
				for (int i = 0; i < resource_num; ++i) {
					resource_ids[i] = buf->readInt32();
					available_ids[i] = buf->readInt32();
				//	LOG_INFO << temp_request_time.toString() << " " << resource_ids[i] << " " << available_ids[i];
				}

				// 更新map
				for (int i = 0; i < resource_num; ++i) {
					node_thread_data_->resource_available_map[resource_ids[i]] = available_ids[i];
				}
				if (node_thread_data_->head.use_count() == 0) {
					LOG_INFO << "head is NULL";
				}
				node_thread_data_->expression_result = calculate_and_update_expression(node_thread_data_->head, node_thread_data_->resource_available_map);
				if (node_thread_data_->expression_result == 1) {
					if (node_thread_data_->is_execution_started != 1)  {
						node_thread_data_->is_execution_started = 1;
						
						//如果已经启动了死锁检测超时定时器，则取消。
						if (node_thread_data_->is_deadlock_timeout_timer_started) {
							loop_->cancel(node_thread_data_->deadlock_timer_id);
							node_thread_data_-> is_deadlock_timeout_timer_started = false;
						}


						//释放资源。
						//启动定时器，定时器处理函数中，释放资源后重新发起资源获取，并修改is_resource_fetch_started/is_execution_started等参数。
						const boost::function<void()> handler = boost::bind(&node_thread_execution_timeout_handler, node_thread_data_, loop_);
						//持有资源0.5-1.5秒种
						double temp = (rand()%11 + 5) * 1.0 / 10;
						loop_->runAfter(temp, handler);
						
					}
				} else {
					if (node_thread_data_->is_deadlock_timeout_timer_started != 1 && node_thread_data_->is_deadlock_detection_started != 1) {
						node_thread_data_->is_deadlock_timeout_timer_started = 1;
						// 启动死锁检测定时器，定时器处理函数中根据当前expresion_result进一步判断，防止发生误超时；否则发起死锁检测算法。
						const boost::function<void()> handler = boost::bind(&deadlock_detection_timeout_handler, node_thread_data_, node_thread_data_->current_resource_request_timestamp, loop_);
						node_thread_data_->deadlock_timer_id = loop_->runAfter(10, handler);
						
					}

				}


				delete[] resource_ids;
				delete[] available_ids;
				break;
			}

		case DEADLOCK_DETECTION_PROBE_TO_NODE_THREAD:
			{
				int32_t leader_tid = buf->readInt32();
				muduo::Timestamp init_time(buf->readInt64());
				uint16_t listen_port = buf->readInt16();
				int resource_id = buf->readInt32();
				int64_t temp_weight = buf->readInt64();
				double weight = reinterpret_cast<double&>(temp_weight);

				//LOG_INFO << "PROBE_TO_NODE_THREAD " << leader_tid << " " << resource_id;
				// Fix me！考虑并发执行
				//if (!(init_time == node_thread_data_->deadlock_detection_initialization_timestamp))
				
				// 是否需要注册
				bool need_register = false;
				
				//准备工作
				if (node_thread_data_->register_map_ptr.use_count() == 0) {
					node_thread_data_->register_map_ptr.reset(new IntPair_Bool_Map);
				}

				IntPair_Bool_Map& temp_register_map = *(node_thread_data_->register_map_ptr);

				int64_t temp_time = init_time.microSecondsSinceEpoch();
				if (temp_register_map[std::make_pair(leader_tid, temp_time)] == 0) {
					// 需要注册
					need_register = true;
					register_dependency_at_local(leader_tid, init_time, listen_port, node_thread_data_);
					temp_register_map[std::make_pair(leader_tid, temp_time)] = 1;
				}
				
				if (node_thread_data_->tid == leader_tid || node_thread_data_->expression_result == 1 || need_register == false) {
					// 发送REPORT
					Buffer reply_buf;
					reply_buf.appendInt32(static_cast<int32_t>(DEADLOCK_DETECTION_REPORT_TO_RESOURCE_MANAGER));
					reply_buf.appendInt32(leader_tid);
					reply_buf.appendInt64(init_time.microSecondsSinceEpoch());
					reply_buf.appendInt16(listen_port);
					reply_buf.appendInt32(static_cast<int32_t>(-node_thread_data_->tid));
					reply_buf.appendInt64(reinterpret_cast<int64_t&>(weight));
					reply_buf.prependInt32(static_cast<int32_t>(reply_buf.readableBytes()));
					//向node thread发送消息
					//这里使用了一个简单的方法，我们向resource manager转发，由resource manager转发这个包，避免了开发类似于NodeThreadClient（目前该类专门用于resource manager连接node thread）的类。
					TcpConnectionPtr& conn = node_thread_data_->resource_manager_connection_array[resource_manager_id];
					conn->send(&reply_buf);

				} else {
					// 发送PROBE 
					std::map<int, bool>::iterator iter;
					std::vector<int> non_available_resrources;
					for (iter = node_thread_data_->resource_available_map.begin(); iter != node_thread_data_->resource_available_map.end(); ++iter) {
						if (iter->second == false) {
							non_available_resrources.push_back(iter->first);
						}
					}

					int non_available_resrources_num = non_available_resrources.size();

					std::vector<int>::iterator iter2;
					for (iter2 = non_available_resrources.begin(); iter2 != non_available_resrources.end(); ++iter2) {
						send_deadlock_detection_probe_to_resource_manager(leader_tid, listen_port, init_time, node_thread_data_, *iter2, weight/non_available_resrources_num, loop_);
					}
				}

				break;
			}

		case DEADLOCK_DETECTION_REPORT_TO_NODE_THREAD:
			{
				int32_t leader_tid = buf->readInt32();
				muduo::Timestamp init_time(buf->readInt64());
				uint16_t listen_port = buf->readInt16();
				int id = buf->readInt32();  //resource_i/-tid
				int64_t temp_weight = buf->readInt64();
				double weight = reinterpret_cast<double&>(temp_weight);

		//		LOG_INFO << leader_tid << " " << node_thread_data_->tid << " " << listen_port << " " << node_thread_data_->port;
				assert(leader_tid == node_thread_data_->tid && listen_port == node_thread_data_->port);
				if (!(init_time == node_thread_data_->deadlock_detection_initialization_timestamp)) {
					LOG_WARN << "Report message is out of date.";
					break;
				}

				node_thread_data_->total_weight += weight;
				if (node_thread_data_->total_weight < 1.0 + 0.0000001 && node_thread_data_->total_weight > 1.0 -0.0000001) {
					// 这里向各个resource manager发送REQUEST消息
			
					for (int i = 0; i < resource_manager_num; ++i) {
						Buffer reply_buf;
						reply_buf.appendInt32(static_cast<int32_t>(DEADLOCK_DETECTION_REQUEST));
						reply_buf.appendInt32(leader_tid);
						reply_buf.appendInt64(init_time.microSecondsSinceEpoch());
						reply_buf.appendInt16(listen_port);
						reply_buf.appendInt32(static_cast<int32_t>(i));
						reply_buf.prependInt32(static_cast<int32_t>(reply_buf.readableBytes()));

						TcpConnectionPtr& conn = node_thread_data_->resource_manager_connection_array[i];

						double wait_time = wait_time_before_send(resource_manager_id, i);
						if (wait_time > 0.000001) {

							boost::shared_ptr<Buffer> buffer(new Buffer);
							buffer->swap(reply_buf);
							const boost::function<void()> handler = boost::bind(&send_buf_to_conn_handler, conn, buffer);
							loop_->runAfter(wait_time, handler);
						} else {
							conn->send(&reply_buf);
						}
					}
				}

				break;
			}
		case DEADLOCK_DETECTION_RESPONCE:
			{
				int32_t leader_tid = buf->readInt32();
				muduo::Timestamp init_time(buf->readInt64());
				uint16_t listen_port = buf->readInt16();
				int temp_resource_manager_id = buf->readInt32();
				int num = buf->readInt32();

				assert(leader_tid == node_thread_data_->tid && listen_port == node_thread_data_->port);
				if (!(init_time == node_thread_data_->deadlock_detection_initialization_timestamp)) {
					LOG_WARN << "Report message is out of date.";
					break;
				}

				assert(node_thread_data_->resource_manager_id_arr[temp_resource_manager_id] == 0);
				node_thread_data_->resource_manager_id_arr[temp_resource_manager_id] = 1;
				node_thread_data_->num_of_responce++;

				for (int i = 0; i < num; ++i) {
					int resource_id = buf->readInt32();
					int str_len = buf->readInt32();
					muduo::string muduo_str(buf->retrieveAsString(str_len));
					std::string str(muduo_str.begin(), muduo_str.end());
					//解析str
					ExpressionNodePtr node = string_deserialization_into_expression(str);
					node_thread_data_->global_expression_map[resource_id] = node;
				}

				//处理global_expresion_map, 解决全局死锁。
				if (node_thread_data_->num_of_responce == resource_manager_num) {
					/*
					LOG_INFO << "-------------------BEGIN " << node_thread_data_->tid << " " << node_thread_data_->deadlock_detection_initialization_timestamp.microSecondsSinceEpoch();
					std::map<int, ExpressionNodePtr>::iterator iter;
					for (iter = node_thread_data_->global_expression_map.begin(); iter != node_thread_data_->global_expression_map.end(); ++iter) {
						LOG_INFO << iter->first << "=" << print_expression_node_ptr(iter->second) << "; ";
					}
					LOG_INFO << "-------------------END";
					*/
					
					// 根据global map去检测死锁。
					bool res = deadlock_detection_handle_global_map(node_thread_data_->global_expression_map);
					LOG_INFO << "Detection end: " << Timestamp::now().toString();
					if (res == true) {
						LOG_INFO << "-------------------BEGIN " << node_thread_data_->tid << " " << node_thread_data_->deadlock_detection_initialization_timestamp.microSecondsSinceEpoch();
						std::map<int, ExpressionNodePtr>::iterator iter;
						for (iter = node_thread_data_->global_expression_map.begin(); iter != node_thread_data_->global_expression_map.end(); ++iter) {
							LOG_INFO << iter->first << "=" << print_expression_node_ptr(iter->second) << "; ";
						}
						LOG_INFO << "-------------------END";

						assert(0);
					} else {
						// 未检测到死锁，做一些状态的清除.
						assert(node_thread_data_->is_deadlock_detection_started == 1 && node_thread_data_->is_deadlock_detection_leader == 1);
						node_thread_data_->total_weight = 0;
						node_thread_data_->is_deadlock_detection_started = false;
						node_thread_data_->is_deadlock_detection_leader = false;
					/*
						LOG_INFO << node_thread_data_->tid
							<< " " << node_thread_data_->expression_result
							<< " " << node_thread_data_->is_execution_started
							<< " " << node_thread_data_->is_deadlock_timeout_timer_started
							<< " " << node_thread_data_->num_of_responce
							<< " " << node_thread_data_->resource_num;
					*/

						if (node_thread_data_->expression_result == 0) {
							assert(node_thread_data_->is_execution_started == 0);
							assert(node_thread_data_->is_deadlock_timeout_timer_started == 0);
							node_thread_data_->is_deadlock_timeout_timer_started = 1;
							const boost::function<void()> handler = boost::bind(&deadlock_detection_timeout_handler, node_thread_data_, node_thread_data_->current_resource_request_timestamp, loop_);
							node_thread_data_->deadlock_timer_id = loop_->runAfter(10, handler);
						}
					}
					// 这里没有释放记录锁和mutex，fix me。
					assert(0);
				}


				break;
			}

		case CANCEL_LOCAL_TID:
			{
				int32_t leader_tid = buf->readInt32();
				muduo::Timestamp init_time(buf->readInt64());
				uint16_t listen_port = buf->readInt16();
				int temp_tid = buf->readInt32();
				assert(temp_tid == node_thread_data_->tid);
				
				//注意：该thread不可能为leader_tid
				//该线程有可能正在进行死锁检测。
				assert(leader_tid != temp_tid);		
				
				if (node_thread_data_->is_execution_started)
					break;
				if (node_thread_data_->is_deadlock_timeout_timer_started) {
					loop_->cancel(node_thread_data_->deadlock_timer_id);
					node_thread_data_->is_deadlock_timeout_timer_started = false;
				}

				//如果该线程同时也在启动死锁检测算法。
				if (node_thread_data_->is_deadlock_detection_leader == true && node_thread_data_->is_deadlock_detection_started == true) {
					node_thread_data_->is_deadlock_detection_started = false;
					node_thread_data_->is_deadlock_detection_leader = false;
					node_thread_data_->deadlock_detection_initialization_timestamp = Timestamp(0);
					node_thread_data_->total_weight = 0;
					node_thread_data_->total_weight = 0;
					node_thread_data_->global_expression_map.clear();
				}
				//修改状态, 看上去像个新的线程。
				node_thread_data_->thread_initial_timestamp = Timestamp::now();
				//释放已获得的任何资源, 并继续获取新资源。
				//Fix me！此处释放的是全部资源码？
				node_thread_execution_timeout_handler(node_thread_data_, loop_);
			}

		default:
			LOG_ERROR << "Invalid command.";
			break;
	}

}

/****************************Function*************************************/
/* 定时器处理函数，用于线程执行完一定任务后释放所获取的资源。*/
void node_thread_execution_timeout_handler(NODE_THREAD* me, EventLoop* loop)
{	
	//确保死锁检测超时定时器已经取消
	if (me->is_deadlock_detection_started) {
		loop->cancel(me->deadlock_timer_id);
		me->is_deadlock_timeout_timer_started = false;
	}
	
	//先向resource manager释放资源
	send_resource_release_to_resource_managers(me);

	LOG_INFO << "Excuted: " << me->tid 
			<< " " << me->current_resource_request_timestamp.microSecondsSinceEpoch()
			<< " --" << me->resource_ids[0] << me->resource_ids[1] << me->resource_ids[2] << me->resource_ids[3] << me->resource_ids[4] << "--";
	
	me->is_execution_started = false;
	me->resource_num = -1;
	me->resource_ids.clear();
	me->resource_available_map.clear();
	me->current_resource_request_timestamp = Timestamp(0);
	me->head.reset();
	me->expression_result = false;
	
	//这里继续用与的方式获取资源。
	//注意：此时可能死锁检测算法还在执行着......
	const boost::function<void()> handler = boost::bind(&get_random_resources_with_AND_type, me, loop);
	loop->runAfter(1, handler);
}



/******************************************************************/
/******************** 该部分用于死锁检测 **************************/
/******************************************************************/
void deadlock_detection_timeout_handler(NODE_THREAD* me, Timestamp& request_time, EventLoop* loop)
{
	//确保此时应当执行死锁检测算法。
	if (me->expression_result == true || 
			(me->expression_result == false &&
			!(request_time == me->current_resource_request_timestamp))) {
		return;
	}

	if (me->is_deadlock_detection_leader || me->is_deadlock_detection_started) {
		return;
	}

		//用于进程互斥
		struct flock lock;
		lock.l_type = F_WRLCK;
		lock.l_whence = SEEK_SET;
		lock.l_start = 0;
		lock.l_len = 0; //给整个文件上锁
		if (fcntl(fd_for_lock, F_SETLKW, &lock) != 0) {
			LOG_INFO << "fcntl false";
			return;
		}
		
		// 处理线程互斥
		if (pthread_mutex_trylock(&mutex_for_lock) != 0)
			return;

	LOG_INFO << "fcntl succeed.";
	
	LOG_INFO << "Deadlock detection begin: " << Timestamp::now().toString();

	me->is_deadlock_detection_started = true;
	me->is_deadlock_detection_leader = true;
	// 表示自己正在处理死锁检测中。
	me->is_deadlock_timeout_timer_started = false;

	me->deadlock_detection_initialization_timestamp = Timestamp::now();
	me->total_weight = 0;

	me->num_of_responce = 0;
	memset(me->resource_manager_id_arr, 0, sizeof(bool) * 100);
	me->global_expression_map.clear();

	//向各个依赖resource manager发送PROBE消息
	//不过要先注册自己的依赖关系到本地。
	//

	register_dependency_at_local(me->tid, me->deadlock_detection_initialization_timestamp, me->port, me);
	std::map<int, bool>::iterator iter;
	std::vector<int> non_available_resrources;
	for (iter = me->resource_available_map.begin(); iter != me->resource_available_map.end(); ++iter) {
		if (iter->second == false) {
			non_available_resrources.push_back(iter->first);
		}
	}

	int non_available_resrources_num = non_available_resrources.size();

	std::vector<int>::iterator iter2;
	for (iter2 = non_available_resrources.begin(); iter2 != non_available_resrources.end(); ++iter2) {
		send_deadlock_detection_probe_to_resource_manager(me->tid, me->port, me->deadlock_detection_initialization_timestamp, me, *iter2, 1.0/non_available_resrources_num, loop);
	}

}

// 由本地的resource manager来管理注册信息
void register_dependency_at_local(int32_t leader_tid, Timestamp& init_time, uint16_t listen_port, NODE_THREAD* me)
{
	Buffer buf;
	buf.appendInt32(static_cast<int32_t>(REGISTER));
	buf.appendInt32(leader_tid);
	buf.appendInt64(init_time.microSecondsSinceEpoch());
	buf.appendInt16(listen_port);

	buf.appendInt32(me->tid);

	std::string str;
	str.clear();
	expression_serialization_into_string(me->head, str);

	buf.appendInt32(static_cast<int32_t>(str.size()));
	buf.append(str);

	buf.prependInt32(static_cast<int32_t>(buf.readableBytes()));

	TcpConnectionPtr& conn = me->resource_manager_connection_array[resource_manager_id];
	conn->send(&buf);

	int64_t temp_time = init_time.microSecondsSinceEpoch();

	if (me->register_map_ptr.use_count() == 0) {
		me->register_map_ptr.reset(new std::tr1::unordered_map<IntPair, bool, IntPairHash, IntPairEqual>());
	}
	me->register_map_ptr->insert(std::make_pair(IntPair(leader_tid, temp_time), true));
	
}

void send_deadlock_detection_probe_to_resource_manager(int leader_tid, uint16_t listen_port, Timestamp& init_time, NODE_THREAD* me, int resource_id, double weight, EventLoop* loop)
{
	Buffer buf;
	buf.appendInt32(static_cast<int32_t>(DEADLOCK_DETECTION_PROBE_TO_RESOURCE_MANAGER));
	buf.appendInt32(leader_tid);
	buf.appendInt64(init_time.microSecondsSinceEpoch());
	buf.appendInt16(listen_port);
	buf.appendInt32(me->tid);
	buf.appendInt32(static_cast<int32_t>(resource_id));
	buf.appendInt64(me->current_resource_request_timestamp.microSecondsSinceEpoch());
	buf.appendInt64(reinterpret_cast<int64_t&>(weight));
	buf.prependInt32(static_cast<int32_t>(buf.readableBytes()));

	int temp_resource_manager_id = resource_id / resource_manager_resource_num;
	TcpConnectionPtr& conn = me->resource_manager_connection_array[temp_resource_manager_id];

	double wait_time = wait_time_before_send(resource_manager_id, temp_resource_manager_id);
	if (wait_time > 0.000001) {

		boost::shared_ptr<Buffer> buffer(new Buffer);
		buffer->swap(buf);
		const boost::function<void()> handler = boost::bind(&send_buf_to_conn_handler, conn, buffer);
		loop->runAfter(wait_time, handler);
	} else {
		conn->send(&buf);
	}
}

/************************************************************/
/*****************该部分函数用于获取资源*********************/
/************************************************************/

static void get_random_resources_with_AND_type(NODE_THREAD* me, EventLoop* loop)
{
	//如果当前正在执行死锁检测算法，我们暂时就不执行资源获取了。
	//1秒后再去尝试获取资源。
	if (me->is_deadlock_timeout_timer_started) {
		const boost::function<void()> handler = boost::bind(&get_random_resources_with_AND_type, me, loop);
		loop->runAfter(1, handler);
		return;
	}

	int resource_ids[AND_RESOURCE_NUM];

	// Fix me! 目前从n个resource manager均等获得资源。
	get_random_and_sorted_ids(resource_ids, 0, resource_manager_resource_num * resource_manager_num, AND_RESOURCE_NUM);

	me->resource_num = AND_RESOURCE_NUM;

	// 记录资源
	me->resource_ids.clear();
	for (int i = 0; i < AND_RESOURCE_NUM; ++i) {
		me->resource_ids.push_back(resource_ids[i]);
		//LOG_INFO << resource_ids[i];
	}

	// 更新map
	me->resource_available_map.clear();
	for (int i = 0; i < AND_RESOURCE_NUM; ++i) {
		me->resource_available_map[resource_ids[i]] = false;
	}
	// 创建资源表达式
	bool is_availables[AND_RESOURCE_NUM];
	memset(is_availables, 0, sizeof(bool) * AND_RESOURCE_NUM);
	//me->head = create_expression('^', resource_ids, is_availables, AND_RESOURCE_NUM);

	
	{
		ExpressionNodePtr child1 = create_expression('v', resource_ids, is_availables, 2); //对于AND_RESOURCE_NUM为5
		ExpressionNodePtr child2 = ExpressionNodePtr(new ExpressionNode(resource_ids[2], is_availables[2]));
		ExpressionNodePtr child3 = create_expression('v', resource_ids + 3, is_availables + 3, 2);
		me->head = ExpressionNodePtr(new ExpressionNode('^'));
		me->head->children_.push_back(child1);
		me->head->children_.push_back(child2);
		me->head->children_.push_back(child3);
	}
	

	//设置发起本次资源请求的时间。
	me->current_resource_request_timestamp = muduo::Timestamp::now();

	// 向相关resource manager发送资源请求
	
	/*********使用发送连续一段resource id给各个resource manager。
	int temp_resource_manager_id = 0;
	for (int i = 0; i < AND_RESOURCE_NUM; ++i) {
		int low = i;
		while (i < AND_RESOURCE_NUM && resource_ids[i] < (temp_resource_manager_id + 1) * resource_manager_resource_num) {
			++i;
		}

		--i;
		int high = i;
		send_resource_request_to_a_resource_manager(me, temp_resource_manager_id, resource_ids, low, high);

		temp_resource_manager_id++;
	}
	***********************************/

	/* 为了使死锁更加常见，使用如下的发送方法。*/
	int vec[AND_RESOURCE_NUM];
	for (int i = 0; i < AND_RESOURCE_NUM; ++i)
		vec[i] = resource_ids[i];
	std::random_shuffle(vec, vec + AND_RESOURCE_NUM);
	for (int i = 0; i < AND_RESOURCE_NUM; ++i) {
		int temp_resource_manager_id = vec[i] / resource_manager_resource_num;
		send_resource_request_to_a_resource_manager(me, temp_resource_manager_id, vec, i, i);
		usleep(10); //10ms, 模拟每次请求资源的先后时间延迟。
	}
}

static void get_random_and_sorted_ids(int* ids, int low, int total_num, int num)
{
	assert(num <= total_num - low);
	std::vector<int> vec;
	for (int i = low; i < total_num; ++i)
		vec.push_back(i);

	std::random_shuffle(vec.begin(), vec.end());
	std::copy(vec.begin(), vec.begin() + num, ids);
	std::sort(ids, ids + num);
}

//将闭区间[low, high]之间的资源请求发送至某个resource manager。
static int send_resource_request_to_a_resource_manager(NODE_THREAD* me, int resource_manager_id, int* resource_ids, int low, int high)
{
	Buffer buf;
	
	buf.appendInt32(static_cast<int32_t>(RESOURCE_REQUEST));  //长度为17
	
	buf.appendInt64(me->current_resource_request_timestamp.microSecondsSinceEpoch());
	buf.appendInt32(me->tid);
	buf.appendInt16(me->port);
	buf.appendInt32(high - low + 1);
	for (int i = low; i <= high; ++i) {
		buf.appendInt32(static_cast<int32_t>(resource_ids[i]));
	}
	int32_t len = static_cast<int32_t>(buf.readableBytes());
	buf.prependInt32(len);   //prepend未实现网络字节序改变

	TcpConnectionPtr& conn = me->resource_manager_connection_array[resource_manager_id];
	conn->send(&buf);
}


/************************************************************/
/*****************该部分函数释放获取的资源*******************/
/************************************************************/
static void send_resource_release_to_resource_managers(NODE_THREAD* me)
{
	int* resource_ids = new int[me->resource_num];
	for (int i = 0; i < me->resource_num; ++i) {
		resource_ids[i] = me->resource_ids[i];
	}

	int temp_resource_manager_id = 0;
	for (int i = 0; i < me->resource_num; ++i) {
		int low = i;
		while (i < me->resource_num && resource_ids[i] < (temp_resource_manager_id + 1) * resource_manager_resource_num) {
			++i;
		}

		--i;
		int high = i;
		send_resource_release_to_a_resource_manager(me, temp_resource_manager_id, resource_ids, low, high);

		temp_resource_manager_id++;
	}

	delete[] resource_ids;
}

static int send_resource_release_to_a_resource_manager(NODE_THREAD* me,int resource_manager_id, int* resource_ids, int low, int high)
{
	Buffer buf;
	
	buf.appendInt32(static_cast<int32_t>(RESOURCE_RELEASE));
	
	buf.appendInt64(me->current_resource_request_timestamp.microSecondsSinceEpoch());
	buf.appendInt32(me->tid);
	buf.appendInt16(me->port);
	buf.appendInt32(high - low + 1);
	for (int i = low; i <= high; ++i) {
		buf.appendInt32(static_cast<int32_t>(resource_ids[i]));
	}
	int32_t len = static_cast<int32_t>(buf.readableBytes());
	buf.prependInt32(len);   //prepend未实现网络字节序改变

	TcpConnectionPtr& conn = me->resource_manager_connection_array[resource_manager_id];
	conn->send(&buf);
}
