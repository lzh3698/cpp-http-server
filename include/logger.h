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
        std::ofstream ofs;
        std::unordered_map<int, std::chrono::system_clock::time_point> connect_time;
        std::mutex mtx;

        std::string get_cur_time();
public:
        logger();
        ~logger();
        logger(const logger&) = delete;
        logger& operator=(const logger&) = delete;
        logger(logger &&) = delete;
        logger& operator=(logger &&) = delete;

        void server_start(int port);
        void client_linked(int fd, sockaddr_in addr);
        void client_closed(int fd);
        void client_active(std::string mean, std::string route, sockaddr_in addr);
        void server_active(std::string status_num, bool is_normal, int size = 0);
        void fail_open(std::string route);
};