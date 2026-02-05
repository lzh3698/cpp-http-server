#include "logger.h"
using namespace std;

std::string logger::get_cur_time(){
        std::ostringstream oss;
        auto cur_time_point = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(cur_time_point.time_since_epoch()) % 1000;
        auto cur_time_stamp = std::chrono::duration_cast<std::chrono::seconds>(cur_time_point.time_since_epoch()).count();
        char buffer[60]{0};
        struct tm timeinfo;
        localtime_r(&cur_time_stamp, &timeinfo);
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
        oss << "[ " << buffer << "." << std::setfill('0') << std::setw(3) << ms.count() << " ]";
        return oss.str();
}

logger::logger() : ofs("./http_server_log", std::ios::app | std::ios::out) {

}

logger::~logger(){
        ofs.close();
}

void logger::server_start(int port){
        std::unique_lock<std::mutex> lock(mtx);
        ofs << get_cur_time() << " Server started on port " << port << std::endl;
}

void logger::client_linked(int fd, sockaddr_in addr) {
        std::unique_lock<std::mutex> lock(mtx);
        char ip_addr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ip_addr, sizeof(ip_addr));
        connect_time[fd] = std::chrono::system_clock::now();
        ofs << get_cur_time() << " New connection: " << ip_addr << ":" << ntohs(addr.sin_port) << " fd=" << fd << std::endl;
}

void logger::client_closed(int fd){
        std::unique_lock<std::mutex> lock(mtx);
        ofs << get_cur_time() << " Connection closed: fd=" << fd << ", duration=" << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - connect_time[fd]).count() << "ms" << std::endl;
}

void logger::client_active(std::string mean, std::string route, sockaddr_in addr){
        std::unique_lock<std::mutex> lock(mtx);
        char ip_addr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr.sin_addr, ip_addr, sizeof(ip_addr));
        ofs << get_cur_time() << " Request: " << mean << " " << route << " from " << ip_addr << std::endl;

}

void logger::server_active(std::string status_num, bool is_normal, int size){
        std::unique_lock<std::mutex> lock(mtx);
        ofs << get_cur_time() << " Response: " << status_num;
        if(is_normal){
                ofs << ", size=" << size;
        }
        ofs << endl;
}

void logger::fail_open(std::string route){
                std::unique_lock<std::mutex> lock(mtx);
                ofs << get_cur_time() << "Failed to open " << route << std::endl;
}