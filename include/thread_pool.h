#pragma once

#include <iostream>
#include <thread>
#include <condition_variable>
#include <vector>
#include <queue>
#include <functional>
#include <utility>
#include <mutex>
#include <future>
#include <memory>
#include <type_traits>
#include <stdexcept>
#include <unistd.h>

class thread_pool{
private:
        std::vector<std::thread> threads;			// 线程vector
        std::queue<std::function<void()>> tasks;	// 任务队列
        std::condition_variable cv;					// 条件变量，任务和停止通知
        std::mutex mtx;			// 锁，用于队列的操作
        bool stop;				// 停止
public:
        thread_pool(int thread_numbers);
        ~thread_pool();
        thread_pool(const thread_pool&) = delete;
        thread_pool& operator=(const thread_pool&) = delete;
        thread_pool(thread_pool&&) = delete;
        thread_pool& operator=(thread_pool&&) = delete;

        // 函数任务提交
        template<typename Func, typename ...Args>
        void submit(Func &&func, Args &&...args){
                {
                        std::unique_lock<std::mutex> lock(mtx);
	        			// 若已经停止，抛出异常
                        if(stop){
                                throw std::runtime_error("Submit when stopped!");
                        }
					
	        			// 将bind生成的可调用对象加入队列
                        tasks.emplace(
                                std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
                        );
                }
				// 通知线程
                cv.notify_one();
        }

        // 带返回值的提交函数，func函数调用的返回值的furure作为此函数返回值
        template<typename Func, typename ...Args>
        auto submit_with_result(Func &&func, Args &&...args) -> std::future< typename std::result_of<Func(Args...)>::type > {
                using return_type = typename std::result_of<Func(Args...)>::type;

				// 将bind创建的可调用对象放入智能指针中，再将智能指针封装成lambda表达式传入任务队列（编译器不支持C++14，否则可直接将packaged_task移入lambda）
                auto ptr = std::make_shared<std::packaged_task<return_type()>>(
                        std::bind(
                                std::forward<Func>(func),
                                std::forward<Args>(args)...
                        )
                );

				// 获取future对象，用于返回
                std::future<return_type> f = ptr->get_future();

                {
                        std::unique_lock<std::mutex> lock(mtx);
                        if(stop){
                                throw std::runtime_error("Submit when stopped!");
                        }
                        tasks.emplace(
                                [ptr](){
                                        (*ptr)();
                                }
                        );
                }

				// 通知线程
                cv.notify_one();

                return f;
        }
};
