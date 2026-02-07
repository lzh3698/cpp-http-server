#include "thread_pool.h"
using namespace std;

// 构造函数，传入线程数
thread_pool::thread_pool(int thread_numbers) : stop(false) {
        while(thread_numbers--){
				// 以lambda表达式初始化线程，lambda表达式内部循环从任务队列中取出任务函数
        		threads.emplace_back([this](){

                        while(true){
                                std::function<void()> func;
                                {
		        						// 若队列为空，则条件变量调用wait等待，出现任务，则notify之后开始执行，或者stop之后队列为空就退出循环
                                        std::unique_lock<std::mutex> lock(mtx);
                                        cv.wait(lock, [this](){
                                                 return stop || !tasks.empty();
                                        });
                                        if(stop && tasks.empty()){
                                                break;
                                        }
                                        func = std::move(tasks.front());
                                        tasks.pop();
                                }
                                func();
                        }
                });

        }
}

// 析构函数，将stop置为true，notify所有线程，并将所有线程join
thread_pool::~thread_pool(){
        {
                std::unique_lock<std::mutex> lock(mtx);
                stop = true;
        }
        cv.notify_all();
        for(int i = 0; i < threads.size(); ++i){
                if(threads[i].joinable()){
                        threads[i].join();
                }
        }
}
