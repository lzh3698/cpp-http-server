#include "thread_pool.h"
using namespace std;

thread_pool::thread_pool(int thread_numbers) : stop(false) {
        while(thread_numbers--){
                threads.emplace_back([this](){

                        while(true){
                                std::function<void()> func;
                                {
                                        std::unique_lock<std::mutex> lock(mtx);
                                        cv.wait(lock, [this](){                                                 return stop || !tasks.empty();
                                        });
                                        if(stop && tasks.empty()){
                                                break;
                                        }
                                        func = std::move(tasks.front());                                               tasks.pop();
                                }
                                func();
                        }
                });

        }
}

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