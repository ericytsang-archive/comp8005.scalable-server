#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <sysexits.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include "net_helper.h"

#define EPOLL_QUEUE_LEN 256
#define ECHO_BUFFER_LEN 1024

void fatal_error(char const * string)
{
    fprintf(stderr,"%s: ",string);
    perror(0);
    exit(EX_OSERR);
}

int child_process(int serverSocket)
{
    // create epoll file descriptor
    int epoll = epoll_create(EPOLL_QUEUE_LEN);
    if (epoll == -1)
    {
        fatal_error("epoll_create");
    }

    // add server socket to epoll event loop
    {
        struct epoll_event event = epoll_event();
        event.events = EPOLLIN|EPOLLERR|EPOLLHUP|EPOLLET;
        event.data.fd = serverSocket;
        if (epoll_ctl(epoll,EPOLL_CTL_ADD,serverSocket,&event) == -1)
        {
            fatal_error("epoll_ctl");
        }
    }

    // execute epoll event loop
    while (true)
    {
        // wait for epoll to unblock to report socket activity
        static struct epoll_event events[EPOLL_QUEUE_LEN];
        static int eventCount;
        eventCount = epoll_wait(epoll,events,EPOLL_QUEUE_LEN,-1);
        if (eventCount < 0)
        {
            fatal_error("epoll_wait");
        }

        // epoll unblocked; handle socket activity
        for (register int i = 0; i < eventCount; i++)
        {
            // close connection if an error occurred
            if (events[i].events&(EPOLLHUP|EPOLLERR))
            {
                close(events[i].data.fd);
                continue;
            }

            // handling case when client socket has data available for reading
            if (events[i].events&EPOLLIN &&
                events[i].data.fd != serverSocket)
            {
                // replace EPOLLIN flag with EPOLLOUT flag so epoll will unblock
                // when it is available for writing as well as reading.
                static struct epoll_event event = epoll_event();
                event.events = EPOLLOUT|EPOLLERR|EPOLLHUP|EPOLLET;
                event.data.fd = events[i].data.fd;
                epoll_ctl(epoll,EPOLL_CTL_MOD,events[i].data.fd,&event);
                continue;
            }

            // handling case when client socket has data available for writing
            if (events[i].events&EPOLLOUT &&
                events[i].data.fd != serverSocket)
            {
                // read data from socket...
                char buf[ECHO_BUFFER_LEN];
                int bytesRead = recv(events[i].data.fd,buf,ECHO_BUFFER_LEN,0);

                if (bytesRead > 0)
                {
                    // echo the data back to the clients if data was read
                    send(events[i].data.fd,buf,bytesRead,0);

                    // replace EPOLLOUT flag with EPOLLIN flag so epoll will
                    // unblock if there is still data on the socket to read, or
                    // when more data arrives.
                    static struct epoll_event event = epoll_event();
                    event.events = EPOLLIN|EPOLLERR|EPOLLHUP|EPOLLET;
                    event.data.fd = events[i].data.fd;
                    epoll_ctl(epoll,EPOLL_CTL_MOD,events[i].data.fd,&event);
                }
                else
                {
                    // close the socket if connection is closed or in error
                    close(events[i].data.fd);

                    // remove the socket from the epoll loop
                    static struct epoll_event event = epoll_event();
                    epoll_ctl(epoll,EPOLL_CTL_DEL,events[i].data.fd,&event);
                }
                continue;
            }

            // handling case when server socket receives a connection request
            if (events[i].events&EPOLLIN &&
                events[i].data.fd == serverSocket)
            {
                // accept the remote connection
                int newSocket = accept(serverSocket,0,0);

                if (newSocket == -1 && errno != EAGAIN)
                {
                    fatal_error("accept");
                }
                else if (errno == EAGAIN)
                {
                    errno = 0;
                    continue;
                }

                // configure new socket to be non-blocking
                int existingFlags = fcntl(newSocket,F_GETFL,0);
                if (fcntl(newSocket,F_SETFL,O_NONBLOCK|existingFlags) == -1)
                {
                    fatal_error("fcntl");
                }

                // add new socket to epoll loop
                static struct epoll_event event = epoll_event();
                event.events = EPOLLIN|EPOLLERR|EPOLLHUP|EPOLLET;
                event.data.fd = newSocket;
                if (epoll_ctl(epoll,EPOLL_CTL_ADD,newSocket,&event) == -1)
                {
                    fatal_error("epoll_ctl");
                }
                continue;
            }
        }
    }
    return EX_OK;
}

int server_process(int numWorkerProcesses)
{
    for (register int i = 0; i < numWorkerProcesses; ++i) wait(0);
    return EX_OK;
}

int main (int argc, char* argv[])
{
    // file descriptor to a server socket
    int serverSocket;

    // port for server socket to listen on
    int listeningPort;

    // number of worker process to create to server connections
    int numWorkerProcesses;

    // parse command line arguments
    {
        char option;
        int portInitialized = false;
        int numWorkerProcessesInitialized = false;
        while ((option = getopt(argc,argv,"p:n:")) != -1)
        {
            switch (option)
            {
            case 'p':
                {
                    char* parsedCursor = optarg;
                    listeningPort = (int) strtol(optarg,&parsedCursor,10);
                    if (parsedCursor == optarg)
                    {
                        fprintf(stderr,"invalid argument for option -p\n");
                    }
                    else
                    {
                        portInitialized = true;
                    }
                    break;
                }
            case 'n':
                {
                    char* parsedCursor = optarg;
                    numWorkerProcesses = (int) strtol(optarg,&parsedCursor,10);
                    if (parsedCursor == optarg)
                    {
                        fprintf(stderr,"invalid argument for option -n\n");
                    }
                    else
                    {
                        numWorkerProcessesInitialized = true;
                    }
                    break;
                }
            case '?':
                {
                    if (isprint (optopt))
                    {
                        fprintf(stderr,"unknown option \"-%c\".\n", optopt);
                    }
                    else
                    {
                        fprintf(stderr,"unknown option character \"%x\".\n",optopt);
                    }
                }
            default:
                {
                    fatal_error("");
                }
            }
        }

        // print usage and abort if not all required arguments were provided
        if (!portInitialized &&
            !numWorkerProcessesInitialized)
        {
            fprintf(stderr,"usage: %s [-p server listening port] [-n number of worker processes]\n",argv[0]);
            return EX_USAGE;
        }
    }

    // create server socket
    serverSocket = make_tcp_server_socket(listeningPort,true).fd;
    if (serverSocket == -1)
    {
        fatal_error("socket");
    }

    // start the worker processes
    for(register int i = 0; i < numWorkerProcesses; ++i)
    {
        // if this is worker process, run worker process code
        if (fork() == 0)
        {
            return child_process(serverSocket);
        }
    }
    return server_process(numWorkerProcesses);
}
