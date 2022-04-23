src = $(wildcard ./*.cpp ./http/*.cpp)

obj = $(patsubst %.cpp, %.o, $(src))

ALL:server

server:$(obj)
	g++ $^ -o $@ -lpthread

$(obj):%.o:%.cpp
	g++ -c $< -o $@

clean:
	-rm -rf $(obj) server

.PHONY:clean ALL

