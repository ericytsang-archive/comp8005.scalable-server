/**
 * implementation of the select server.
 *
 * @sourceFile select_svr.cpp
 *
 * @program    select_svr.out
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
#include <strings.h>
#include <sysexits.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include "net_helper.h"
#include "select_helper.h"

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
 * listens to the passed server socket, accepts new connection requests and
 *   services them until application termination.
 *
 * @function   child_process
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
 * @signature  int child_process(int serverSocket)
 *
 * @param      serverSocket server socket on the local host to accept and
 *   service connection requests from.
 *
 * @return     exit code of the process.
 */
int child_process(int serverSocket)
{
    // create selectable files set
    Files files;
    files_init(&files);

    // add server socket to select event loop
    files_add_file(&files,serverSocket);

    // execute select event loop
    while (true)
    {
        // wait for select to unblock to report socket activity
        // wait for an event on any socket to occur
        if(files_select(&files) == -1)
        {
            fatal_error("failed on select");
        }

        // loop through sockets, and handle them
        for(std::set<int>::iterator socketIt = files.fdSet.begin(); socketIt != files.fdSet.end(); ++socketIt)
        {
            int curSock = *socketIt;

            // if this socket doesn't have any activity, move on to next socket
            if(!FD_ISSET(curSock,&files.selectFds))
            {
                continue;
            }

            // handling case when client socket has data available for reading
            if (curSock != serverSocket)
            {
                // read data from socket...
                static char buf[ECHO_BUFFER_LEN];
                register int bytesRead;

                // read and echo back to client
                while ((bytesRead = recv(curSock,buf,ECHO_BUFFER_LEN,0)) > 0)
                {
                    send(curSock,buf,bytesRead,0);
                }

                // if call would block, continue event loop
                if (bytesRead == -1 && errno == EWOULDBLOCK)
                {
                    errno = 0;
                }

                // close socket if connection is closed or unexpected error
                else
                {
                    // close socket & remove from select event loop
                    close(curSock);
                    files_rm_file(&files,curSock);
                }
                continue;
            }

            // handling case when server socket receives a connection request
            else
            {
                // accept the remote connection
                int newSocket = accept(serverSocket,0,0);

                // ignore EAGAIN because this socket is shared, and connection
                // may have been accepted by another process
                if (newSocket == -1 && errno != EAGAIN)
                {
                    fatal_error("accept");
                }

                // propagate error if it is unexpected
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

                // add new socket to select loop
                files_add_file(&files,newSocket);
                continue;
            }
        }
    }
    return EX_OK;
}

/**
 * waits for all child processes to terminate before terminating itself.
 *
 * @function   server_process
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
 * @signature  int server_process(int numWorkerProcesses)
 *
 * @param      numWorkerProcesses number of child processes to wait for before
 *   terminating.
 *
 * @return     exit code of the process.
 */
int server_process(int numWorkerProcesses)
{
    for (register int i = 0; i < numWorkerProcesses; ++i) wait(0);
    return EX_OK;
}

/**
 * main entry point of the application.
 *
 * parses command line arguments, then sets up IPC, and then spawns worker
 *   processes to accept and service new connections until application
 *   termination.
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
