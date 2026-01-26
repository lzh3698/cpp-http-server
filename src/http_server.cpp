#include <iostream>
#include <sys/socket.h>
#include <fcntl.h>
#include <pthread.h>
#include <cstdio>
#include <unistd.h>
#include <arpa/inet.h>
#include <cstdlib>
#include <cstring>
#include "thread_pool.cpp"
#include <string>
#include <sys/stat.h>
using namespace std;

#define PORT 8080
#define BUFFER_SIZE 1024 + 1

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

void handle_http_request(int sock, string &message, string &route){
        cout << "Begin handle http request" << endl;
        int fd;
        route = "/my_server/static" + route;
        if(route.find("..") != string::npos){
                string s;
                string error_info = "403 Forbidden";
                s = s + "HTTP/1.1 403 Forbidden\r\n"
                      + "Content-Type: text/html\r\n"
                      + "Content-Length: " + to_string(error_info.size()) + "\r\n"
                      + "\r\n"
                      + error_info;
                if(write(sock, s.c_str(), s.size()) != s.size()){
                        cerr << "HTTP write error!" << endl;
                }
                return;
        }

        if((fd = open(route.c_str(), O_RDONLY)) == -1){
                string s;
                string error_info = "404 Not Found\r\n";
                s = s + "HTTP/1.1 404 Not Found\r\n"
                      + "Content-Type: text/html\r\n"
                      + "Content-Length: " + to_string(error_info.size()) + "\r\n"
                      + "\r\n"
                      + error_info;
                if(write(sock, s.c_str(), s.size()) != s.size()){
                        cerr << "HTTP write error!" << endl;
                }
                return;
        }

        struct stat state;
        if(fstat(fd, &state) == -1){
                cerr << "Fstat error!" << endl;
                return;
        }
        int size = state.st_size;

        string response;
        response = response + "HTTP/1.1 200 OK\r\n"
        + "Content-Type: " + get_file_type(route) + "\r\n"
        + "Content-Length: " + to_string(size) + "\r\n"
        + "\r\n";

        string content;

        int read_nums = 0;
        char buffer[BUFFER_SIZE] = {0};
        while((read_nums = read(fd, buffer, BUFFER_SIZE)) > 0){
                buffer[read_nums] = '\0';
                content += buffer;
                if(read_nums < BUFFER_SIZE){
                        break;
                        return;
                }
        }

        if(read_nums == -1){
                cerr << "HTTP read error!" << endl;
                return;
        }

        string to_send = response + content;
        if(write(sock, to_send.c_str(), to_send.size()) == -1){
                cerr << "Write error!" << endl;
                return;
        }

}

bool is_http_request(const string &message, string &route){
        string request_line = message.substr(0, message.find('\n'));

        int index = request_line.find(' ');
        if(index == string::npos){
                return false;
        }

        string request = request_line.substr(0, index);
        request_line.erase(0, index + 1);

        int i = 0;
        for(; i < request_line.size(); ++i){
                if(request_line[i] != ' '){
                        break;
                }
        }

        if(i == request_line.size()){
                return false;
        }

        if(request != "GET" && request != "POST" && request != "PUT"){
                return false;
        }

        if((index = request_line.find(' ')) == string::npos){
                return false;
        }
        if(request_line[0] != '/'){
                return false;
        }

        route = request_line.substr(0, index);
        request_line.erase(0, index + 1);

        i = 0;
        for(; i < request_line.size(); ++i){
                if(request_line[i] != ' '){
                        break;
                }
        }

        if(i == request_line.size()){
                return false;
        }

        if(request_line.substr(0, 4) != "HTTP"){
                return false;
        }

        return true;
}

void handle(int client_sock){

        char buffer[BUFFER_SIZE] = {0};

        int read_nums = 0;

        string accepted_message;

        while(true){
                if((read_nums = read(client_sock, buffer, BUFFER_SIZE)) < 0){
                        cerr << "Read error!" << endl;
                        return;
                }else{

                        buffer[read_nums] = '\0';

                        accepted_message += buffer;

                        if(read_nums < BUFFER_SIZE){
                                break;
                        }
                }
        }

        string route;

        if(is_http_request(accepted_message, route)){
                handle_http_request(client_sock, accepted_message, route);
        }
        else{
                if(write(client_sock, buffer, read_nums) != read_nums){
                        cerr << "Write error!" << endl;
                        return;
                }
        }

        if(close(client_sock) < 0){
                cerr << "Close error!" << endl;
                return;
        }

}

int main(){

        int sc = socket(AF_INET, SOCK_STREAM, 0);
        if(sc == -1){

                cerr << "Socket error!" << endl;
                exit(-1);
        }

        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(PORT);
        if(bind(sc, (sockaddr*)&addr, sizeof(addr)) == -1){
                cerr << "Bind error!" << endl;
                exit(-1);
        }

        if(listen(sc, 3) == -1){
                cerr << "Listen error!" << endl;
                exit(-1);
        }

        thread_pool th_pool(5);

        while(true){
                struct sockaddr_in acc_addr;
                int new_sc;
                const char *wrong = "Cannot link to server";
                socklen_t addr_len = sizeof(acc_addr);
                if((new_sc = accept(sc, (sockaddr*)&acc_addr, &addr_len)) == -1){
                        cerr << "Accept error!" << endl;
                        continue;
                }

                th_pool.submit(handle, new_sc);
                cerr << "main running" << endl;
        }


        return 0;
}