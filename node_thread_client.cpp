/*************************************************************************
    > File Name: node_thread_client.cpp
    > Author: tao
    > Mail: tao_zhi126@126.com 
    > Created Time: 2014年11月12日 星期三 18时55分41秒
 ************************************************************************/

#include "global.h"

#include "node_thread_client.h"
#include "resource_manager.h"

NodeThreadClient::NodeThreadClient(EventLoop* loop, 
                                   const InetAddress& listenAddr,
								   ResourceManager* rm,
								   int connId,
								   Buffer* send_buf
								   )
	: loop_(loop),
      client_(loop, listenAddr, "NodeThreadClient"),
	  rm_(rm),
	  conn_id_(connId)
{
    client_.setConnectionCallback(boost::bind(&NodeThreadClient::onConnection, this, rm_, _1));
    client_.setMessageCallback(boost::bind(&NodeThreadClient::onMessage, this, rm_, _1, _2, _3));
	if (send_buf != NULL) {
		send_buf_.swap(*send_buf);
		need_send_message_ = true;
	} else {
		need_send_message_ = false;
	}
}

void NodeThreadClient::connect()
{
	client_.connect();
}

void NodeThreadClient::onConnection(ResourceManager *rm,
                                    const TcpConnectionPtr& conn)
{
    if (conn->connected()) {
//		LOG_INFO << "Good! I'm node thread client, I'm connected.";
		if (need_send_message_)
			conn->send(&send_buf_);
		conn->shutdown();
	} else {
//		LOG_INFO << "SHIT! I'm node thread client, I'm shuting down.";


		//根据conn_id_找到对应的NodeThreadClientPTr并删除，因为之后不会再使用它发送消息了，删除之后不久对应的NodeThreadClient就会析构。
		rm->node_thread_clients_ptr_map_.erase(conn_id_);
	}
}

void NodeThreadClient::onMessage(ResourceManager* rm,
								 const TcpConnectionPtr& conn,
                                 Buffer* buf,
								 Timestamp receiveTime)
{


}
