CXXFLAGS = -std=c++11 -I include -g -pthread

src = $(wildcard src/*.cpp)
obj = $(patsubst src/%.cpp, src/%.o, $(src))

ALL : http_server

http_server : $(obj)
        g++ $(CXXFLAGS) $^ -o http_server

src/%.o : src/%.cpp
        g++ $(CXXFLAGS) -c $< -o $@

clean :
        rm -rf src/*.o

.PHONY :
        clean ALL