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

#define PORT 8080                 // 默认监听的端口号
#define BUFFER_SIZE 1024 + 1      // 默认缓冲区大小
#define THREAD_NUMS 5             // 默认线程池的线程数量
#define EPOLL_NUMS 5              // 默认最大文件描述符数量（几乎无用）
#define STATIC_ROUTE "/static/"   // 默认静态文件目录

// 有连接开始时，生成此类对象，描述连接的信息
struct Connection{
        enum means{
                GET,
                NONE
        };
        int fd;		// 客户端套接字描述符
        sockaddr_in addr;
        socklen_t len;
        char read_buffer[BUFFER_SIZE];	// 读取客户端消息时的缓冲区
        string read_message;	// 读到的消息
        string response_head;	// 响应头部，由线程池生成
        string route;		    // 客户端访问的路径
        means mean;		    // 方法
        int body_len;		// 响应体长度（即响应的文件大小）
        off_t offset;		// sendfile发送消息时要使用的偏移量
        bool is_reading;	// 主线程判断是否客户端消息是否还未读取完，正在读取，由于客户端消息可能需要分好几次读取，需要一个变量判断
        bool write_ready;	// 线程池准备好响应消息之后设置为true
        mutex mtx;		    // 线程池和主线程可能会有竞争，需要锁
        bool is_normal;	    // 如果正常响应，线程池设置值为true，若出现Not Found等错误，为false，主线程根据此值判断是否需要打开对应文件读取并发送给客户端
        
		Connection() = default;

        ~Connection(){
				// 客户端套接字描述符由析构函数关闭
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

// 根据文件后缀判断文件类型并返回
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

// 获取并设置Connection类中的mean成员
void get_mean(Connection &con){
        string s = con.read_message.substr(0, con.read_message.find(' '));
        if(s == "GET"){
                con.mean = Connection::GET;
        }else{
                con.mean = Connection::NONE;
        }
}

// 获取并设置Connection类中的route成员
void get_route(Connection &con){
        int first_space = con.read_message.find(' ') + 1;
        int line_end = con.read_message.find('\n');
        string route = con.read_message.substr(first_space, line_end - first_space);
        route = route.substr(0, route.find(' '));
        con.route = STATIC_ROUTE + route;
}

// 线程池处理的主要函数，用于解析客户端请求并生成响应头部
void parse_http_request(Connection &con, logger &loger){

        get_mean(con);	// 获取请求中的方法
        get_route(con);	// 获取请求中的路径

        // 检测路径中是否包含".."，防止非法访问
        if(con.route.find("..") != string::npos){
                string error_info = "403 Forbidden";
                con.response_head = con.response_head + "HTTP/1.1 " + error_info + "\r\n"
                      + "Content-Type: " + get_file_type(con.route) + "\r\n"
                      + "Content-Length: " + "0" + "\r\n"
                      + "\r\n"
                      + error_info;
                {       
	        			// 加锁设置write_ready，is_normal初始化为false，此处不需要设置
                        unique_lock<mutex> lock(con.mtx);
                        con.write_ready = true;
                }
                cout << "403 Forbidden" << endl;
                return;
        }

        if(con.mean == Connection::GET){
				// 尝试打开文件，检测是否存在
                int fd;
                if((fd = open(con.route.c_str(), O_RDONLY)) == -1){
                        string error_info = "404 Not Found";
                        con.response_head = con.response_head + "HTTP/1.1 " + error_info + "\r\n"
                              + "Content-Type: " + get_file_type(con.route) + "\r\n"
                              + "Content-Length: " + to_string(sizeof(error_info )) + "\r\n"
                              + "\r\n"
                              + error_info;
                        {
	                			// 加锁设置write_ready，is_normal初始化为false，此处不需要设置
                                unique_lock<mutex> lock(con.mtx);
                                con.write_ready = true;
                        }
                        cout << "404 Not Found" << endl;
		
	        			// 打开文件失败写入日志
                        loger.fail_open(con.route);
                        return;
                }
	
				// 文件打开成功后，用智能指针管理文件描述符
                auto closer = [](int *fd){ close(*fd); delete fd; };
                unique_ptr<int, decltype(closer)> up(new int(fd), closer);
	
				// 设置Connection中文件大小
                struct stat state;
                if(fstat(fd, &state) == -1){
                        cerr << "Fstat error!" << endl;
                        return;
                }
                int size = state.st_size;
                con.body_len = size;
                con.offset = 0;	// sendfile偏移量设置为0
	
                con.response_head = con.response_head + "HTTP/1.1 200 OK\r\n"
                + "Content-Type: " + get_file_type(con.route) + "\r\n"
                + "Content-Length: " + to_string(size) + "\r\n"
                + "\r\n";
	
				// 解析完成，写入日志
                loger.client_active("GET", con.route, con.addr);

                {
	        			// 加锁设置write_ready、is_normal为true
                        unique_lock<mutex> lock(con.mtx);
                        con.is_normal = true;
                        con.write_ready = true;
                }

        }else{
				// 其他方法，此处暂未扩展
				string error_info = "405 Method Not Allowed";
                con.response_head = con.response_head + "HTTP/1.1 " + error_info + "\r\n"
                    	+ "Content-Type: " + get_file_type(con.route) + "\r\n"
                        + "Content-Length: " + to_string(error_info.size()) + "\r\n"
                        + "\r\n"
                        + error_info;
                {
	                	// 加锁设置write_ready，is_normal初始化为false，此处不需要设置
						unique_lock<mutex> lock(con.mtx);
                    	con.write_ready = true;
                }
                return;
        }
}

// 获取请求头中的请求体Content-Length大小，用于判断消息是否读取完
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
        // 创建日志对象
        logger loger;

        // 创建服务器端套接字（server socket）
        int sc = socket(AF_INET, SOCK_STREAM, 0);
        if(sc == -1){
                cerr << "Socket error!" << endl;
                exit(-1);
        }
	
        // 创建成功，用智能指针管理
        auto socket_closer = [](int *sc){ if(close(*sc) == -1) cerr << "Close server socket error!" << endl; delete sc; };
        unique_ptr<int, decltype(socket_closer)> server_socket(new int(sc), socket_closer);

        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(PORT);

        // 设置端口可重用，SO_REUSEADDR
        int opt = 1;
        if(setsockopt(sc, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
                cerr << "Set sockopt failed!" << endl;
                exit(-1);
        }

        // 绑定端口
        if(bind(sc, (sockaddr*)&addr, sizeof(addr)) == -1){
                cerr << "Bind error!" << endl;
                exit(-1);
        }

        // 写入日志：服务器启动
        loger.server_start(PORT);

        // 开始监听
        if(listen(sc, 3) == -1){
                cerr << "Listen error!" << endl;
                exit(-1);
        }

        // 创建线程池
        thread_pool th_pool(THREAD_NUMS);

        // 创建epoll实例
        int epfd = epoll_create(3);
        if(epfd == -1){
                cerr << "Create epoll error!" << endl;
                exit(-1);
        }

        // epoll创建成功后，用智能指针管理
        auto epoll_closer = [](int *epfd){ close(*epfd); delete epfd; };
        unique_ptr<int, decltype(epoll_closer)> up(new int(epfd), epoll_closer);

        // 设置服务器端监听套接字为非阻塞
        int flags = 0;
        if((flags = fcntl(sc, F_GETFL)) == -1){
                cerr << "Fcntl get error!" << endl;
        }

        flags |= O_NONBLOCK;
        if(fcntl(sc, F_SETFL, flags) == -1){
                cerr << "Fcntl set error!" << endl;
        }

        // 让epoll监测服务器端套接字，若有连接，通知
        struct epoll_event eve;
        eve.events = EPOLLIN;
        eve.data.fd = sc;

        if(epoll_ctl(epfd, EPOLL_CTL_ADD, sc, &eve) == -1){
                cerr << "Epoll_ctl add error!" << endl;
                exit(-1);
        }

        // 创建一张哈希表，客户端的套接字描述符为key，其Connection对象的指针为value（Connection的拷贝赋值运算符和移动赋值运算符都被删除，故哈希表中存放其shared_ptr）
        unordered_map<int, shared_ptr<Connection>> cons;

        while(true){
	
				// new一个数组用于存放epoll监测到的可直接操作（即读取或者写入）的epoll_event对象
                unique_ptr<epoll_event[]> evens(new epoll_event[EPOLL_NUMS]);

				// 可直接操作的描述符个数
                int wait_nums = 0;

				// 循环调用wait
                if((wait_nums = epoll_wait(epfd, evens.get(), EPOLL_NUMS, -1)) != -1){

	        			// 依从取出可操作的epoll_event对象
                        for(int i = 0; i < wait_nums; ++i){
								// 当前操作的描述符
                                int cur_fd = evens[i].data.fd;

								// 若当前描述符为服务器端套接字，则表示有新链接
                                if(cur_fd == sc){
		        						// 客户端ip相关信息
                                        struct sockaddr_in client_addr;
                                        socklen_t client_addr_len = sizeof(addr);

		        						// 获取客户端套接字
                                        int client_sock = accept(sc, (sockaddr*)&client_addr, &client_addr_len);
                                        if(client_sock == -1){
                                                cerr << "Accept error!" << endl;
                                                continue;
                                        }else{
												// 客户端连接写入日志
                                                loger.client_linked(client_sock, client_addr);

												// 将客户端套接字设置为非阻塞
                                                int flags = 0;
                                                if((flags = fcntl(client_sock, F_GETFL)) == -1){
                                                        cerr << "Fcntl get error!" << endl;
                                                }

                                                flags |= O_NONBLOCK;
                                                if(fcntl(client_sock, F_SETFL, flags) == -1){
                                                        cerr << "Fcntl set error!" << endl;
                                                }

												// 让epoll监测客户端套接字的发送行为
                                                struct epoll_event e;
                                                e.events = EPOLLIN;
                                                e.data.fd = client_sock;
                                                if(epoll_ctl(epfd, EPOLL_CTL_ADD,
                                                                client_sock, &e) == -1){
                                                        cerr << "Epoll ctl error!" << endl;
                                                        continue;
                                                }

												// 用shared_ptr管理新建的Connection对象，并放入哈希表
                                                shared_ptr<Connection> new_con(new Connection(client_sock, client_addr, client_addr_len), [&loger, client_sock](Connection *con){ delete con; loger.client_closed(client_sock); });
                                                cons.emplace(client_sock, new_con);
                                        }
                                }else{
		        						// 当前描述符为客户端套接字
                                        Connection &to_deal = *cons[cur_fd];

		        						// 判断客户端消息之前是否读取完成，若没有，则继续读取
                                        if(to_deal.is_reading){
                                                int read_nums = 0;

                                                int head_len = 0;
                                                int body_len = 0;
                                                int body_begin = 0;
                                                while(true){
			        									// 读取消息
                                                        if((read_nums = read(cur_fd, to_deal.read_buffer, BUFFER_SIZE)) < 0){
                                                                if(errno != EAGAIN){
                                                                        cerr << "Read error!" << endl;
                                                                        cons.erase(cur_fd);
                                                                        epoll_ctl(epfd, EPOLL_CTL_DEL, cur_fd, NULL);
                                                                }
                                                                break;
                                                        }else{
																// 将读到的消息放入读取缓冲区，再赋值给Connection的string成员
                                                                to_deal.read_buffer[read_nums] = '\0';

                                                                to_deal.read_message += to_deal.read_buffer;

																// 若连续找到两个"\r\n"，说明请求头结束
                                                                if((head_len = to_deal.read_message.find("\r\n\r\n")) == string::npos){
                                                                        continue;
                                                                }

																// 此时请求头结束，可以获取请求头中的Content-Length
                                                                body_len = get_body_len(to_deal.read_message);

																// 请求体的索引为之前找到"\r\n\r\n"的位置再加上这四个字符的长度
                                                                body_begin = head_len + 4;

																// 已读到的消息总长度 - 请求体开始索引 = 已读到的请求体长度，此处判断请求体是否读完
                                                                if(body_len == to_deal.read_message.size() - body_begin){
				        												// 请求体读取完成，将读取状态置为false
                                                                        to_deal.is_reading = false;

				        												// 从epoll监测任务中删除此描述符的读取任务
                                                                        if(epoll_ctl(epfd, EPOLL_CTL_DEL, cur_fd, NULL) == -1){
                                                                                cerr << "Epoll_ctl del error!" << endl;
                                                                        }

				        												// 再将此描述符的发送检测任务添加到其中
                                                                        epoll_event write_epoll;
                                                                        write_epoll.events = EPOLLOUT;
                                                                        write_epoll.data.fd = cur_fd;
                                                                        if(epoll_ctl(epfd, EPOLL_CTL_ADD, cur_fd, &write_epoll) == -1){
                                                                                cerr << "Epoll_ctl add error!" << endl;
                                                                        }

				        												// 将解析任务提交到线程池解析
                                                                        th_pool.submit(parse_http_request, std::ref(to_deal), std::ref(loger));

                                                                        break;
                                                                }
                                                        }
                                                }
                                        }else{
			       								// 客户端消息此时已读取完成
												{
														// 加锁判断线程池的解析任务是否完成
                                                        unique_lock<mutex> lock(to_deal.mtx);
                                                        if(!to_deal.write_ready){
																continue;
                                                        }
												}

			        							// 开始发送响应头
                                                if(write(cur_fd, to_deal.response_head.c_str(), to_deal.response_head.size()) != to_deal.response_head.size()){
                                                        cerr << "Send message error!" << endl;
                                                        continue;
                                                }

			        							// 若处于非正常状态，则只需要发送响应头，不必发送文件内容
                                                if(!to_deal.is_normal){
                                                        if(epoll_ctl(epfd, EPOLL_CTL_DEL, cur_fd, NULL) == -1){
                                                                cerr << "Epoll_ctl del error!" << endl;
                                                        }
                                                        cons.erase(cur_fd);
                                                        continue;
                                                }

			        							// 打开文件，并用智能指针管理描述符
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

			        							// 使用sendfile根据目前记录的偏移量进行发送
                                                int send_nums = sendfile(cur_fd, file_fd, &to_deal.offset, to_deal.body_len);
                                                if(send_nums == -1){
												// 判断是否是EAGAIN，若不是，则发送出错
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
														// 记录剩下的响应体长度和下次发送的偏移量
														to_deal.body_len -= send_nums;
														to_deal.offset += send_nums;
												}else{
														// 若sendfile返回值为0，判断剩下的响应体长度是否为0
														if(to_deal.body_len == 0){
																// 发送记录写入日志
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
