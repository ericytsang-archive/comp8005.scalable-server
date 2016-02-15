/**
 * implementation of the multi-threaded server.
 *
 * @sourceFile thread_svr.cpp
 *
 * @program    thread_svr.out
 *
 * @date       2016-02-14
 *
 * @revision   none
 *
 * @designer   Eric Tsang
 *
 * @programmer Eric Tsang
 *
 * @note       none
 */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <strings.h>
#include <sysexits.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include "net_helper.h"
#include "Semaphore.h"

/**
 * size of buffer used to read bytes into from TCP/IP sockets.
 */
#define ECHO_BUFFER_LEN 1024

/**
 * prints the error message, then exits the program.
 *
 * @function   fatal_error
 *
 * @date       2016-02-14
 *
 * @revision   none
 *
 * @designer   EricTsang
 *
 * @programmer EricTsang
 *
 * @note       none
 *
 * @signature  void fatal_error(const char* string)
 *
 * @param      string string to print before exiting the program
 */
void fatal_error(char const * string)
{
    fprintf(stderr,"%s: ",string);
    perror(0);
    exit(EX_OSERR);
}

/**
 * a pointer of this structure is passed as the parameter for the thread running
 *   worker_routine.
 */
struct WorkerRoutineParams
{
    Semaphore* postOnAcceptPtr;
    int* serverSocketPtr;
};

/**
 * thread routine that accepts a connection from the server socket, and services
 *   it. once the connection closes, the thread terminates.
 *
 * @function   worker_routine
 *
 * @date       2016-02-14
 *
 * @revision   none
 *
 * @designer   Eric Tsang
 *
 * @programmer Eric Tsang
 *
 * @note       none
 *
 * @signature  void* worker_routine(void* voidParams)
 *
 * @param      voidParams pointer to a WorkerRoutineParams structure.
 */
void* worker_routine(void* voidParams)
{
    WorkerRoutineParams* params = (WorkerRoutineParams*) voidParams;
    int serverSocket = *(params->serverSocketPtr);
    char buf[ECHO_BUFFER_LEN];
    int clntSock;

    // accept a client
    while (true)
    {
        // accept the remote connection
        if ((clntSock = accept(serverSocket,0,0)) >= 0)
        {
            break;
        }

        // ignore EAGAIN because this socket is shared, and connection
        // may have been accepted by another process
        if (errno == EAGAIN)
        {
            errno = 0;
        }

        // propagate error if it is unexpected
        else
        {
            fatal_error("accept");
        }
    }

    // connection established; post
    params->postOnAcceptPtr->post();

    // read and echo back to client
    register int bytesRead;
    while ((bytesRead = recv(clntSock,buf,ECHO_BUFFER_LEN,0)) > 0)
    {
        send(clntSock,buf,bytesRead,0);
    }

    // if socket is closed, close socket
    if (bytesRead == 0 || errno == ECONNRESET)
    {
        close(clntSock);
        errno = 0;
    }

    // else unexpected error, die
    else
    {
        fatal_error("recv");
    }

    pthread_exit(0);
}

/**
 * the main entry point to the application.
 *
 * parses command line arguments, opens the server socket, sets up IPC and
 *   continuously tops off the idle thread pool until SIGINT is received.
 *
 * @function   main
 *
 * @date       2016-02-14
 *
 * @revision   none
 *
 * @designer   Eric Tsang
 *
 * @programmer Eric Tsang
 *
 * @note       none
 *
 * @signature  int main (int argc, char* argv[])
 *
 * @param      argc number of command line arguments.
 * @param      argv array of c-style strings.
 *
 * @return     exit code of the application.
 */
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
                        fprintf(stderr,"invalid argument for option -%c\n",option);
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
                        fprintf(stderr,"invalid argument for option -%c\n",option);
                    }
                    else
                    {
                        numWorkerProcessesInitialized = true;
                    }
                    break;
                }
            case '?':
                {
                    if (isprint(optopt))
                    {
                        fprintf(stderr,"unknown option \"-%c\".\n",optopt);
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
    serverSocket = make_tcp_server_socket(listeningPort,false).fd;
    if (serverSocket == -1)
    {
        fatal_error("socket");
    }

    // setup IPC
    Semaphore postOnAccept(false,numWorkerProcesses);

    // setup worker routine parameters
    WorkerRoutineParams workerRoutineParams;
    workerRoutineParams.serverSocketPtr = &serverSocket;
    workerRoutineParams.postOnAcceptPtr = &postOnAccept;

    // start the worker processes
    while (true)
    {
        postOnAccept.wait();
        pthread_t thread;
        if (pthread_create(&thread,0,worker_routine,&workerRoutineParams) != 0)
        {
            fatal_error("pthread_create");
        }
        pthread_detach(thread);
    }
    return EX_OK;
}
