src = $(wildcard src/*.cpp)
obj = $(patsubst src/%.cpp, src/%.o, $(src))

ALL : http_server

http_server : $(obj)
        g++ -std=c++11 $^ -o http_server -pthread

%.o : %.cpp
        g++ -std=c++11 -c $< -o $@

clean :
        rm -rf src/*.o

.PHONY :
        clean ALL