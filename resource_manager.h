#ifndef HAVE_RESOURCE_MANAGER_H
#define HAVE_RESOURCE_MANAGER_H

#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>

#include <set>
#include <vector>
#include <list>
#include <utility>

class MapValue;
typedef std::tr1::unordered_map<IntPair, MapValue, IntPairHash, IntPairEqual> DEPENDENCY_MAP;
typedef boost::shared_ptr<DEPENDENCY_MAP> DEPENDENCY_MAP_PTR;

typedef std::tr1::unordered_map<int, int> INT_INT_MAP;
typedef boost::shared_ptr<INT_INT_MAP> PROBE_NUM_PTR;

typedef std::tr1::unordered_map<int, ExpressionNodePtr> INT_EX_MAP;
typedef boost::shared_ptr<INT_EX_MAP> EXPRESSION_MAP_PTR;

// 最后采用向所有resource manager发送REQUEST的方法，以防遗漏。
// 每个(tid,timestamp)对应一个MapValue
class MapValue {
public:
	MapValue() : init_listen_port(0) {
		probe_num_ptr.reset(new INT_INT_MAP());
		expression_map_ptr.reset(new INT_EX_MAP());
	}

	uint16_t init_listen_port;
	PROBE_NUM_PTR probe_num_ptr;  //据此可以查看是否注册了，只用于resource manager上的resource，各个node thread自己管理自己的probe num。

	//pair中的int可以为-tid，也可以为resource_id.
	//这里使用-tid是为了防止tid与resource_id有重合。
	EXPRESSION_MAP_PTR expression_map_ptr;
};

class NODE_THREAD_ID {
public:
	NODE_THREAD_ID(
			muduo::Timestamp& time,
			int32_t tid,
			const muduo::net::InetAddress& addr1,
			const muduo::net::InetAddress& addr2,
			int id) 
		: resource_request_time(time),
		  node_thread_tid(tid),
		  node_thread_addr(addr1),
		  listen_addr(addr2),
		  resource_id(id) { }

	bool operator==(const NODE_THREAD_ID& node_thread_id)
	{
		if (resource_request_time == node_thread_id.resource_request_time &&
			node_thread_tid == node_thread_id.node_thread_tid &&
			node_thread_addr.toIpPort() == node_thread_id.node_thread_addr.toIpPort() &&
			listen_addr.toIpPort() == node_thread_id.listen_addr.toIpPort() &&
			resource_id == node_thread_id.resource_id) {
			return true;
		} else {
			return false;
		}
	}

	muduo::Timestamp resource_request_time;
	int32_t node_thread_tid;
	//注意该addr是node thread连接至resource manager所用的addr。
	muduo::net::InetAddress node_thread_addr;
	//该listen_addr是node_thread的listen addr。
	muduo::net::InetAddress listen_addr;
	int resource_id;
};

class SingleResource {
public:
	SingleResource(int resource_manager_id, int resource_id);
	
	int resource_manager_id_;
	int resource_id_;
	std::list<NODE_THREAD_ID> node_thread_wait_list_;
	boost::shared_ptr<MutexLock> list_mutex_ptr_;  //保护list,注意:为了能使类SingleResource能加入容器中，所以不能直接使用MutexLock mutex_，而是加入到智能指针中。思考：貌似这是在单线程中，也没有保护的必要阿。
};


class NodeThreadClient;
typedef boost::shared_ptr<NodeThreadClient> NodeThreadClientPtr;

class ResourceManager
{
public:
	ResourceManager(muduo::net::EventLoop* loop,
                    const muduo::net::InetAddress& listenAddr);

	void start();

private:
	void printResourceManagerHandler();
	void onConnection(const muduo::net::TcpConnectionPtr& conn);

	void onMessage(const muduo::net::TcpConnectionPtr& conn,
                   muduo::net::Buffer* buf,
				   muduo::Timestamp time);
	void doCommand(const muduo::net::TcpConnectionPtr& conn,
                   int32_t command_type,
                   Buffer* buf,
				   int len,
				   muduo::Timestamp time);
	int getResourcesFromResourceManager(
			const TcpConnectionPtr& conn,
			int* resource_ids,
			int* available_ids,
			int resource_num,
			muduo::Timestamp request_time,
			int32_t tid,
			uint16_t node_thread_listen_port);

	void releaseResourcesFromResourceManager(
			const TcpConnectionPtr& conn,
			int* resource_ids,
			int resource_num,
			muduo::Timestamp request_time,
			int32_t tid,
			uint16_t node_thread_listen_port);

	void connectNodeThreadAndSendMessage(
			InetAddress& node_thread_listen_addr,
			Buffer* buf);

	muduo::net::EventLoop* loop_;
	muduo::net::TcpServer server_;

public:
	//暂时设为public
	//这里暂时以int为conn的标志，它由message传递。
	std::map<int, NodeThreadClientPtr> node_thread_clients_ptr_map_;
	int conn_id_;  // 不断递增，用以区分不同的连接。

	//注意使用resouce_id % resource_manager_id.
	std::vector<SingleResource> resource_array_;

	//这里保存与该resource mangener在同一个站点上的thread tid
	//用于本地死锁检测时，判断某个tid是否在本地站点上。
	//port范围应该起始于: 50000(node_thread_first_port) + resource_manager_id * node_thread_num_for_each_manager -----  
	//在收到资源请求时更新该数据。
	std::map<int32_t, uint16_t> local_tid_port_;

	//依赖关系映射表，int64_t部分为Timestamp
	DEPENDENCY_MAP_PTR dependency_map_ptr_;
};

#endif
