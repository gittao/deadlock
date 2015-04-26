/*************************************************************************
    > File Name: resource_manager_client.h
    > Author: tao
    > Mail: tao_zhi126@126.com 
    > Created Time: 2014年11月10日 星期一 18时52分10秒
 ************************************************************************/
#ifndef HAVE_RESOURCE_MANAGER_CLIENT_H
#define HAVE_RESOURCE_MANAGER_CLIENT_H

#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpClient.h>

typedef struct node_thread NODE_THREAD;
typedef boost::function<void()> RESOURCE_MANAGER_CLIENT_CALLBACK;

class ResourceManagerClient : boost::noncopyable
{
public:
	ResourceManagerClient(EventLoop* loop,
                          const InetAddress& listenAddr,
						  int id,
						  NODE_THREAD* me
						  );
	void connect();
	void onConnection(const TcpConnectionPtr& conn);
	void onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp time);
private:

	void doCommand(const muduo::net::TcpConnectionPtr& conn,
                   int32_t command_type,
                   Buffer* buf,
				   int len,
				   muduo::Timestamp time);

	EventLoop* loop_;
	TcpClient client_;
	int connect_resource_manager_id_;
	NODE_THREAD* node_thread_data_;
};

#endif
