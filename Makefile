target= my_webserver
source_file=$(wildcard ./*.cpp)
objects_file=$(patsubst %.cpp, %.o, $(source_file))

$(target):$(objects_file)
	$(CXX) $(objects_file) -o $(target) -pthread -g

%.o : %.cpp
	$(CXX) -c $^ -o $@ -g


.PHONY:clean

clean:
	rm $(objects_file) -f



.PHONY:clean_all
	
clean_all:
	rm $(target) -f
	rm $(objects_file) -f
