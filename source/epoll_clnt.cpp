#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <strings.h>
#include <sysexits.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include "net_helper.h"

#define EPOLL_QUEUE_LEN 256
#define ECHO_BUFFER_LEN 1024

struct client_t
{
    int timesTransmitted;
    int bytesReceived;
};

void fatal_error(char const * string)
{
    fprintf(stderr,"%s: ",string);
    perror(0);
    exit(EX_OSERR);
}

int child_process(char* remoteName,int remotePort,int numClients,char* data,int timesToRetransmit)
{
    // create epoll file descriptor
    int epoll = epoll_create(EPOLL_QUEUE_LEN);
    if (epoll == -1)
    {
        fatal_error("epoll_create");
    }

    // create all clients, call connect, and add them to epoll loop
    struct client_t clients[numClients];
    memset(clients,0,sizeof(clients)*sizeof(struct client_t));
    for (register int i = 0; i < numClients; ++i)
    {
        struct epoll_event event = epoll_event();
        event.events = EPOLLOUT|EPOLLERR|EPOLLHUP|EPOLLET;
        event.data.fd = make_tcp_client_socket(remoteName,0,remotePort,0,true).fd;
        event.data.ptr = clients+i;
        if (epoll_ctl(epoll,EPOLL_CTL_ADD,event.data.fd,&event) == -1)
        {
            fatal_error("epoll_ctl");
        }
    }

    // execute epoll event loop
    while (true)
    {
        fprintf(stderr,"// wait for epoll to unblock to report socket activity\n");
        static struct epoll_event events[EPOLL_QUEUE_LEN];
        static int eventCount;
        eventCount = epoll_wait(epoll,events,EPOLL_QUEUE_LEN,-1);
        if (eventCount < 0)
        {
            fatal_error("epoll_wait");
        }

        fprintf(stderr,"// epoll unblocked; handle socket activity\n");
        for (register int i = 0; i < eventCount; i++)
        {
            fprintf(stderr,"// close connection if an error occurred\n");
            if (events[i].events&(EPOLLHUP|EPOLLERR))
            {
                fprintf(stderr,"// close connection\n");
                close(events[i].data.fd);

                fprintf(stderr,"// remove the socket from the epoll loop\n");
                static struct epoll_event event = epoll_event();
                epoll_ctl(epoll,EPOLL_CTL_DEL,events[i].data.fd,&event);
                continue;
            }

            // handling case when client socket has data available for writing
            if (events[i].events&EPOLLOUT)
            {
                fprintf(stderr, "// write data to socket\n");
                send(events[i].data.fd,data,strlen(data),0);

                // update client structure
                struct client_t* clientPtr = (struct client_t*) events[i].data.ptr;
                clientPtr->timesTransmitted += 1;

                // configure to wait for data to be available for reading
                static struct epoll_event event = epoll_event();
                event.events = EPOLLIN|EPOLLERR|EPOLLHUP|EPOLLET;
                event.data.fd = events[i].data.fd;
                epoll_ctl(epoll,EPOLL_CTL_MOD,events[i].data.fd,&event);
                continue;
            }

            // handling case when client socket has data available for reading
            if (events[i].events&EPOLLIN)
            {
                fprintf(stderr, "// read data from socket\n");
                char buf[ECHO_BUFFER_LEN];
                int bytesRead = recv(events[i].data.fd,buf,ECHO_BUFFER_LEN,0);

                // update client structure
                struct client_t* clientPtr = (struct client_t*) events[i].data.ptr;
                clientPtr->bytesReceived += bytesRead;

                // handle case when all data has been read, and we need to
                // retransmit
                if (clientPtr->bytesReceived == sizeof(data) &&
                    clientPtr->timesTransmitted < timesToRetransmit)
                {
                    // update client structure
                    clientPtr->bytesReceived = 0;

                    // configure to wait for data to be available for writing
                    static struct epoll_event event = epoll_event();
                    event.events = EPOLLIN|EPOLLERR|EPOLLHUP|EPOLLET;
                    event.data.fd = events[i].data.fd;
                    epoll_ctl(epoll,EPOLL_CTL_MOD,events[i].data.fd,&event);
                    continue;
                }

                // handle case when there should be more data to read
                if (clientPtr->bytesReceived != sizeof(data))
                {
                    continue;
                }

                // handle case when client should be closed, and a new one
                // should be opened in its place
                if (clientPtr->bytesReceived == sizeof(data) &&
                    clientPtr->timesTransmitted == timesToRetransmit)
                {
                    // close the socket...
                    close(events[i].data.fd);

                    // remove the socket from the epoll loop
                    static struct epoll_event event = epoll_event();
                    epoll_ctl(epoll,EPOLL_CTL_DEL,events[i].data.fd,&event);

                    // create and add a new client socket to event loop
                    memset(&event,0,sizeof(event));
                    event.events = EPOLLOUT|EPOLLERR|EPOLLHUP|EPOLLET;
                    event.data.fd = make_tcp_client_socket(remoteName,0,remotePort,0,true).fd;
                    event.data.ptr = events[i].data.ptr;
                    if (epoll_ctl(epoll,EPOLL_CTL_ADD,event.data.fd,&event) == -1)
                    {
                        fatal_error("epoll_ctl");
                    }
                    continue;
                }

                fatal_error(" should not reach this point in code!");
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
    // name of remote host to connect to
    char* remoteName;

    // port to connect to on remote host
    int remotePort;

    // number of worker process to create
    int numWorkerProcesses;

    // number of clients to create on each worker process
    int numClients;

    // data to send to remote server
    char* data;

    // number of times each client should send their data
    int timesToRetransmit;

    // parse command line arguments
    {
        char option;
        bool remoteNameInitialized = false;
        bool remotePortInitialized = false;
        bool numWorkerProcessesInitialized = false;
        bool numClientsInitialized = false;
        bool dataInitialized = false;
        bool timesToRetransmitInitialized = false;
        while ((option = getopt(argc,argv,"h:p:n:c:d:r:")) != -1)
        {
            switch (option)
            {
            case 'h':
                {
                    remoteName = optarg;
                    remoteNameInitialized = true;
                    break;
                }
            case 'p':
                {
                    char* parsedCursor = optarg;
                    remotePort = (int) strtol(optarg,&parsedCursor,10);
                    if (parsedCursor == optarg)
                    {
                        fprintf(stderr,"invalid argument for option -%c\n",option);
                    }
                    else
                    {
                        remotePortInitialized = true;
                    }
                    break;
                }
            case 'n':
                {
                    char* parsedCursor = optarg;
                    numWorkerProcesses = (int) strtol(optarg,&parsedCursor,10);
                    if (parsedCursor == optarg)
                    {
                        fprintf(stderr,"invalid argument for option -%c\n",option);
                    }
                    else
                    {
                        numWorkerProcessesInitialized = true;
                    }
                    break;
                }
            case 'c':
                {
                    char* parsedCursor = optarg;
                    numClients = (int) strtol(optarg,&parsedCursor,10);
                    if (parsedCursor == optarg)
                    {
                        fprintf(stderr,"invalid argument for option -%c\n",option);
                    }
                    else
                    {
                        numClientsInitialized = true;
                    }
                    break;
                }
            case 'd':
                {
                    data = optarg;
                    dataInitialized = true;
                    break;
                }
            case 'r':
                {
                    char* parsedCursor = optarg;
                    timesToRetransmit = (int) strtol(optarg,&parsedCursor,10);
                    if (parsedCursor == optarg)
                    {
                        fprintf(stderr,"invalid argument for option -%c\n",option);
                    }
                    else
                    {
                        timesToRetransmitInitialized = true;
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
        if (!remoteNameInitialized ||
            !remotePortInitialized ||
            !numWorkerProcessesInitialized ||
            !numClientsInitialized ||
            !dataInitialized ||
            !timesToRetransmitInitialized)
        {
            fprintf(stderr,"usage: %s [-h server name] [-p server port] [-n number of worker processes] [-c number of clients] [-d data to send] [-r times to retransmit per client]\n",argv[0]);
            return EX_USAGE;
        }
    }

    // start the worker processes
    for(register int i = 0; i < numWorkerProcesses; ++i)
    {
        // if this is worker process, run worker process code
        if (fork() == 0)
        {
            if (i == 0)
            {
                return child_process(remoteName,remotePort,numClients/numWorkerProcesses,data,timesToRetransmit);
            }
            else
            {
                return child_process(remoteName,remotePort,(numClients/numWorkerProcesses)+(numClients%numWorkerProcesses),data,timesToRetransmit);
            }
        }
    }
    return server_process(numWorkerProcesses);
}
