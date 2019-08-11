/**
 * @file ThreadPool.h
 * @author your name (you@domain.com)
 * @brief 线程池
 * @version 0.1
 * @date 2019-08-11
 *
 * @copyright Copyright (c) 2019
 *
 */
#pragma once
#include <sys/epoll.h>
#include <pthread.h>
#include <functional>
#include <deque>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <list>
#include <vector>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

struct TaskResource
{
	int socket;
	std::function<void(int socket)> task;
};

class ThreadPool
{
public:
	typedef std::function<void(int socket)> Task;
public:
	ThreadPool(int threadNum = 4);
	~ThreadPool();
public:
	size_t addTask(const Task& task);
	void stop();
	size_t size();
	bool isResourceSatisfy();
	TaskResource* task();
public:
	int getThreadNum();
	int getEpfd();
	int getTcpServer();
	int getListenPort();
	pthread_cond_t& getConnCondition();
	std::list<int>& getTcpSockets();
	void setListenPort(int port);
private:
	void createThread();
	void createServer();
	static void* threadFunc(void * arg);
	static void* socketThreadFunc(void * arg);
private:
	ThreadPool& operator=(const ThreadPool&);
	ThreadPool(const ThreadPool&);
private:
	volatile bool       isRuning_;
	int                 threadNum_;
	int                 epfd_;
	pthread_t*          threads_;
	pthread_t           socketThread_;          //监听连接线程，填充连接池

	std::deque<Task>    taskQueue_;             //任务队列
	std::vector<int>    tcpServers_;            //监听队列
	int                 tcpServer_;
	int                 listenPort_;
	std::list<int>      tcpSockets_;            //连接池
	pthread_mutex_t     mutex_;
	pthread_cond_t      condition_;
	pthread_cond_t      connCondition_;         //连接池锁
};

