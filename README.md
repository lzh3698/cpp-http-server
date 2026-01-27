                                C++ HTTP静态文件服务器

本项目从零开始实现了一个完整的HTTP服务器，包含线程池管理、HTTP协议解析、静态文件服务等核心功能。

一、功能特性：

    1、支持HTTP/1.1 GET方法
    2、静态文件服务
    3、线程池管理并发连接
    4、基础路径安全检查
    5、200/404状态码支持

二、编译运行

    1、编译：
        make
    2、运行：
        ./http_server
    3、测试
        curl http://localhost:8080/
        （或使用浏览器访问，默认监听8080端口）

三、技术栈

    1、C++11 现代C++特性（lambda表达式，标准线程库）
    2、POSIX Socket API 底层网络编程（系统调用）
    3、C++多线程
    4、Linux系统编程（文件I/O，系统调用）

四、实现细节

    1、线程池实现
        ① 使用工作线程+任务队列模式
        ② 支持任意可调用对象的提交
        ③ 可使用std::future获取异步结果
        ④ 线程安全的队列操作

    2、HTTP协议解析
        ① 支持请求行解析
        ② 正确处理Content-Length

五、项目结构

    cpp-http-server/
    ├── src/
    │   ├── http_server.cpp       // 主服务器实现
    │   └── thread_pool.cpp       // 线程池实现
    ├── makefile                  // 构建配置
    └── .gitignore

六、环境要求

    1、Linux/UNIX系统
    2、支持C++11
    3、C++11标准线程库
