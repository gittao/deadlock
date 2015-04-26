/*************************************************************************
    > File Name: node_thread_server.cpp
    > Author: tao
    > Mail: tao_zhi126@126.com 
    > Created Time: 2014年11月09日 星期日 00时09分47秒
 ************************************************************************/

#include "global.h"

#include "node_thread_server.h"
#include "resource_manager_client.h"



NodeThreadServer::NodeThreadServer(EventLoop* loop,
                                   const InetAddress& listenAddr,
								   int resource_manager_num,
								   InetAddress* server_addr,
								   NODE_THREAD* me
								   )
  : loop_(loop),
	server_(loop, listenAddr, "NodeThreadServer"),
	resource_manager_num_(resource_manager_num),
	node_thread_data_(me)
{
	resource_manager_clients_ = new ResourceManagerClient*[resource_manager_num_];
	for (int i = 0; i < resource_manager_num_; ++i) {
		resource_manager_clients_[i] = new ResourceManagerClient(loop, server_addr[i], i, me);
	}

	server_.setConnectionCallback(
			boost::bind(&NodeThreadServer::onConnection, this, _1));
	server_.setMessageCallback(
			boost::bind(&ResourceManagerClient::onMessage, resource_manager_clients_[0], _1, _2, _3));
}

NodeThreadServer::~NodeThreadServer()
{
	if (resource_manager_clients_ != NULL) {
		for (int i = 0; i < resource_manager_num_; ++i) {
			if (resource_manager_clients_[i])
				delete resource_manager_clients_[i];
		}
		delete[] resource_manager_clients_;
	}
}

void NodeThreadServer::start()
{
	LOG_INFO << "NodeThreadServer starts.";
	server_.start();

	const boost::function<void()> handler = boost::bind(
			&NodeThreadServer::connectResourceManagers, this);
	loop_->runAfter(10, handler);
}

void NodeThreadServer::connectResourceManagers()
{	
	for (int i = 0; i < resource_manager_num_; ++i) {
		resource_manager_clients_[i]->connect();
	}
}

void NodeThreadServer::onConnection(const TcpConnectionPtr& conn)
{
//	LOG_INFO << "NodeThreadServer - " << conn->peerAddress().toIpPort()
//             << " -> "<< conn->localAddress().toIpPort() << " is "
//             << (conn->connected() ? "UP" : "DOWN");
	
	if (conn->connected()) {
//		conn->send("I'm node thread server!!!!!\n");
//
	} else {
//		LOG_INFO << "NodeThreadServer is shutting down!";
	}
}

void NodeThreadServer::onMessage(const TcpConnectionPtr& conn,
                                 Buffer* buf,
                                 Timestamp time)
{
//	string msg(buf->retrieveAllAsString());
//	LOG_INFO << conn->name() << " discards " << msg.size()
//           << " bytes received at " << time.toString();}

}

