/*************************************************************************
    > File Name: deadlock_detection.h
    > Author: tao
    > Mail: tao_zhi126@126.com 
    > Created Time: 2014年12月20日 星期六 23时28分53秒
 ************************************************************************/

#ifndef HAVE_DEADLOCK_DETECTION_H
#define HAVE_DEADLOCK_DETECTION_H

#include "expression_node.h"

// true表示有死锁存在。
// 核心函数
bool deadlock_detection_handle_global_map(std::map<int, ExpressionNodePtr>& global_expression_map);



/***************************************************************
 * 以下用于本地死锁检测
 * *************************************************************/

//cancel_tids用于返回解决本地死锁时需要cancel哪些线程
//注意：该函数不会改变local_expression_map.
bool deadlock_detection_handle_local_map(std::tr1::unordered_map<int, ExpressionNodePtr>& local_expression_map, std::map<int32_t, uint16_t>& tid_port_map, std::vector<int>* cancel_tids);

//在cancel部分线程后，更新expression_map
void deadlock_reduction_after_resolution(std::map<int, bool>& cancel_tids, std::tr1::unordered_map<int, ExpressionNodePtr>& expression_map);

#endif
