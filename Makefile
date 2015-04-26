all: tags main

CPPFLAGS = -lmuduo_net -lmuduo_base -lpthread -lboost_serialization -std=c++0x

main : main.o global.o expression_node.o node_thread.o node_thread_server.o\
	node_thread_client.o resource_manager.o resource_manager_client.o \
	deadlock_detection.o
	g++ $^ -o $@  $(CPPFLAGS) 

main.o : main.cpp global.h resource_manager.h node_thread.h
	g++ -c $< -o $@  $(CPPFLAGS)

global.o : global.cpp global.h
	g++ -c $< -o $@  $(CPPFLAGS)

expression_node.o : expression_node.cpp expression_node.h global.h
	g++ -c $< -o $@  $(CPPFLAGS)

node_thread.o : node_thread.cpp global.h node_thread.h \
	node_thread_server.h resource_manager_client.h
	g++ -c $< -o $@ $(CPPFLAGS)

node_thread_server.o : node_thread_server.cpp global.h \
	node_thread_server.h resource_manager_client.h
	g++ -c $< -o $@ $(CPPFLAGS)

node_thread_client.o : node_thread_client.cpp global.h \
	node_thread_client.h resource_manager.h
	g++ -c $< -o $@ $(CPPFLAGS)

resource_manager.o : resource_manager.cpp global.h node_thread_client.h \
	resource_manager.h
	g++ -c $< -o $@ $(CPPFLAGS)

resource_manager_client.o : resource_manager_client.cpp global.h \
	node_thread.h resource_manager_client.h
	g++ -c $< -o $@ $(CPPFLAGS)

deadlock_detection.o : deadlock_detection.cpp deadlock_detection.h \
	expression_node.h
	g++ -c $< -o $@ $(CPPFLAGS)

tags: *.h *.cpp
	ctags -R --c-kinds=+p --fields=+iaS --extra=+q

clean:
	rm *.o main tags

.PHONY : clean
