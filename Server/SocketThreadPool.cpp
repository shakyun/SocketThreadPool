#include "SocketThreadPool.h"
/**
 * @brief Construct a new Thread Pool:: Thread Pool object
 *
 * @param threadNum
 */
ThreadPool::ThreadPool(int threadNum)
{
	threadNum_ = threadNum;
	isRuning_ = true;
	createServer();
	createThread();
}

/**
 * @brief Destroy the Thread Pool:: Thread Pool object
 *
 */
ThreadPool::~ThreadPool()
{
	stop();
}

/**
 * @brief 添加任务
 *
 * @param task
 * @return size_t
 */
size_t ThreadPool::addTask(const Task& task)
{
	pthread_mutex_lock(&mutex_);
	taskQueue_.push_back(task);
	int size = taskQueue_.size();
	pthread_mutex_unlock(&mutex_);
	if(isResourceSatisfy())
		pthread_cond_signal(&condition_);
	return size;
}

/**
 * @brief 停止线程池
 *
 */
void ThreadPool::stop()
{
	if (!isRuning_)
	{
		return;
	}
	isRuning_ = false;
	pthread_cond_broadcast(&condition_);
	for (int i = 0; i < threadNum_; ++i)
	{
		pthread_join(threads_[i], NULL);
	}
	pthread_join(socketThread_, NULL);  //释放监听连接线程
	free(threads_);
	threads_ = NULL;
	socketThread_ = 0;
	pthread_mutex_destroy(&mutex_);
	pthread_cond_destroy(&condition_);
}

/**
 * @brief 返回剩余任务个数
 *
 * @return size_t
 */
size_t ThreadPool::size()
{
	pthread_mutex_lock(&mutex_);
	int size = taskQueue_.size();
	pthread_mutex_unlock(&mutex_);
	return size;
}

/**
 * @brief 获取一个任务
 *
 * @return Task
 */
TaskResource* ThreadPool::task()
{
	Task task = NULL;
	int socket = 0;
	pthread_mutex_lock(&mutex_);
	while ((taskQueue_.empty() || tcpSockets_.empty()) && isRuning_)
	{
		pthread_cond_wait(&condition_, &mutex_);
		//pthread_cond_wait(&connCondition_, &mutex_);
	}

	if (!isRuning_)
	{
		pthread_mutex_unlock(&mutex_);
		return NULL;
	}
	TaskResource *taskres = new TaskResource;
	assert(!taskQueue_.empty());
	task = taskQueue_.front();
	taskQueue_.pop_front();

	socket = tcpSockets_.front();
	tcpSockets_.pop_front();

	taskres->task = task;
	taskres->socket = socket;
	printf("连接池空闲连接:%d\n", tcpSockets_.size());
	printf("任务池等待任务:%d\n", taskQueue_.size());

	pthread_mutex_unlock(&mutex_);
	return taskres;
}

/**
 * @brief 初始化线程池
 *
 */
void ThreadPool::createThread()
{
	pthread_mutex_init(&mutex_, NULL);
	pthread_cond_init(&condition_, NULL);
	//pthread_cond_init(&connCondition_, NULL);
	threads_ = (pthread_t *)malloc(sizeof(pthread_t) * threadNum_);
	for (int i = 0; i < threadNum_; ++i)
	{
		pthread_create(&threads_[i], NULL, threadFunc, this);
	}
	pthread_create(&socketThread_, NULL, socketThreadFunc, this);//创建连接监听线程
	return;
}

/**
 * @brief 初始化监听
 *
 */
void ThreadPool::createServer()
{
	// 1.创建套接字
	tcpServer_ = socket(AF_INET, SOCK_STREAM, 0);
	if (tcpServer_ == -1)
	{
		perror("connectPool:");
		exit(0);
	}
	int ls = 1;
	setsockopt(tcpServer_, SOL_SOCKET, SO_REUSEADDR, &ls, sizeof(ls));
	// 2. 绑定 ip, port
	struct sockaddr_in addr;
	socklen_t addrLen = sizeof(addr);
	addr.sin_port = htons(0);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	int ret = bind(tcpServer_, (struct sockaddr*)&addr, sizeof(addr));
	if (ret == -1)
	{
		perror("bind");
		exit(0);
	}
	//获取监听的端口
	getsockname(tcpServer_, (struct sockaddr*)&addr, &addrLen);
	setListenPort(ntohs(addr.sin_port));
	// 3. 监听
	ret = listen(tcpServer_, 100);
	if (ret == -1)
	{
		perror("listen");
		exit(0);
	}
	// 创建epoll树
	epfd_ = epoll_create(1000);
	if (epfd_ == -1)
	{
		perror("connectPpoll_create");
		exit(0);
	}

	// 将监听lfd添加到树上
	struct epoll_event ev;
	// 检测事件的初始化
	ev.events = EPOLLIN;
	ev.data.fd = tcpServer_;
	epoll_ctl(epfd_, EPOLL_CTL_ADD, tcpServer_, &ev);

}

/**
 * @brief 线程执行函数
 *
 * @param threadData
 * @return ThreadPool::void*
 */
void* ThreadPool::threadFunc(void * arg)
{
	pthread_t tid = pthread_self();
	ThreadPool* pool = static_cast<ThreadPool*>(arg);
	while (pool->isRuning_)
	{
		TaskResource* taskres = pool->task();
		if (!taskres)
		{
			printf("thread %lu will exit\n", tid);
			break;
		}
		assert(taskres);
		taskres->task(taskres->socket);
		//归还socket到连接池
		pthread_mutex_lock(&pool->mutex_);
		pool->tcpSockets_.push_front(taskres->socket);
		pthread_mutex_unlock(&pool->mutex_);
		//pthread_cond_signal(&pool->connCondition_);
	}
	return 0;
}

/**
 * @brief 监听连接线程执行函数
 *
 * @param arg
 * @return void*
 */
void* ThreadPool::socketThreadFunc(void * arg)
{
	ThreadPool* pool = static_cast<ThreadPool*>(arg);
	struct epoll_event ev;
	struct epoll_event events[pool->getThreadNum()];
	// 开始检测
	while (1)
	{
		int nums = epoll_wait(pool->getEpfd(), events, sizeof(events) / sizeof(events[0]), -1);
		printf("numbers = %d\n", nums);

		// 遍历状态变化的文件描述符集合
		for (int i = 0; i < nums; ++i)
		{
			int curfd = events[i].data.fd;
			// 有新连接
			if (pool->getTcpServer() == curfd)
			{
				struct sockaddr_in clisock;
				socklen_t len = sizeof(clisock);
				int connfd = accept(curfd, (struct sockaddr*)&clisock, &len);
				if (connfd == -1)
				{
					perror("accept");
					exit(0);
				}
				// 将通信的fd挂到树上
				//ev.events = EPOLLIN | EPOLLOUT;
				ev.events = EPOLLIN;
				ev.data.fd = connfd;
				epoll_ctl(pool->getEpfd(), EPOLL_CTL_ADD, connfd, &ev);
				//加入连接池
				pthread_mutex_lock(&pool->mutex_);
				pool->getTcpSockets().push_back(connfd);
				pthread_mutex_unlock(&pool->mutex_);
				if (pool->isResourceSatisfy())
					pthread_cond_signal(&pool->condition_);
				//pthread_cond_signal(&pool->getConnCondition());
			}
			else
			{
				// 读事件触发, 写事件触发
				if (events[i].events & EPOLLOUT)
				{
					continue;
				}
				char buf[128] = { 0 };
				int count = read(curfd, buf, sizeof(buf));
				if (count == 0)
				{
					printf("client disconnect ...\n");
					close(curfd);
					// 从树上删除该节点
					epoll_ctl(pool->getEpfd(), EPOLL_CTL_DEL, curfd, NULL);
				}
				else if (count == -1)
				{
					perror("read");
					exit(0);
				}
				else
				{
					//正常情况
					write(curfd, "ok", 2);
				}
			}
		}
		printf("连接池空闲连接:%d\n", pool->getTcpSockets().size());
	}
}

/**
 * @brief
 *
 * @return ThreadPool&
 */
ThreadPool& ThreadPool::operator=(const ThreadPool&)
{
}

/**
 * @brief Construct a new Thread Pool:: Thread Pool object
 *
 */
ThreadPool::ThreadPool(const ThreadPool&)
{

}

/**
 * @brief 获取threadNum_
 *
 * @return int
 */
int ThreadPool::getThreadNum()
{
	return threadNum_;
}

/**
 * @brief 获取epfd_
 *
 * @return int
 */
int ThreadPool::getEpfd()
{
	return epfd_;
}

/**
 * @brief 设置监听端口
 *
 * @param port
 */
void ThreadPool::setListenPort(int port)
{
	listenPort_ = port;
}

/**
 * @brief 获取tcpServer_
 *
 * @return int
 */
int ThreadPool::getTcpServer()
{
	return tcpServer_;
}

/**
 * @brief 获取tcpSocket_
 *
 * @return std::list<int>&
 */
std::list<int>& ThreadPool::getTcpSockets()
{
	return tcpSockets_;
}

/**
 * @brief 获取condition_
 *
 * @return pthread_cond_t&
 */
pthread_cond_t& ThreadPool::getConnCondition()
{
	return connCondition_;
}

/**
 * @brief 获取listenPort
 *
 * @return int
 */
int ThreadPool::getListenPort()
{
	return listenPort_;
}


/**
*@brief  检查资源是否全部满足
*@author	erase
*@date  2019.8.12 
*@return  bool  
*/
bool ThreadPool::isResourceSatisfy()
{
	if (tcpSockets_.empty() && taskQueue_.empty())
	{
		return false;
	}
	return true;
}