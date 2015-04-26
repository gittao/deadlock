#include "global.h"

#include "deadlock_detection.h"
#include "node_thread_client.h"
#include "resource_manager.h"

void send_conn_handler(NodeThreadClientPtr& client);


SingleResource::SingleResource(int resource_manager_id, int resource_id)
	: resource_manager_id_(resource_manager_id),
	  resource_id_(resource_id),
	  list_mutex_ptr_(new MutexLock())
{
	node_thread_wait_list_.clear();
}

ResourceManager::ResourceManager(EventLoop* loop,
                                 const InetAddress& listenAddr)
  : loop_(loop),
	server_(loop, listenAddr, "ResourceManager"),
	conn_id_(0)
{
	server_.setConnectionCallback(
			boost::bind(&ResourceManager::onConnection, this, _1));
	server_.setMessageCallback(
			boost::bind(&ResourceManager::onMessage, this, _1, _2, _3));

	node_thread_clients_ptr_map_.clear();
	dependency_map_ptr_.reset(new DEPENDENCY_MAP);

	for (int i = 0; i < resource_manager_resource_num; ++i) {
		resource_array_.push_back(SingleResource(resource_manager_id, resource_manager_id * resource_manager_resource_num + i));

		LOG_INFO << "resource " << i + resource_manager_id * resource_manager_resource_num;
	}

	//每隔20秒打印一次resource manager管理的资源的内容。
	const boost::function<void()> handler = boost::bind(&ResourceManager::printResourceManagerHandler, this);
	loop_->runAfter(20, handler);
}

void ResourceManager::start()
{
	server_.start();
}

void ResourceManager::printResourceManagerHandler()
{

	LOG_INFO << resource_manager_id;
	for (int i = 0; i < resource_array_.size(); ++i) {
			char log[1024];
			char* ptr = log;
			int res;
			res = sprintf(ptr, "%d ", resource_array_[i].resource_id_);
			ptr += res;

			std::list<NODE_THREAD_ID>::iterator iter;
			for (iter = resource_array_[i].node_thread_wait_list_.begin(); iter != resource_array_[i].node_thread_wait_list_.end(); ++iter) {
				res = sprintf(ptr, "(%d,%lld)<--", iter->node_thread_tid, iter->resource_request_time.microSecondsSinceEpoch());
				ptr += res;
			}
			LOG_INFO << log;
	}
	const boost::function<void()> handler = boost::bind(&ResourceManager::printResourceManagerHandler, this);
	loop_->runAfter(20, handler);
}

void ResourceManager::onConnection(const TcpConnectionPtr& conn)
{
//	LOG_INFO << "ResourceManager - " << conn->peerAddress().toIpPort()
//             << " -> "<< conn->localAddress().toIpPort() << " is "
//             << (conn->connected() ? "UP" : "DOWN");
	if (conn->connected()) {

	//	conn->shutdown();

	}
}

void ResourceManager::onMessage(const TcpConnectionPtr& conn,
                                Buffer* buf,
                                Timestamp time)
{

/******************************************
	

	//这里做了个简单的测试，如果client是局部变量，退出该作用域时就析构了。那样就会connect失败的，正确的做法是将分配一个client，并保存起来。参考multiplexer/demuxer.cc。
//	InetAddress serveraddr(50000);
//	NodeThreadClient* client = new NodeThreadClient(loop_, serveraddr);
//	client->connect();


	//方法2：
	InetAddress serveraddr(50000);
	NodeThreadClientPtr clientPtr;
	int connId = ++conn_id_;  //此处connId对于同一个Resource manager应当是唯一的。
	clientPtr.reset(new NodeThreadClient(loop_, serveraddr, this, connId)); //1为conn ID，是这个连接的标志。此处暂时使用整数
	//clientPtr加入集合中，防止还没有发送数据，该连接就已经析构。
	//clientPtr何时从集合中删除呢？见NodeThreadClient::onMessage。
	node_thread_clients_ptr_map_[connId] = clientPtr;
	clientPtr->connect();	

**********************************************/


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

void ResourceManager::doCommand(const TcpConnectionPtr& conn,
                                int32_t command_type,
                                Buffer* buf,
								int len,
                                Timestamp time)
{
	assert(len > 0);

	switch (command_type) {
		case RESOURCE_REQUEST:
			{
				muduo::Timestamp request_time(buf->readInt64());
				int32_t node_thread_tid = buf->readInt32();
				uint16_t node_thread_listen_port = buf->readInt16();
				int resource_num = buf->readInt32();
				int* resource_ids = new int[resource_num];
				int* available_ids = new int[resource_num];
				
				//将tid/port保存到local_tid_port_中
				local_tid_port_[node_thread_tid] = node_thread_listen_port;
				
				for (int i = 0; i < resource_num; ++i) {
					available_ids[i] = 0;
					resource_ids[i] = buf->readInt32();  //起到buf->retrieve(len)的作用。
					
				}

				int available = getResourcesFromResourceManager(
								conn,
								resource_ids,
								available_ids,
								resource_num,
								request_time,
								node_thread_tid,
								node_thread_listen_port);

				Buffer reply_buf;
				reply_buf.appendInt32(static_cast<int32_t>(RESOURCE_REQUEST_REPLY));
				reply_buf.appendInt64(request_time.microSecondsSinceEpoch());
				reply_buf.appendInt32(node_thread_tid);

				reply_buf.appendInt32(static_cast<int32_t>(resource_manager_id));
				reply_buf.appendInt32(static_cast<int32_t>(resource_num));
				reply_buf.appendInt32(static_cast<int32_t>(available));
				
				for (int i = 0; i < resource_num; ++i) {
					reply_buf.appendInt32(static_cast<int32_t>(resource_ids[i]));
					reply_buf.appendInt32(static_cast<int32_t>(available_ids[i]));
				}
				reply_buf.prependInt32(static_cast<int32_t>(reply_buf.readableBytes()));
				conn->send(&reply_buf);

				delete[] resource_ids;
				delete[] available_ids;
				break;
			}

		case RESOURCE_RELEASE:
			{
				muduo::Timestamp request_time(buf->readInt64());
				int32_t node_thread_tid = buf->readInt32();
				uint16_t node_thread_listen_port = buf->readInt16();
				int resource_num = buf->readInt32();
				int* resource_ids = new int[resource_num];
		
				for (int i = 0; i < resource_num; ++i) {
					resource_ids[i] = buf->readInt32();
				}

				releaseResourcesFromResourceManager(
								conn,
								resource_ids,
								resource_num,
								request_time,
								node_thread_tid,
								node_thread_listen_port);
				
				delete[] resource_ids;
				break;
			}

		case REGISTER:
			{
				//LOG_INFO << "REGISTER";
				int32_t leader_tid = buf->readInt32();
				muduo::Timestamp init_time(buf->readInt64());
				uint16_t listen_port = buf->readInt16();
				int32_t sender_tid = buf->readInt32();
				int str_len = buf->readInt32();

				muduo::string muduo_str(buf->retrieveAsString(str_len));
				std::string str(muduo_str.begin(), muduo_str.end());
				//解析str
				ExpressionNodePtr node = string_deserialization_into_expression(str);

				int64_t temp_time = init_time.microSecondsSinceEpoch();
				
				// 对映射表的更新
				DEPENDENCY_MAP& temp_dependency_map = *dependency_map_ptr_;
				
				if (listen_port > 0) { //说明为有效的值
					temp_dependency_map[std::make_pair(leader_tid, temp_time)].init_listen_port = listen_port;
				}

				if (temp_dependency_map[std::make_pair(leader_tid, temp_time)].expression_map_ptr.use_count() == 0) {
					temp_dependency_map[std::make_pair(leader_tid, temp_time)].expression_map_ptr.reset(new INT_EX_MAP());
				}

				INT_EX_MAP& temp_int_ex_map = *(temp_dependency_map[std::make_pair(leader_tid, temp_time)].expression_map_ptr);
				temp_int_ex_map[-sender_tid] = node; //注意使用负数

				//LOG_INFO << "REGISTER_END";
				break;
			}

		case DEADLOCK_DETECTION_PROBE_TO_RESOURCE_MANAGER:
			{
				//LOG_INFO << "DEADLOCK_DETECTION_PROBE_TO_RESOURCE_MANAGER";
				int32_t leader_tid = buf->readInt32();
				muduo::Timestamp init_time(buf->readInt64());
				uint16_t listen_port = buf->readInt16();
				int32_t sender_tid = buf->readInt32();
				int resource_id = buf->readInt32();
				muduo::Timestamp resource_request_time(buf->readInt64());
				int64_t temp_weight = buf->readInt64();
				double weight = reinterpret_cast<double&>(temp_weight);
				
				//映射表准备
				int64_t temp_time = init_time.microSecondsSinceEpoch();
				DEPENDENCY_MAP& temp_dependency_map = *dependency_map_ptr_;
				if (temp_dependency_map[std::make_pair(leader_tid, temp_time)].probe_num_ptr.use_count() == 0) {
					temp_dependency_map[std::make_pair(leader_tid, temp_time)].probe_num_ptr.reset(new INT_INT_MAP());
				}
				if (temp_dependency_map[std::make_pair(leader_tid, temp_time)].expression_map_ptr.use_count() == 0) {
					temp_dependency_map[std::make_pair(leader_tid, temp_time)].expression_map_ptr.reset(new INT_EX_MAP());
				}

				MapValue& temp_map_value = temp_dependency_map[std::make_pair(leader_tid, temp_time)];
				INT_INT_MAP& temp_probe_num_map = *(temp_map_value.probe_num_ptr);
				INT_EX_MAP& temp_int_ex_map = *(temp_map_value.expression_map_ptr);

				//resource manager对PROBE的处理。
				SingleResource& sr = resource_array_[resource_id % resource_manager_resource_num];

				int probe_num = ++temp_probe_num_map[resource_id];
				assert(probe_num >= 1);			

				//检查当前请求是否在队列中，它可以在队列中/队首/不在队列中。
				//注意：一个线程可能已经释放了原来的请求，并
				bool is_request_in_list = false;
				bool is_tid_in_list = false;
				bool is_other_request_list_head = false;
				std::list<NODE_THREAD_ID>::iterator iter;
				for (iter = sr.node_thread_wait_list_.begin(); iter != sr.node_thread_wait_list_.end(); ++iter) {
					if (iter->node_thread_tid == sender_tid) {
						is_tid_in_list = true;
						if (iter->resource_request_time == resource_request_time && iter->resource_id == resource_id) {
							is_request_in_list = true;
						}
					}
				}
				if (!sr.node_thread_wait_list_.empty()) {
					iter = sr.node_thread_wait_list_.begin();
					if (iter->node_thread_tid != sender_tid)
						is_other_request_list_head = true;
				}

				
				// need register(自身注册，不同于node thread发送REGISTER)。
				if (probe_num == 1) {
					ExpressionNodePtr node(new ExpressionNode);
					if (!sr.node_thread_wait_list_.empty() && is_request_in_list && is_other_request_list_head) {
						// 因为我们采取的策略是：node thread发现自己正在运行死锁检测算法，就在释放自身的资源（可能）后不再获取资源。（为了简单）
						node.reset(new ExpressionNode(-(sr.node_thread_wait_list_.front().node_thread_tid), 0));
					}
					temp_int_ex_map[resource_id] = node;		
				}

				if (is_other_request_list_head && is_request_in_list && probe_num == 1) {
					//发送PROBE消息
					Buffer reply_buf;
					reply_buf.appendInt32(static_cast<int32_t>(DEADLOCK_DETECTION_PROBE_TO_NODE_THREAD));
					reply_buf.appendInt32(leader_tid);
					reply_buf.appendInt64(init_time.microSecondsSinceEpoch());
					reply_buf.appendInt16(listen_port);
					reply_buf.appendInt32(static_cast<int32_t>(resource_id));
					reply_buf.appendInt64(reinterpret_cast<int64_t&>(weight));
					reply_buf.prependInt32(static_cast<int32_t>(reply_buf.readableBytes()));

					connectNodeThreadAndSendMessage(sr.node_thread_wait_list_.front().listen_addr, &reply_buf);
				} else {
					//这时向init发送REPORT消息
					Buffer reply_buf;
					reply_buf.appendInt32(static_cast<int32_t>(DEADLOCK_DETECTION_REPORT_TO_NODE_THREAD));
					reply_buf.appendInt32(leader_tid);
					reply_buf.appendInt64(init_time.microSecondsSinceEpoch());
					reply_buf.appendInt16(listen_port);
					reply_buf.appendInt32(static_cast<int32_t>(resource_id));
					reply_buf.appendInt64(reinterpret_cast<int64_t&>(weight));
					reply_buf.prependInt32(static_cast<int32_t>(reply_buf.readableBytes()));

					InetAddress listen_addr(listen_port);
					connectNodeThreadAndSendMessage(listen_addr, &reply_buf);
				}
								
				break;
			}

		case DEADLOCK_DETECTION_REPORT_TO_RESOURCE_MANAGER:
			{
				int32_t leader_tid = buf->readInt32();
				muduo::Timestamp init_time(buf->readInt64());
				uint16_t listen_port = buf->readInt16();
				int id = buf->readInt32();
				int64_t temp_weight = buf->readInt64();
				double weight = reinterpret_cast<double&>(temp_weight);

				// 转发REPORT
				Buffer reply_buf;
				reply_buf.appendInt32(static_cast<int32_t>(DEADLOCK_DETECTION_REPORT_TO_NODE_THREAD));
				reply_buf.appendInt32(leader_tid);
				reply_buf.appendInt64(init_time.microSecondsSinceEpoch());
				reply_buf.appendInt16(listen_port);
				reply_buf.appendInt32(static_cast<int32_t>(id));
				reply_buf.appendInt64(reinterpret_cast<int64_t&>(weight));
				reply_buf.prependInt32(static_cast<int32_t>(reply_buf.readableBytes()));

				InetAddress listen_addr(listen_port);
				connectNodeThreadAndSendMessage(listen_addr, &reply_buf);
				break;
			}

		case DEADLOCK_DETECTION_REQUEST:
			{
				int32_t leader_tid = buf->readInt32();
				muduo::Timestamp init_time(buf->readInt64());
				uint16_t listen_port = buf->readInt16();
				int32_t temp_resource_manager_id = buf->readInt32();
				assert(temp_resource_manager_id == resource_manager_id);

				//发送RESPONCE
				
				//映射表准备
				int64_t temp_time = init_time.microSecondsSinceEpoch();
				DEPENDENCY_MAP& temp_dependency_map = *dependency_map_ptr_;
				if (temp_dependency_map[std::make_pair(leader_tid, temp_time)].expression_map_ptr.use_count() == 0) {
					temp_dependency_map[std::make_pair(leader_tid, temp_time)].expression_map_ptr.reset(new INT_EX_MAP());
				}

				MapValue& temp_map_value = temp_dependency_map[std::make_pair(leader_tid, temp_time)];
				INT_EX_MAP& temp_int_ex_map = *(temp_map_value.expression_map_ptr);	
				INT_EX_MAP::iterator iter;

				//For debug.
			//	/************
				LOG_INFO << "-------------------BEGIN " << leader_tid << " " << temp_time << "; resource_manager_id=" << resource_manager_id;
				
				for (iter = temp_int_ex_map.begin(); iter != temp_int_ex_map.end(); ++iter) {
					LOG_INFO << iter->first << "=" << print_expression_node_ptr(iter->second) << "; ";
				}
				LOG_INFO << "END----------------------";
			//	************/
				
				//发送reply之前，先进行本地死锁检测
				std::vector<int> cancel_tids;
				LOG_INFO << "Local detection begin: " << Timestamp::now().toString();
				bool res = deadlock_detection_handle_local_map(temp_int_ex_map, local_tid_port_, &cancel_tids);
				if (res == true) {
					
					LOG_INFO << leader_tid << "HHHHHHHHH";
					std::map<int32_t, uint16_t>::iterator iter2;
					for (iter2 = local_tid_port_.begin(); iter2 != local_tid_port_.end(); ++iter2) {
						LOG_INFO << iter2->first << " " << iter2->second;
					}

					std::map<int, bool> cancel_tids_map;
					std::vector<int>::iterator cancel_tids_iter;
					for (cancel_tids_iter = cancel_tids.begin(); cancel_tids_iter != cancel_tids.end(); ++cancel_tids_iter) {
						//除了leader_tid，其他线程直接cancel掉
						if (*cancel_tids_iter != -leader_tid) {
							LOG_INFO << "SSSSSSSS" << *cancel_tids_iter;
							cancel_tids_map[*cancel_tids_iter] = true;

							Buffer reply_buf;
							reply_buf.appendInt32(static_cast<int32_t>(CANCEL_LOCAL_TID));
							reply_buf.appendInt32(leader_tid);
							reply_buf.appendInt64(init_time.microSecondsSinceEpoch());
							reply_buf.appendInt16(listen_port);
							reply_buf.appendInt32(-(*cancel_tids_iter));

							reply_buf.prependInt32(static_cast<int32_t>(reply_buf.readableBytes()));

							//*cancel_tids_iter为负值
							InetAddress listen_addr(local_tid_port_[-(*cancel_tids_iter)]);
							connectNodeThreadAndSendMessage(listen_addr, &reply_buf);
						}
					}
					LOG_INFO << "Local detection/resolution end: " << Timestamp::now().toString();

					//规约依赖关系，之后发送出去。
					deadlock_reduction_after_resolution(cancel_tids_map, temp_int_ex_map);
					

					//For debug.
			//		/************
					LOG_INFO << "-------------------BEGIN " << leader_tid << " " << temp_time << "; resource_manager_id=" << resource_manager_id;
				
					for (iter = temp_int_ex_map.begin(); iter != temp_int_ex_map.end(); ++iter) {
						LOG_INFO << iter->first << "=" << print_expression_node_ptr(iter->second) << "; ";
					}
					LOG_INFO << "END----------------------";
				//	************/
			
				} else {
			//		LOG_INFO << "BBBBBBBBBBBBBBBBBBBBBBBBBBB";
					LOG_INFO << "Local detection end: " << Timestamp::now().toString();
				}


				//开始发送			
				Buffer reply_buf;
				reply_buf.appendInt32(static_cast<int32_t>(DEADLOCK_DETECTION_RESPONCE));
				reply_buf.appendInt32(leader_tid);
				reply_buf.appendInt64(init_time.microSecondsSinceEpoch());
				reply_buf.appendInt16(listen_port);
				reply_buf.appendInt32(resource_manager_id);

				reply_buf.appendInt32(temp_int_ex_map.size());
				for (iter = temp_int_ex_map.begin(); iter != temp_int_ex_map.end(); ++iter) {
					reply_buf.appendInt32(iter->first);
					std::string str;
					str.clear();
					expression_serialization_into_string(iter->second, str);

					reply_buf.appendInt32(static_cast<int32_t>(str.size()));
					reply_buf.append(str);
				}

				reply_buf.prependInt32(static_cast<int32_t>(reply_buf.readableBytes()));

				InetAddress listen_addr(listen_port);
				connectNodeThreadAndSendMessage(listen_addr, &reply_buf);
					
				break;
			}

		default:
			LOG_ERROR << "Invalid command.";
			break;
	}

}

int ResourceManager::getResourcesFromResourceManager(
		const TcpConnectionPtr& conn,
		int* resource_ids,
        int* available_ids,
		int resource_num,
		muduo::Timestamp request_time,
		int32_t tid,
		uint16_t node_thread_listen_port)
{
	int available_resources = 0;

	for (int i = 0; i < resource_num; ++i) {
		int resource_id = resource_ids[i];
		{
			SingleResource& res = resource_array_[resource_id % resource_manager_resource_num];

			MutexLockGuard lock(*(res.list_mutex_ptr_));
			if (res.node_thread_wait_list_.empty()) {
				available_resources++;
				available_ids[i] = 1;	
			}
			res.node_thread_wait_list_.push_back(
					NODE_THREAD_ID(
						request_time,
						tid,
						conn->peerAddress(),
						InetAddress(node_thread_listen_port),
						resource_id)
					);
		}

	}
	return available_resources;
}

void ResourceManager::releaseResourcesFromResourceManager(
		const TcpConnectionPtr& conn,
        int* resource_ids,
		int resource_num,
		muduo::Timestamp request_time,
		int32_t tid,
		uint16_t node_thread_listen_port)
{
	std::vector<NODE_THREAD_ID> node_thread_ids_which_get_resources;

	for (int i = 0; i < resource_num; ++i) {
		bool found = false;
		SingleResource& res = resource_array_[resource_ids[i] % resource_manager_resource_num];
		NODE_THREAD_ID node_thread_id(request_time, tid, conn->peerAddress(), InetAddress(node_thread_listen_port), resource_ids[i]);
		std::list<NODE_THREAD_ID>::iterator iter;
		for (iter = res.node_thread_wait_list_.begin();
			 iter != res.node_thread_wait_list_.end();
			 ) {
			if (*iter == node_thread_id) {
				found = true;
				if (iter == res.node_thread_wait_list_.begin()) {
					iter = res.node_thread_wait_list_.erase(iter);
					if (iter != res.node_thread_wait_list_.end()) {
						node_thread_ids_which_get_resources.push_back(*iter);
					}
				} else {  //直接删除资源
					iter = res.node_thread_wait_list_.erase(iter);
				}
			} else {
				++iter;
			}
		}
		assert(found == true);
	}

	//处理node_thread_ids_which_get_resources，它们是由于在释放一些资源后，新的node thread获取了资源。
	//为了简便，我们为每个资源都发送一条消息，而没有整合发送至同一个node thread的资源。因为在node thread很多时，此处获取的资源不大可能是一个node thread的。
	if (!node_thread_ids_which_get_resources.empty()) {
		std::vector<NODE_THREAD_ID>::iterator iter_vec;
		for (iter_vec = node_thread_ids_which_get_resources.begin();
			iter_vec != node_thread_ids_which_get_resources.end();
			++iter_vec) {
			Buffer reply_buf;
			reply_buf.appendInt32(static_cast<int32_t>(RESOURCE_REQUEST_REPLY));
			reply_buf.appendInt64(iter_vec->resource_request_time.microSecondsSinceEpoch());
			reply_buf.appendInt32(iter_vec->node_thread_tid);
			reply_buf.appendInt32(static_cast<int32_t>(resource_manager_id));
			reply_buf.appendInt32(1); //resource_num
			reply_buf.appendInt32(1); //available_num

			reply_buf.appendInt32(static_cast<int32_t>(iter_vec->resource_id));
			reply_buf.appendInt32(1); //state，1表示available

			reply_buf.prependInt32(static_cast<int32_t>(reply_buf.readableBytes()));
			
			//连接相应的node thread，并发送消息。注意reply_buf在经过connectNodeThreadAndSendMessage调用后将不能再使用，其中的数据被转移。
			//注意：这里使用的addr是node thread的listen addr，需要从resource_id计算出来。
			connectNodeThreadAndSendMessage(iter_vec->listen_addr, &reply_buf);
		}
	}
	
}

void ResourceManager::connectNodeThreadAndSendMessage(
		InetAddress& node_thread_listen_addr,
		Buffer* buf)
{
	NodeThreadClientPtr clientPtr;
	int connId = ++conn_id_;  //此处connId对于同一个Resource manager应当是唯一的。

	clientPtr.reset(new NodeThreadClient(loop_, node_thread_listen_addr, this, connId, buf)); //connId是这个连接的标志。此处暂时使用整数
	//clientPtr加入集合中，防止还没有发送数据，该连接就已经析构。
	//clientPtr何时从集合中删除呢？见NodeThreadClient::onMessage。
	node_thread_clients_ptr_map_[connId] = clientPtr;

	uint16_t to_port = node_thread_listen_addr.toPort();
	int to_resource_manager_id = (to_port - node_thread_first_port) % node_thread_num_for_each_manager;

	
	double wait_time = wait_time_before_send(resource_manager_id, to_resource_manager_id);
	if (wait_time > 0.000001) {


		const boost::function<void()> handler = boost::bind(&send_conn_handler, clientPtr);
		loop_->runAfter(wait_time, handler);
	} else {
		clientPtr->connect();
	}

}

void send_conn_handler(NodeThreadClientPtr& client)
{
	client->connect();
}
