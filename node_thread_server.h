/*************************************************************************
    > File Name: node_thread_server.h
    > Author: tao
    > Mail: tao_zhi126@126.com 
    > Created Time: 2014年11月09日 星期日 00时04分24秒
 ************************************************************************/

#ifndef HAVE_NODE_THREAD_SERVER_H
#define HAVE_NODE_THREAD_SERVER_H

#include <muduo/net/TcpServer.h>

typedef struct node_thread NODE_THREAD;
class ResourceManagerClient;


//不仅监听一个端口，同时连接到所有的resource manager。
class NodeThreadServer
{
public:
	NodeThreadServer(muduo::net::EventLoop* loop,
                     const muduo::net::InetAddress& listenAddr,
					 int resource_manager_num,
					 InetAddress* server_addr,
                     NODE_THREAD* me
					 );
	~NodeThreadServer();

	void start();
	void connectResourceManagers();

private:
	void onConnection(const muduo::net::TcpConnectionPtr& conn);

	void onMessage(const muduo::net::TcpConnectionPtr& conn,
                   muduo::net::Buffer* buf,
				   muduo::Timestamp time);

	muduo::net::EventLoop* loop_;
	muduo::net::TcpServer server_;
	int resource_manager_num_;
	ResourceManagerClient** resource_manager_clients_;
	NODE_THREAD* node_thread_data_;
};

#endif 
