#include <iostream>
#include <sys/socket.h>
#include <fcntl.h>
#include <pthread.h>
#include <cstdio>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include "thread_pool.h"
#include <string>
#include <sys/stat.h>
#include <memory>
#include <sys/epoll.h>
#include <mutex>
#include <unordered_map>
#include <error.h>
#include <sys/sendfile.h>
#include "logger.h"
using namespace std;

#define PORT 8080
#define BUFFER_SIZE 1024 + 1
#define THREAD_NUMS 5
#define EPOLL_NUMS 5

struct Connection{
        enum means{
                GET,
                NONE
        };
        int fd;
        sockaddr_in addr;
        socklen_t len;
        char read_buffer[BUFFER_SIZE];
        string read_message;
        string response_head;
        string route;
        means mean;
        int body_len;
        off_t offset;
        bool is_reading;
        bool write_ready;
        mutex mtx;
        bool is_normal;
        Connection() = default;
        ~Connection(){
                if(close(fd) == -1){
                        cerr << "Close Connection fd error!" << endl;
                }
        }
        Connection(int f, sockaddr_in a, socklen_t l)
                : fd(f), addr(a), len(l)
                , read_buffer{0}, is_reading(true), write_ready(false), is_normal(false){

        }
        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;
        Connection(Connection &&) = default;
        Connection& operator=(Connection &&) = default;
};

string get_file_type(const string &filename){
        int index = filename.rfind('.');
        string s;
        if(index != string::npos)
                s = filename.substr(index);
        if(s == ".html") return "text/html";
        if(s == ".txt") return "text/txt";
        if(s == ".jpg") return "image/jpeg";
        if(s == ".png") return "image/png";

        return "application/octet-stream";
}

void get_mean(Connection &con){
        string s = con.read_message.substr(0, con.read_message.find(' '));
        if(s == "GET"){
                con.mean = Connection::GET;
        }else{
                con.mean = Connection::NONE;
        }
}

void get_route(Connection &con){
        int first_space = con.read_message.find(' ') + 1;
        int line_end = con.read_message.find('\n');
        string route = con.read_message.substr(first_space, line_end - first_space);
        route = route.substr(0, route.find(' '));
        con.route = "/static/" + route;
}

void parse_http_request(Connection &con, logger &loger){

        get_mean(con);
        get_route(con);

        if(con.route.find("..") != string::npos){
                string error_info = "403 Forbidden";
                con.response_head = con.response_head + "HTTP/1.1 403 Forbidden\r\n"
                      + "Content-Type: " + get_file_type(con.route) + "\r\n"
                      + "Content-Length: " + to_string(error_info.size()) + "\r\n"
                      + "\r\n"
                      + error_info;
                {
                        unique_lock<mutex> lock(con.mtx);
                        con.write_ready = true;
                }
                cout << "403 Forbidden" << endl;
                return;
        }

        if(con.mean == Connection::GET){
                int fd;
                if((fd = open(con.route.c_str(), O_RDONLY)) == -1){
                        string error_info = "404 Not Found\r\n";
                        con.response_head = con.response_head + "HTTP/1.1 404 Not Found\r\n"
                              + "Content-Type: " + get_file_type(con.route) + "\r\n"
                              + "Content-Length: " + to_string(error_info.size()) + "\r\n"
                              + "\r\n"
                              + error_info;
                        {
                                unique_lock<mutex> lock(con.mtx);
                                con.write_ready = true;
                        }
                        cout << "404 Not Found" << endl;
                        loger.fail_open(con.route);
                        return;
                }

                auto closer = [](int *fd){ close(*fd); delete fd; };
                unique_ptr<int, decltype(closer)> up(new int(fd), closer);
                struct stat state;
                if(fstat(fd, &state) == -1){
                        cerr << "Fstat error!" << endl;
                        return;
                }
                int size = state.st_size;
                con.body_len = size;
                con.offset = 0;

                con.response_head = con.response_head + "HTTP/1.1 200 OK\r\n"
                + "Content-Type: " + get_file_type(con.route) + "\r\n"
                + "Content-Length: " + to_string(size) + "\r\n"
                + "\r\n";

                loger.client_active("GET", con.route, con.addr);
                {
                        unique_lock<mutex> lock(con.mtx);
                        con.is_normal = true;
                        con.write_ready = true;
                }

        }else{
                return;
        }
}

int get_body_len(string message){

        string s = "Content-Length:";
        int index = message.find(s);
        if(index == string::npos){
                return 0;
        }
        index += s.size();
        int index2 = message.find('\n', index);
        if(message[index2 - 1] == '\r'){
                --index;
        }
        return stoi(message.substr(index, index2 - index));

}

int main(){
        logger loger;
        int sc = socket(AF_INET, SOCK_STREAM, 0);
        if(sc == -1){

                cerr << "Socket error!" << endl;
                exit(-1);
        }

        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(PORT);

        int opt = 1;
        if(setsockopt(sc, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
                cerr << "Set sockopt failed!" << endl;
                exit(-1);
        }

        if(bind(sc, (sockaddr*)&addr, sizeof(addr)) == -1){
                cerr << "Bind error!" << endl;
                exit(-1);
        }

        loger.server_start(PORT);

        if(listen(sc, 3) == -1){
                cerr << "Listen error!" << endl;
                exit(-1);
        }

        thread_pool th_pool(THREAD_NUMS);
        int epfd = epoll_create(3);
        if(epfd == -1){
                cerr << "Create epoll error!" << endl;
                exit(-1);
        }
        auto epoll_closer = [](int *epfd){ close(*epfd); delete epfd; };
        unique_ptr<int, decltype(epoll_closer)> up(new int(epfd), epoll_closer);

        int flags = 0;
        if((flags = fcntl(sc, F_GETFL)) == -1){
                cerr << "Fcntl get error!" << endl;
        }

        flags |= O_NONBLOCK;
        if(fcntl(sc, F_SETFL, flags) == -1){
                cerr << "Fcntl set error!" << endl;
        }

        struct epoll_event eve;
        eve.events = EPOLLIN;
        eve.data.fd = sc;

        if(epoll_ctl(epfd, EPOLL_CTL_ADD, sc, &eve) == -1){
                cerr << "Epoll_ctl add error!" << endl;
                exit(-1);
        }

        unordered_map<int, shared_ptr<Connection>> cons;

        while(true){

                unique_ptr<epoll_event[]> evens(new epoll_event[EPOLL_NUMS]);

                int wait_nums = 0;
                if((wait_nums = epoll_wait(epfd, evens.get(), EPOLL_NUMS, -1)) != -1){
                        for(int i = 0; i < wait_nums; ++i){
                                int cur_fd = evens[i].data.fd;
                                if(cur_fd == sc){
                                        struct sockaddr_in client_addr;
                                        socklen_t client_addr_len = sizeof(addr);
                                        int client_sock = accept(sc, (sockaddr*)&client_addr, &client_addr_len);
                                        if(client_sock == -1){
                                                cerr << "Accept error!" << endl;
                                                continue;
                                        }else{

                                                loger.client_linked(client_sock, client_addr);

                                                int flags = 0;
                                                if((flags = fcntl(client_sock, F_GETFL)) == -1){
                                                        cerr << "Fcntl get error!" << endl;
                                                }

                                                flags |= O_NONBLOCK;
                                                if(fcntl(client_sock, F_SETFL, flags) == -1){
                                                        cerr << "Fcntl set error!" << endl;
                                                }

                                                struct epoll_event e;
                                                e.events = EPOLLIN;
                                                e.data.fd = client_sock;
                                                if(epoll_ctl(epfd, EPOLL_CTL_ADD,
                                                                client_sock, &e) == -1){
                                                        cerr << "Epoll ctl error!" << endl;
                                                        continue;
                                                }
                                                shared_ptr<Connection> new_con(new Connection(client_sock, client_addr, client_addr_len), [&loger, client_sock](Connection *con){ delete con; loger.client_closed(client_sock); });
                                                cons.emplace(client_sock, new_con);
                                        }
                                }else{
                                        Connection &to_deal = *cons[cur_fd];
                                        if(to_deal.is_reading){
                                                int read_nums = 0;

                                                int head_len = 0;
                                                int body_len = 0;
                                                int body_begin = 0;
                                                while(true){
                                                        if((read_nums = read(cur_fd, to_deal.read_buffer, BUFFER_SIZE)) < 0){
                                                                if(errno != EAGAIN){
                                                                        cerr << "Read error!"
                                                                        << endl;
                                                                        cons.erase(cur_fd);
                                                                        epoll_ctl(epfd, EPOLL_CTL_DEL, cur_fd, NULL);
                                                                }
                                                                break;
                                                        }else{
                                                                to_deal.read_buffer[read_nums] = '\0';

                                                                to_deal.read_message += to_deal.read_buffer;

                                                                if((head_len = to_deal.read_message.find("\r\n\r\n")) == string::npos){
                                                                        continue;
                                                                }

                                                                body_len = get_body_len(to_deal.read_message);
                                                                body_begin = head_len + 4;
                                                                if(body_len == to_deal.read_message.size() - body_begin){
                                                                        to_deal.is_reading = false;
                                                                        if(epoll_ctl(epfd, EPOLL_CTL_DEL, cur_fd, NULL) == -1){
                                                                                cerr << "Epoll_ctl del error!" << endl;
                                                                        }
                                                                        epoll_event write_epoll;
                                                                        write_epoll.events = EPOLLOUT;
                                                                        write_epoll.data.fd = cur_fd;
                                                                        if(epoll_ctl(epfd, EPOLL_CTL_ADD, cur_fd, &write_epoll) == -1){
                                                                                cerr << "Epoll_ctl add error!" << endl;
                                                                        }
                                                                        th_pool.submit(parse_http_request, std::ref(to_deal), std::ref(loger));

                                                                        break;
                                                                }
                                                        }

                                        }}else{
                                                {
                                                        unique_lock<mutex> lock(to_deal.mtx);
                                                        if(!to_deal.write_ready){
                                                                continue;
                                                        }
                                                }
                                                        if(write(cur_fd, to_deal.response_head.c_str(), to_deal.response_head.size()) != to_deal.response_head.size()){
                                                                cerr << "Send message error!" << endl;
                                                                continue;
                                                        }
                                                        if(!to_deal.is_normal){
                                                                if(epoll_ctl(epfd, EPOLL_CTL_DEL, cur_fd, NULL) == -1){
                                                                        cerr << "Epoll_ctl del error!" << endl;
                                                                }
                                                                cons.erase(cur_fd);


                                                                continue;
                                                        }
                                                        int file_fd = open(to_deal.route.c_str(), O_RDONLY);
                                                        shared_ptr<int> manager(new int(file_fd), [](int *fd){ close(*fd); delete fd; });
                                                        if(file_fd == -1){
                                                                cerr << "Open static file error!" << endl;
                                                                if(epoll_ctl(epfd, EPOLL_CTL_DEL, cur_fd, NULL) == -1){

                                                                        cerr << "Epoll_ctl del error!" << endl;
                                                                }
                                                                cons.erase(cur_fd);
                                                                continue;
                                                        }

                                                        int send_nums = sendfile(cur_fd, file_fd, &to_deal.offset, to_deal.body_len);
                                                        if(send_nums == -1){
                                                                if(errno == EAGAIN){
                                                                        continue;
                                                                }else{
                                                                        cerr << "Sendfile error!" << endl;
                                                                        if(epoll_ctl(epfd, EPOLL_CTL_DEL, cur_fd, NULL) == -1){

                                                                                cerr << "Epoll_ctl del error!" << endl;
                                                                        }
                                                                        cons.erase(cur_fd);
                                                                        continue;

                                                                }
                                                        }else if(send_nums > 0){
                                                                to_deal.body_len -= send_nums;
                                                                to_deal.offset += send_nums;
                                                        }else{
                                                                if(to_deal.body_len == 0){
                                                                        loger.server_active("200 OK", true, to_deal.offset);
                                                                        if(epoll_ctl(epfd, EPOLL_CTL_DEL, cur_fd, NULL) == -1){

                                                                                cerr << "Epoll_ctl del error!" << endl;
                                                                        }
                                                                        cons.erase(cur_fd);
                                                                        continue;

                                                                }else{
                                                                        cerr << "Send error!" << endl;
                                                                        if(epoll_ctl(epfd, EPOLL_CTL_DEL, cur_fd, NULL) == -1){

                                                                                cerr << "Epoll_ctl del error!" << endl;
                                                                        }
                                                                        cons.erase(cur_fd);
                                                                        continue;
                                                                }
                                                        }
                                        }

                                }
                        }
                }
        }


        return 0;
}