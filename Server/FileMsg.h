/**
 * @file FileMsg.h
 * @author your name (you@domain.com)
 * @brief 文件协议
 * @version 0.1
 * @date 2019-08-11
 *
 * @copyright Copyright (c) 2019
 *
 */
#pragma once
#include <string>


enum Msg
{
	file
};

struct  FileMsg
{
	std::string fileName;
	int64_t fileSize;
	int64_t fileBlockLen;//文件一块长度
	int64_t fileBlockIndex;//第几块
};