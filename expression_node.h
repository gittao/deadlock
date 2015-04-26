/*************************************************************************
    > File Name: expression_node.h
    > Author: tao
    > Mail: tao_zhi126@126.com 
    > Created Time: 2014年12月20日 星期六 23时14分25秒
 ************************************************************************/

#ifndef HAVE_EXPRESSION_NODE_H
#define HAVE_EXPRESSION_NODE_H

#include "global.h"

//与或表达式节点
class ExpressionNode {
public:
	friend class boost::serialization::access;

	template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
        ar & type_;
        ar & resource_id_;
		ar & is_available_;
		ar & children_;
    }

	ExpressionNode() : type_(0), resource_id_(-1), is_available_(0) {
		children_.clear();
	}

	ExpressionNode(char type) : type_(type), resource_id_(-1), is_available_(0) {
		children_.clear();
	}

	ExpressionNode(int resource_id, bool is_available)
		: type_('o'),
          resource_id_(resource_id),
		  is_available_(is_available) {
			  children_.clear();
		  }

	ExpressionNode(ExpressionNode& node)
	{
		type_ = node.type_;
		resource_id_ = node.resource_id_;
		is_available_ = node.is_available_;
		
		std::vector<boost::shared_ptr<ExpressionNode> >::iterator iter;
		for (iter = node.children_.begin(); iter != node.children_.end(); ++iter) {
			children_.push_back(boost::shared_ptr<ExpressionNode>(new ExpressionNode(*(*iter))));
		}

	}

	// 以下提供一些常用方法
	bool isActive()
	{
		if (type_ == 0)
			return true;
		else return false;
	}

	bool isResourceAsTid()
	{
		if (type_ == 'o' && resource_id_ < 0)
			return true;    //说明保存的资源是个tid，以负数保存。
		else return false;
	}

	int32_t getTid()
	{
		if (isResourceAsTid())
			return static_cast<int32_t>(-resource_id_);
		else return 0;
	}

	char type_;  //表示是什么运算，有^/v/o/0，表示与/或/资源节点/空。
	int resource_id_;  //注意tid也看做resource的资源，但用-tid表示。
	bool is_available_;
	std::vector<boost::shared_ptr<ExpressionNode> > children_;
};

typedef boost::shared_ptr<ExpressionNode> ExpressionNodePtr;

ExpressionNodePtr create_expression(char type, int* resource_ids, bool* is_availables, int num);

ExpressionNodePtr join_expression(char type, ExpressionNodePtr& exp1, ExpressionNodePtr& exp2);

//返回ture表示资源是active的
//is_local为true则表示用于local reduction中将非本地资源看作是true，即可获得。
bool calculate_and_update_expression(
		ExpressionNodePtr& head,
		std::map<int, bool>& resource_available_map,
		bool is_local = false);

void expression_serialization_into_string(
		ExpressionNodePtr& head,
		std::string& str);

ExpressionNodePtr string_deserialization_into_expression(std::string& str);

//将一个关系表达式打印出来，用于调试
std::string print_expression_node_ptr(ExpressionNodePtr& head);

#endif
