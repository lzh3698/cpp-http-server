#pragma once

#include <iostream>
#include <fstream>
#include <chrono>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unordered_map>
#include <string>
#include <iomanip>
#include <sstream>
#include <thread>
#include <mutex>

class logger{
private:
        std::ofstream ofs;      // 文件输出流对象
        std::unordered_map<int, std::chrono::system_clock::time_point> connect_time;	// 客户端描述符为key，连接的时间点为value
        std::mutex mtx;	        // 不同线程写入，需要锁

        std::string get_cur_time();	// 按一定格式获取当前时间
public:
        logger();
        ~logger();
        logger(const logger&) = delete;
        logger& operator=(const logger&) = delete;
        logger(logger &&) = delete;
        logger& operator=(logger &&) = delete;

        void server_start(int port);			// 服务器启动，将端口号写入日志
        void client_linked(int fd, sockaddr_in addr);	// 客户端连接调用
        void client_closed(int fd);			// 客户端关闭调用
        void client_active(std::string mean, std::string route, sockaddr_in addr);	// 客户端的请求内容
        void server_active(std::string status_num, bool is_normal, int size = 0);	// 服务器端的响应
        void fail_open(std::string route);		// 打开文件失败
};
