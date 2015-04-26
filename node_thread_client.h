/*************************************************************************
    > File Name: node_thread_client.h
    > Author: tao
    > Mail: tao_zhi126@126.com 
    > Created Time: 2014年11月12日 星期三 18时32分20秒
 ************************************************************************/

#ifndef HAVE_NODE_THREAD_CLIENT_H
#define HAVE_NODE_THREAD_CLIENT_H

#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpClient.h>

class ResourceManager;

//resource manager用于临时连接node thread的监听端口。
class NodeThreadClient : boost::noncopyable
{
public:
	NodeThreadClient(EventLoop* loop,
                     const InetAddress& listenAddr,
					 ResourceManager* me,
					 int connId,
					 Buffer* send_buf
					 );
	void connect();

private:
	void onConnection(ResourceManager* rm, const TcpConnectionPtr& conn);
	void onMessage(ResourceManager* rm, const TcpConnectionPtr& conn, Buffer* buf, Timestamp receiveTime);

	EventLoop* loop_;
	TcpClient client_;
	ResourceManager* rm_;   //一个Resource Manager需要可能会连接多个node thread server，会将NodeThreadClientPtr放在一个set中。该域用来获得这个set。
	int conn_id_;

	bool need_send_message_;
	Buffer send_buf_;  //与need_send_message_配合使用。
};

#endif
