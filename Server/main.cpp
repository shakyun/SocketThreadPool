#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/epoll.h>
#include "SocketThreadPool.h"
#include "FileMsg.h"
#include <fstream>
#include <iostream>

void test(int a, int b)
{
	printf("hello:%d----%d\n", a, b);
}

int main1()
{
	std::ifstream in("../protobuf-3.6.1", std::ios::in);

}


int main()
{

	// 1.创建套接字
	int lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd == -1)
	{
		perror("socket");
		exit(0);
	}
	int ls = 1;
	setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &ls, sizeof(ls));

	// 2. 绑定 ip, port
	struct sockaddr_in addr;
	socklen_t addrLen = sizeof(addr);
	addr.sin_port = htons(10002);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	int ret = bind(lfd, (struct sockaddr*)&addr, sizeof(addr));
	if (ret == -1)
	{
		perror("bind");
		exit(0);
	}

	getsockname(lfd, (struct sockaddr*)&addr, &addrLen);
	printf("port:%d\n", ntohs(addr.sin_port));
	// 3. 监听
	ret = listen(lfd, 100);
	if (ret == -1)
	{
		perror("listen");
		exit(0);
	}

	// 创建epoll树
	int epfd = epoll_create(1000);
	if (epfd == -1)
	{
		perror("epoll_create");
		exit(0);
	}

	// 将监听lfd添加到树上
	struct epoll_event ev;
	// 检测事件的初始化
	ev.events = EPOLLIN;
	ev.data.fd = lfd;
	epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);

	struct epoll_event events[1024];
	// 开始检测
	ThreadPool tpool(20);
	while (1)
	{
		int nums = epoll_wait(epfd, events, sizeof(events) / sizeof(events[0]), -1);
		printf("numbers = %d\n", nums);

		// 遍历状态变化的文件描述符集合
		for (int i = 0; i < nums; ++i)
		{
			int curfd = events[i].data.fd;
			// 有新连接
			if (curfd == lfd)
			{
				struct sockaddr_in clisock;
				socklen_t len = sizeof(clisock);
				int connfd = accept(lfd, (struct sockaddr*)&clisock, &len);
				if (connfd == -1)
				{
					perror("accept");
					exit(0);
				}
				// 将通信的fd挂到树上
				//ev.events = EPOLLIN | EPOLLOUT;
				ev.events = EPOLLIN;
				ev.data.fd = connfd;
				epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev);
			}
			// 通信
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
					epoll_ctl(epfd, EPOLL_CTL_DEL, curfd, NULL);
				}
				else if (count == -1)
				{
					perror("read");
					exit(0);
				}
				else
				{
					// 正常情况
					//printf("client say: %s\n", buf);
					Msg msg = (Msg)atoi(buf);
					switch (msg)
					{
					case file:
						for(int i = 0; i < 20; ++i)
							tpool.addTask(std::bind(test, 1, std::placeholders::_1));
						break;
					default:
						printf("其他\n");
					}

				}
			}

		}
		printf("连接池暴露端口: %d\n", tpool.getListenPort());
	}

	close(lfd);

	return 0;
}


