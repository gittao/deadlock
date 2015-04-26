#include "global.h"

#include "resource_manager.h"
#include "node_thread.h"

//一个简单的实现，不是高效，处理的配置文件也是相当简单。
int read_config_file()
{
	using namespace std;
	ifstream fin("config.cfg");
	std::string temp;
	std::string str;
	int para;
	while (getline(fin, temp)) {
		if (temp.empty() || temp[0] == '#') { //对于空行和注释行
			continue;
		}
		
		stringstream tempstream(temp);
		tempstream >> str >> para;
		if (str == "ResourceManagerNum") {
			resource_manager_num = para;
		} else if (str == "ResourceManagerFirstPort") {
			resource_manager_first_port = static_cast<uint16_t>(para);
		} else if (str == "ResourceManagerResourceNum") {
			resource_manager_resource_num = para;
		} else if (str == "NodeThreadNumForEachManager") {
			node_thread_num_for_each_manager = para;
		} else if (str == "NodeThreadFirstPort") {
			node_thread_first_port = static_cast<uint16_t>(para);
		} else {
			cout << "The config file has errors." << endl;
		}
	}
	fin.close();
}

int main(int argc, char* argv[])
{
	if (argc  < 2)
		return -1;

	resource_manager_id = atoi(argv[1]);

	LOG_INFO << "pid = " << getpid();

	int temp_time = std::time(0);
	std::srand(temp_time);
	
	if (resource_manager_id == 0)
		unlink("./file_for_lock");  //防止文件锁还没有解除
	fd_for_lock = open("./file_for_lock", O_RDWR | O_CREAT);
	if (fd_for_lock == -1)
		LOG_ERROR << "can not open the file for lock.";
	
	//读取配置文件
	read_config_file();
	if (resource_manager_id >= resource_manager_num)
		return -2;
	
	//创建线程池，每个线程打开一个端口。何时发起资源请求呢？等待资源线程发来信息。
	

	node_thread_init(node_thread_num_for_each_manager);
	
	LOG_INFO << "pid = " << getpid();
	EventLoop loop;
	InetAddress listenAddr(static_cast<uint16_t>(resource_manager_first_port + resource_manager_id));
	ResourceManager server(&loop, listenAddr);
	server.start();
	loop.loop();
}

