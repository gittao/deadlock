/*************************************************************************
    > File Name: expression_node.cpp
    > Author: tao
    > Mail: tao_zhi126@126.com 
    > Created Time: 2014年12月20日 星期六 23时16分48秒
 ************************************************************************/

#include "global.h"
#include "expression_node.h"

ExpressionNodePtr create_expression(char type, int* resource_ids, bool* is_availables, int num)
{
	ExpressionNodePtr node(new ExpressionNode(type));

	for (int i = 0; i < num; ++i) {
		node->children_.push_back(ExpressionNodePtr(new ExpressionNode(resource_ids[i], is_availables[i])));
	}

	return node;
}

ExpressionNodePtr join_expression(char type, ExpressionNodePtr& exp1, ExpressionNodePtr& exp2)
{
	ExpressionNodePtr node(new ExpressionNode(type));
	node->children_.push_back(exp1);
	node->children_.push_back(exp2);
	return node;
}


bool calculate_and_update_expression(
		ExpressionNodePtr& head,
		std::map<int, bool>& resource_available_map,
		bool is_local)
{

	if (head->type_ == 'o') { //表示叶子节点，也就是资源节点
		if (resource_available_map.find(head->resource_id_) != resource_available_map.end() && resource_available_map[head->resource_id_] == true) {
			return true;
		} else if (is_local == true && head->resource_id_ > 0) { //对于本地规约，只考虑资源，不考虑tid
			if (head->resource_id_/resource_manager_resource_num != resource_manager_id) {
				return true;  //对于非本地资源，假设可获得
			} else {
				return false;  //对于本地资源，则不可获得
			}
		} else {
			return false;
		}
	} else {
		std::vector<bool> children_results;
		std::vector<boost::shared_ptr<ExpressionNode> >::iterator iter;
		for (iter = head->children_.begin(); iter != head->children_.end(); ) {
			bool res = calculate_and_update_expression(*iter, resource_available_map, is_local);
		
			children_results.push_back(res);

			if (res == true) {
				iter = head->children_.erase(iter);  //防止序列式迭代器失效
			} else {
				++iter;
			}
		}

		/* 计算最终的result */
		std::vector<bool>::iterator iter2;
		bool result;
		if (head->type_ == 'v') { //表示或
			result = false;
		} else {  //表示与
			result = true;
		}
		bool has_children = false;
		for (iter2 = children_results.begin(); iter2 != children_results.end(); ++iter2) {
			has_children = true;
			if (head->type_ == 'v') {
				result |= *iter2;
			} else {
				result &= *iter2;
			}
		}
		if (has_children == false)
			result = true;   //已经没有子节点了，所以一开始将result设置成false是错误的，此处做出修改。

		if (result == true) {
			head->children_.clear();
		}
		return result;
	}
}

void expression_serialization_into_string(
		ExpressionNodePtr& head,
		std::string& str)
{
	std::ostringstream os;
    boost::archive::text_oarchive oa(os);
    oa << *head;
	str = os.str();
}

ExpressionNodePtr string_deserialization_into_expression(std::string& str)
{
	std::istringstream is(str);
    boost::archive::text_iarchive ia(is);
    ExpressionNode node;
	ia >> node;
	ExpressionNodePtr head(new ExpressionNode(node));
	return head;
}

//将一个关系表达式打印出来，用于调试
std::string print_expression_node_ptr(ExpressionNodePtr& head)
{
	std::string str;
	char temp[256];
	str.append("(");
	if (head->type_ == 'o') {
		sprintf(temp, "%d", head->resource_id_);
		str.append(temp);
	} else {
		std::vector<boost::shared_ptr<ExpressionNode> >::iterator iter;
		for (iter = head->children_.begin(); iter != head->children_.end(); ++iter) {
			std::string temp_str = print_expression_node_ptr(*iter);
			str.append(temp_str);
//			if (iter != --(head->children_.end()))
				str.push_back(head->type_);
		}
	}
	str.append(")");
	return str;
}
