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
        std::vector<std::thread> threads;
        std::queue<std::function<void()>> tasks;
        std::condition_variable cv;
        std::mutex mtx;
        bool stop;

public:
        thread_pool(int thread_numbers) : stop(false) {
                std::cout << "construct function" << std::endl;
                printf("constructor\n");
                write(STDOUT_FILENO, "abc", 3);
                while(thread_numbers--){
                        std::cout << "construct" << std::endl;
                        threads.emplace_back([this](){

                                while(true){

                                        std::function<void()> func;
                                        {
                                                std::unique_lock<std::mutex> lock(mtx);
                                                std::cout << "wait" << std::endl;
                                                cv.wait(lock, [this](){
                                                        return stop || !tasks.empty();
                                                });
                                                std::cout << "wait finish" << std::endl;
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

        ~thread_pool(){
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

        thread_pool(const thread_pool &pool) = delete;
        thread_pool(thread_pool &&pool) = delete;
        thread_pool &operator=(const thread_pool &pool) = delete;
        thread_pool &operator=(thread_pool &&pool) = delete;

        template<typename Func, typename ...Args>
        void submit(Func &&func, Args &&...args){
                {
                        std::unique_lock<std::mutex> lock(mtx);
                        if(stop){
                                throw std::runtime_error("Submit when stopped!");
                        }
                        tasks.emplace(
                                std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
                        );
                }

                cv.notify_one();
        }

        template<typename Func, typename ...Args>
        auto submit_with_result(Func &&func, Args &&...args)
                -> std::future< typename std::result_of<Func(Args...)>::type > {
                using return_type = typename std::result_of<Func(Args...)>::type;

                auto ptr = std::make_shared<std::packaged_task<return_type()>>(
                        std::bind(
                                std::forward<Func>(func),
                                std::forward<Args>(args)...
                        )
                );

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

                cv.notify_one();

                return f;
        }
};