CC = g++ -g -Wall -W -Wextra

# clean
clean:
	rm -R *.out *.o

# compiling
epoll_svr: ./epoll_svr.o ./net_helper.o
	$(CC) -o ./epoll_svr.out ./epoll_svr.o ./net_helper.o

epoll_svr.o: ./epoll_svr.cpp
	$(CC) -c ./epoll_svr.cpp

net_helper.o: ./net_helper.cpp ./net_helper.h
	$(CC) -c ./net_helper.cpp
