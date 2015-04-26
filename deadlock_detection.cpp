/*************************************************************************
    > File Name: deadlock_detection.cpp
    > Author: tao
    > Mail: tao_zhi126@126.com 
    > Created Time: 2014年12月21日 星期日 01时04分17秒
 ************************************************************************/

#include "expression_node.h"
#include "deadlock_detection.h"

/*************************************************************
 * global deadlock detection
 *************************************************************/

static bool global_reduce_and_update_expression(ExpressionNodePtr& head, std::map<int, bool>& active_id, bool is_local = false);

// true表示有死锁存在。
static bool deadlock_reduction(std::map<int, bool>& active_id, std::map<int, ExpressionNodePtr>& expression_map, bool is_local = false, std::vector<int>* cancel_tids = NULL);


bool deadlock_detection_handle_global_map(std::map<int, ExpressionNodePtr>& global_expression_map)
{
	std::map<int, bool> active_id; //存放active的-tid/resource_id, bool=1.
	
	std::map<int, ExpressionNodePtr>::iterator iter;
	for (iter = global_expression_map.begin(); iter != global_expression_map.end();) {
		if (iter->second->isActive()) {
			active_id[iter->first] = true;	
			global_expression_map.erase(iter++);
		}else {
			++iter;
		}
	}
	return deadlock_reduction(active_id, global_expression_map);
}



// true表示有死锁存在。
static bool deadlock_reduction(std::map<int, bool>& active_id, std::map<int, ExpressionNodePtr>& global_expression_map, bool is_local, std::vector<int>* cancel_tids)
{
	if (global_expression_map.empty())
		return false;

	bool change = true;
	while(change == true) {
		change = false;
		std::map<int, ExpressionNodePtr>::iterator iter;
		for (iter = global_expression_map.begin(); iter != global_expression_map.end();) {
			//进行归约。
			bool res = global_reduce_and_update_expression(iter->second, active_id, is_local);

			if (res == true) {
				change = true;
				active_id[iter->first] = true;
				global_expression_map.erase(iter++);
			} else {
				++iter;
			}

		}
	}

	if (global_expression_map.empty()) {
		return false;
	} else {
		//获得一个列表，这个列表记录着所有需要cancel的线程
		std::map<int, ExpressionNodePtr>::iterator iter;
		for (iter = global_expression_map.begin(); iter != global_expression_map.end(); ++iter) {
			if (iter->first < 0) {
				//这是一个tid
				break;
			}

		}
		if (iter != global_expression_map.end()) {
			// 处理这个tid
			if (cancel_tids != NULL) {
				cancel_tids->push_back(iter->first);
				active_id[iter->first] = true;
				global_expression_map.erase(iter++);
				deadlock_reduction(active_id, global_expression_map, is_local, cancel_tids);
			}
		}

		return true;
	}
}

//直接使用calculate_and_update_expression的实现。
static bool global_reduce_and_update_expression(ExpressionNodePtr& head, std::map<int, bool>& active_id, bool is_local)
{
	return calculate_and_update_expression(head, active_id, is_local);
}


/*****************************************************************
 ******** local deadlock detection *******************************
 *****************************************************************/
//查询local_tid_port_
static bool isLocalNodeThread(int32_t tid, std::map<int32_t, uint16_t>& tid_port_map);


//注意在local detection时，必须先将expression拷贝过来，再进行处理。防止对原数据造成更改。
bool deadlock_detection_handle_local_map(
		std::tr1::unordered_map<int, ExpressionNodePtr>& local_expression_map,
		std::map<int32_t, uint16_t>& tid_port_map,
		std::vector<int>* cancel_tids)  //用于保存作为victim的tids
{
	std::tr1::unordered_map<int, ExpressionNodePtr>::iterator iter1;
	
	//先拷贝local_expression_map
	std::map<int, ExpressionNodePtr> temp_expression_map;
	for (iter1 = local_expression_map.begin(); iter1 != local_expression_map.end(); ++iter1) {
		ExpressionNodePtr node(new ExpressionNode(*(iter1->second)));
		temp_expression_map[iter1->first] = node;
	}


	std::map<int, bool> active_id; //存放active的-tid/resource_id, bool=1.
	
	std::map<int, ExpressionNodePtr>::iterator iter;
	for (iter = temp_expression_map.begin(); iter != temp_expression_map.end();) {
		// 将非本地的tid看作是true
		// 如果这个节点时resource->-tid，则可以直接判断这个tid是否为本地；
		// 否则对于tid->多个resource，需要在归约中查看是否是本地资源。
		if (iter->second->isActive() || (iter->second->isResourceAsTid() && !isLocalNodeThread(iter->second->getTid(), tid_port_map))) {
			active_id[iter->first] = true;	
			temp_expression_map.erase(iter++);
		} else {
			++iter;
		}
	}

		//For debug.
		/************
		LOG_INFO << "-------------------BEGIN temp_expression_map";
				
		for (iter = temp_expression_map.begin(); iter != temp_expression_map.end(); ++iter) {
			LOG_INFO << iter->first << "=" << print_expression_node_ptr(iter->second) << "; ";
		}
		LOG_INFO << "END----------------------";
		************/
	
	//deadlock_reduction同时可用于本地死锁检测，不过需要先对temp_expression_map处理tid为非本地的。
	//参数true表示本地归约。
	bool res = deadlock_reduction(active_id, temp_expression_map, true, cancel_tids);

		//For debug.
		/************
		LOG_INFO << "-------------------BEGIN temp_expression_map";
				
		for (iter = temp_expression_map.begin(); iter != temp_expression_map.end(); ++iter) {
			LOG_INFO << iter->first << "=" << print_expression_node_ptr(iter->second) << "; ";
		}
		LOG_INFO << "END----------------------";
		************/
	return res;
}

static void deadlock_reduction_after_resolution2(std::map<int, bool>& active_id, std::tr1::unordered_map<int, ExpressionNodePtr>& expression_map)
{
	if (expression_map.empty())
		return;

	bool change = true;
	while(change == true) {
		change = false;
		std::tr1::unordered_map<int, ExpressionNodePtr>::iterator iter;
		for (iter = expression_map.begin(); iter != expression_map.end();) {
			//进行归约。
			bool res = global_reduce_and_update_expression(iter->second, active_id);

			if (res == true) {
				change = true;
				active_id[iter->first] = true;
				expression_map.erase(iter++);
			} else {
				++iter;
			}

		}
	}
	//将active_id放回expression_map
	std::map<int, bool>::iterator map_iter;
	for (map_iter = active_id.begin(); map_iter != active_id.end(); ++map_iter) {
		ExpressionNodePtr node(new ExpressionNode());
		expression_map[map_iter->first] = node;   //为空
	}

//	if (expression_map.empty()) {
//		return false;
//	} else {
//		return true;
//	}
}

void deadlock_reduction_after_resolution(std::map<int, bool>& cancel_tids_map, std::tr1::unordered_map<int, ExpressionNodePtr>& expression_map)
{
	std::map<int, bool> active_id; //存放active的-tid/resource_id, bool=1.

	std::tr1::unordered_map<int, ExpressionNodePtr>::iterator iter;
	for (iter = expression_map.begin(); iter != expression_map.end();) {
		if (cancel_tids_map.find(iter->first) != cancel_tids_map.end()) {
			active_id[iter->first] = true;
			expression_map.erase(iter++);
		}else {
			++iter;
		}
	}
	deadlock_reduction_after_resolution2(active_id, expression_map);
}

bool isLocalNodeThread(int32_t tid, std::map<int32_t, uint16_t>& tid_port_map)
{
	std::map<int32_t, uint16_t>::iterator iter;
	iter = tid_port_map.find(tid);
	if (iter != tid_port_map.end()) {
		uint16_t port = iter->second;
		if (port >= node_thread_first_port + resource_manager_id * node_thread_num_for_each_manager && port < node_thread_first_port + (resource_manager_id + 1) * node_thread_num_for_each_manager) {
			return true;
		} else {
			return false;
		}
	} else {
		return false;
	}
}

