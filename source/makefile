CC = g++ -g -Wall -W -Wextra
LIBS = -pthread

# clean
clean:
	rm -R *.out *.o

# compiling
select_svr: ./select_svr.o ./epoll_svr.o ./select_helper.o ./net_helper.o
	$(CC) $(LIBS) -o ./select_svr.out ./select_helper.o ./epoll_svr.o ./net_helper.o

epoll_svr: ./epoll_svr.o ./net_helper.o
	$(CC) $(LIBS) -o ./epoll_svr.out ./epoll_svr.o ./net_helper.o

epoll_clnt: ./epoll_clnt.o ./net_helper.o
	$(CC) $(LIBS) -o ./epoll_clnt.out ./epoll_clnt.o ./net_helper.o

select_svr.o: ./select_svr.cpp
	$(CC) -c ./select_svr.cpp

epoll_svr.o: ./epoll_svr.cpp
	$(CC) -c ./epoll_svr.cpp

epoll_clnt.o: ./epoll_clnt.cpp
	$(CC) -c ./epoll_clnt.cpp

net_helper.o: ./net_helper.cpp ./net_helper.h
	$(CC) -c ./net_helper.cpp

select_helper.o: ./select_helper.cpp ./select_helper.h
	$(CC) -c ./select_helper.cpp
