#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <strings.h>
#include <sysexits.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include "net_helper.h"

#define EPOLL_QUEUE_LEN 2048
#define ECHO_BUFFER_LEN 1024

/**
 * pointer to a sem_t sized shared memory where a semaphore will be allocated
 * onto. used by children processes to ensure exclusion when printing statistics
 * upon termination.
 */
sem_t* printStatsLock = 0;

/**
 * duration of the shortest connection.
 */
double minServiceTime = DBL_MAX;

/**
 * duration of the longest connection.
 */
double maxServiceTime = 0;

/**
 * duration of the average service time.
 */
double avgServiceTime = 0;

/**
 * total number of connections made.
 */
unsigned long totalSessionCount = 0;

/**
 * highest number of concurrent connections in one moment.
 */
unsigned long peakSessionCount = 0;

/**
 * current number of concurrent connections.
 */
unsigned long sessionCount = 0;

/**
 * number of clients the process is managing (child process only).
 */
unsigned long targetSessionCount = 0;

/**
 * time stamp taken when child process started (child process only).
 */
long startTime = 0;

struct client_t
{
    int fd;
    unsigned int timesTransmitted;
    unsigned int bytesReceived;
    long timeSynSent;
};

void fatal_error(char const * string)
{
    fprintf(stderr,"%s: ",string);
    perror(0);
    exit(EX_OSERR);
}

void increment_session_count()
{
    sessionCount++;
    if (peakSessionCount < sessionCount)
        peakSessionCount = sessionCount;
}

void decrement_session_count(double instanceServiceTime)
{
    sessionCount--;
    totalSessionCount++;

    // update service times
    if (minServiceTime > instanceServiceTime)
        minServiceTime = instanceServiceTime;
    if (maxServiceTime < instanceServiceTime)
        maxServiceTime = instanceServiceTime;
    double totalServiceTime = avgServiceTime*(totalSessionCount-1)+instanceServiceTime;
    avgServiceTime = totalServiceTime/totalSessionCount;
}

long current_timestamp()
{
    struct timeval te;
    gettimeofday(&te,0);
    return te.tv_sec*1000L + te.tv_usec/1000;
}

void print_statistics(int)
{
    sem_wait(printStatsLock);

    long totalRuntime = current_timestamp()-startTime;

    printf("\n[%lu]\n",(unsigned long) getpid());
    printf("    minServiceTime: %lf ms\n",minServiceTime);
    printf("    maxServiceTime: %lf ms\n",maxServiceTime);
    printf("    avgServiceTime: %lf ms\n",avgServiceTime);
    printf(" totalSessionCount: %li\n",totalSessionCount);
    printf("targetSessionCount: %li\n",targetSessionCount);
    printf("  peakSessionCount: %li\n",peakSessionCount);
    printf("      sessionsRate: %lf sessions served per second\n",(double) totalSessionCount/(totalRuntime/1000L));
    printf("      totalRuntime: %li ms\n",totalRuntime);

    sem_post(printStatsLock);

    exit(0);
}

int child_process(char* remoteName,int remotePort,int numClients,char* data,unsigned int timesToRetransmit)
{
    targetSessionCount = numClients;
    startTime = current_timestamp();

    // set signal handler
    signal(SIGINT,print_statistics);

    // create epoll file descriptor
    int epoll = epoll_create(EPOLL_QUEUE_LEN);
    if (epoll == -1)
    {
        fatal_error("epoll_create");
    }

    // create all clients, call connect, and add them to epoll loop
    struct client_t clients[numClients];
    memset(clients,0,sizeof(clients));
    for (register int i = 0; i < numClients; ++i)
    {
        // create client socket, and setup client_t structure
        client_t* clientPtr = clients+i;
        clientPtr->fd = make_tcp_client_socket(remoteName,0,remotePort,0,true).fd;
        clientPtr->timeSynSent = current_timestamp();

        // add the client to the epoll event loop
        struct epoll_event event = epoll_event();
        event.events = EPOLLOUT|EPOLLERR|EPOLLHUP|EPOLLET;
        event.data.ptr = clientPtr;
        if (epoll_ctl(epoll,EPOLL_CTL_ADD,clients[i].fd,&event) == -1)
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
            struct client_t* clientPtr = (struct client_t*) events[i].data.ptr;

            // close connection if an error occurred
            if (events[i].events&(EPOLLHUP|EPOLLERR))
            {
                // close connection
                close(clientPtr->fd);
                continue;
            }

            // handling case when client socket is available for writing
            if (events[i].events&EPOLLOUT)
            {
                // write data to socket
                send(clientPtr->fd,data,strlen(data),0);

                // update statistics
                if (clientPtr->timesTransmitted == 0)
                    increment_session_count();

                // update client structure
                clientPtr->timesTransmitted += 1;

                // configure to wait for data to be available for reading
                static struct epoll_event event = epoll_event();
                event.events = EPOLLIN|EPOLLERR|EPOLLHUP|EPOLLET;
                event.data.ptr = (void*) clientPtr;
                epoll_ctl(epoll,EPOLL_CTL_MOD,clientPtr->fd,&event);
                continue;
            }

            // handling case when client socket is available for reading
            if (events[i].events&EPOLLIN)
            {
                static char buf[ECHO_BUFFER_LEN];
                register int bytesRead = 0;

                // read until the socket is empty
                while (true)
                {
                    // read data from socket
                    bytesRead = recv(clientPtr->fd,buf,ECHO_BUFFER_LEN,0);

                    // update client structure
                    if (bytesRead > 0)
                    {
                        clientPtr->bytesReceived += bytesRead;
                    }

                    // ignore errors: EWOULDBLOCK and EAGAIN
                    else if (bytesRead == -1 && (errno == EWOULDBLOCK || errno == EAGAIN))
                    {
                        errno = 0;
                        break;
                    }

                    // unexpected error or closed; fatal error!
                    else
                    {
                        fatal_error("recv");
                    }
                }

                // handle case when all data has been read, and we need to
                // retransmit
                if (clientPtr->bytesReceived >= strlen(data) &&
                    clientPtr->timesTransmitted < timesToRetransmit)
                {
                    // update client structure
                    clientPtr->bytesReceived = 0;

                    // configure to wait for data to be available for writing
                    static struct epoll_event event = epoll_event();
                    event.events = EPOLLOUT|EPOLLERR|EPOLLHUP|EPOLLET;
                    event.data.ptr = (void*) clientPtr;
                    epoll_ctl(epoll,EPOLL_CTL_MOD,clientPtr->fd,&event);
                    continue;
                }

                // handle case when client should be closed, and a new one
                // should be opened in its place
                if (clientPtr->bytesReceived >= strlen(data) &&
                    clientPtr->timesTransmitted >= timesToRetransmit)
                {

                    // update statistics
                    double serviceTime = (double) (current_timestamp()-clientPtr->timeSynSent);
                    decrement_session_count(serviceTime);

                    // close the socket
                    if (close(clientPtr->fd) == -1)
                    {
                        fatal_error("close");
                    }

                    // clear client data so the new client socket can make use
                    // of it
                    memset(clientPtr,0,sizeof(struct client_t));

                    // update statistics
                    clientPtr->timeSynSent = current_timestamp();

                    // create and add a new client socket to event loop
                    static struct epoll_event event = epoll_event();
                    event.events = EPOLLOUT|EPOLLERR|EPOLLHUP|EPOLLET;
                    event.data.ptr = (void*) clientPtr;
                    for (register int i = 0; i < 10; ++i)
                    {
                        clientPtr->fd = make_tcp_client_socket(remoteName,0,remotePort,0,true).fd;
                        if (clientPtr->fd >= 0) break;
                    }
                    if (epoll_ctl(epoll,EPOLL_CTL_ADD,clientPtr->fd,&event) == -1)
                    {
                        fatal_error("epoll_ctl");
                    }
                    continue;
                }

                // handle case when there should be more data to read
                if (clientPtr->bytesReceived < strlen(data))
                {
                    continue;
                }

                fatal_error("should not reach this point in code!");
            }
        }
    }
    return EX_OK;
}

int server_process(int numWorkerProcesses,long timeout)
{
    // kill all processes of process group after timeout
    if (timeout > 0)
    {
        usleep(timeout*1000);
        kill(0,SIGINT);
    }

    // wait for all child processes to terminate
    else
    {
        for (register int i = 0; i < numWorkerProcesses; ++i)
        {
            wait(0);
        }
    }

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
    unsigned int timesToRetransmit;

    // time in milliseconds that clients should run for
    long lifetime;

    // parse command line arguments
    {
        char option;
        bool remoteNameInitialized = false;
        bool remotePortInitialized = false;
        bool numWorkerProcessesInitialized = false;
        bool numClientsInitialized = false;
        bool dataInitialized = false;
        bool timesToRetransmitInitialized = false;
        bool lifetimeInitialized = false;
        while ((option = getopt(argc,argv,"h:p:n:c:d:r:t:")) != -1)
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
            case 't':
                {
                    char* parsedCursor = optarg;
                    lifetime = (int) strtol(optarg,&parsedCursor,10);
                    if (parsedCursor == optarg)
                    {
                        fprintf(stderr,"invalid argument for option -%c\n",option);
                    }
                    else
                    {
                        lifetimeInitialized = true;
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

        // post-process user input
        lifetime = lifetimeInitialized ? lifetime : -1;

        // print usage and abort if not all required arguments were provided
        if (!remoteNameInitialized ||
            !remotePortInitialized ||
            !numWorkerProcessesInitialized ||
            !numClientsInitialized ||
            !dataInitialized ||
            !timesToRetransmitInitialized)
        {
            fprintf(stderr,"usage: %s [-h server name] [-p server port] [-n number of worker processes] [-c number of clients] [-d data to send] [-r times to retransmit per client] [-t timeout]\n",argv[0]);
            return EX_USAGE;
        }
    }

    // setup IPC
    printStatsLock = (sem_t*) mmap(0,sizeof(sem_t),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);

    if (printStatsLock == MAP_FAILED)
    {
        fatal_error("mmap");
    }

    if (sem_init(printStatsLock,1,1) < 0)
    {
        fatal_error("sem_init");
    }

    // start the worker processes
    for(register int i = 0; i < numWorkerProcesses; ++i)
    {
        // if this is worker process, run worker process code
        if (fork() == 0)
        {
            if (i == 0)
            {
                return child_process(remoteName,remotePort,(numClients/numWorkerProcesses)+(numClients%numWorkerProcesses),data,timesToRetransmit);
            }
            else
            {
                return child_process(remoteName,remotePort,numClients/numWorkerProcesses,data,timesToRetransmit);
            }
        }
    }
    int returnValue = server_process(numWorkerProcesses,lifetime);

    // tear down IPC
    sem_destroy(printStatsLock);
    munmap(printStatsLock,sizeof(sem_t));

    return returnValue;
}
