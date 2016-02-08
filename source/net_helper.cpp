#include "net_helper.h"

#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#define LISTENQ 2048

static void fatal_error(const char* errstr);

/**
 * creates a new server socket, and returns the new server socket's file
 *   descriptor.
 *
 * @function   make_tcp_server_socket
 *
 * @date       2015-03-05
 *
 * @revision   none
 *
 * @designer   Eric Tsang
 *
 * @programmer Eric Tsang
 *
 * @note       none
 *
 * @signature  int make_tcp_server_socket(short port)
 *
 * @param      port port number on local host to bind the new server socket to.
 *
 * @param      isNonBlocking true if the socket should be put into non blocking
 *   mode; false otherwise.
 *
 * @return     socket file descriptor to the new server socket. may return -1 on
 *   binding error.
 */
struct socket_t make_tcp_server_socket(short port, bool isNonBlocking)
{
    // local address that server socket is bound to
    struct sockaddr localAddr;
    // socket file descriptor to the new server socket
    int svrSock;

    // create TCP socket
    if((svrSock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        fatal_error("failed to create TCP socket");
    }

    // set sock opt to reuse address
    int arg = 1;
    if(setsockopt(svrSock,SOL_SOCKET,SO_REUSEADDR,&arg,sizeof(arg)) == -1)
    {
        fatal_error("failed to set sock opt to reuse address");
    }

    // set sock opt to not linger after close
    {
        struct linger linger;
        memset(&linger,0,sizeof(linger));
        linger.l_onoff = 1;
        linger.l_linger = 0;
        if (setsockopt(svrSock,SOL_SOCKET,SO_LINGER,(char*) &linger,sizeof(linger)) == -1)
        {
            fatal_error("failed to set sock opt to not linger");
        }
    }

    // make the server listening socket non-blocking
    if (isNonBlocking)
    {
        int existingFlags = fcntl(svrSock,F_GETFL,0);
        if (existingFlags == -1 || fcntl(svrSock,F_SETFL,O_NONBLOCK|existingFlags) == -1)
        {
            fatal_error("fcntl");
        }
    }

    // bind server socket to local host
    localAddr = make_sockaddr(0, INADDR_ANY, port);
    if(bind(svrSock, (struct sockaddr*) &localAddr, sizeof(localAddr)) == -1)
    {
        perror("failed to bind server socket to address structure");
        close(svrSock);
        svrSock = -1;
    }

    // put server socket into listening mode
    if (listen(svrSock, LISTENQ) == -1)
    {
        fatal_error("listen");
    }

    // return...
    struct socket_t socket;
    memset(&socket,0,sizeof(socket));
    socket.fd = svrSock;
    socket.localAddr = localAddr;
    return socket;
}

/**
 * craetes a new socket that is connected to the specified remote host.
 *
 * @function   make_tcp_client_socket
 *
 * @date       2015-03-05
 *
 * @revision   none
 *
 * @designer   Eric Tsang
 *
 * @programmer Eric Tsang
 *
 * @note       none
 *
 * @signature  int make_tcp_client_socket(char* remoteName, long remoteAddr,
 *   short remotePort, short localPort)
 *
 * @param      remoteName name of the remote host. either this, or {remoteAddr}
 *   needs to be specified; one of them can be 0, but not both.
 * @param      remoteAddr address in host byte ordering of the remote host.
 *   either this, or {remoteName} needs to be specified; one of them can be 0,
 *   but not both.
 * @param      remotePort the remote host's port.
 * @param      localPort the local port. can be 0 if you don't care.
 * @param      isNonBlocking true if the socket should be put into non blocking
 *   mode; false otherwise.
 *
 * @return     socket file descriptor to the new connected client socket. may
 *   return -1 on error.
 */
struct socket_t make_tcp_client_socket(char* remoteName, long remoteAddr, short remotePort, short localPort, bool isNonBlocking)
{
    // local address that client socket is bound to
    struct sockaddr local;
    // remote address that client socket should connect to
    struct sockaddr remote;
    // socket file descriptor to the new client socket
    int clntSock;

    // create TCP socket
    if((clntSock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        fatal_error("failed to create TCP socket");
    }

    // set sock opt to reuse address
    {
        int arg = 1;
        if(setsockopt(clntSock,SOL_SOCKET,SO_REUSEADDR,&arg,sizeof(arg)) == -1)
        {
            fatal_error("failed to set sock opt to reuse address");
        }
    }

    // set sock opt to not linger after close
    {
        struct linger linger;
        memset(&linger,0,sizeof(linger));
        linger.l_onoff = 1;
        linger.l_linger = 0;
        if (setsockopt(clntSock,SOL_SOCKET,SO_LINGER,(char*) &linger,sizeof(linger)) == -1)
        {
            fatal_error("failed to set sock opt to not linger");
        }
    }

    // bind socket to local host if a local port is specified
    if(clntSock > 0 && localPort)
    {
        local = make_sockaddr(0, INADDR_ANY, localPort);
        if(bind(clntSock, (struct sockaddr*) &local, sizeof(local)) == -1)
        {
            perror("failed to bind socket to local host");
            close(clntSock);
            clntSock = -1;
        }
    }

    // make the server listening socket non-blocking if specified
    if (clntSock > 0 && isNonBlocking)
    {
        int existingFlags = fcntl(clntSock, F_GETFL,0);
        if (existingFlags == -1 || fcntl(clntSock, F_SETFL, O_NONBLOCK | existingFlags) == -1)
        {
            fatal_error("failed to make socket non-blocking");
        }
    }

    // connect socket to remote host
    remote = make_sockaddr(remoteName, remoteAddr, remotePort);
    if (clntSock > 0 && connect(clntSock, (struct sockaddr*) &remote, sizeof(remote)) == -1)
    {
        // check for fatal error. ignore EINPROGRESS error if socket is
        // non-blocking because this is expected.
        if (isNonBlocking && errno == EINPROGRESS)
        {
            errno = 0;
        }

        // propagate error otherwise
        else
        {
            perror("failed to connect to remote host");
            close(clntSock);
            clntSock = -1;
        }
    }

    // return...
    struct socket_t socket;
    memset(&socket,0,sizeof(socket_t));
    socket.fd = clntSock;
    socket.localAddr = local;
    socket.remoteAddr = remote;
    return socket;
}

/**
 * makes a address structure. the hostPort must be provided. if {hostName} is
 *   specified, the function will perform a query to find the remote IP. if
 *   {hostName} is not provided, then the IP address used will be {hostAddr}.
 *
 * @function   make_sockaddr
 *
 * @date       2015-03-05
 *
 * @revision   none
 *
 * @designer   Eric Tsang
 *
 * @programmer Eric Tsang
 *
 * @note       none
 *
 * @signature  struct sockaddr make_sockaddr(char* hostName, long hostAddr, short hostPort)
 *
 * @param      hostName optional. name of the remote host. either this, or
 *   {hostAddr} must be specified.
 * @param      hostAddr optional. IP address of host. either this or {hostName}
 *   must be specified.
 * @param      hostPort the host's port number.
 *
 * @return     sockaddr structure with the provided {hostPort}, and appropriate
 *   IP address. may return 0 on error.
 */
struct sockaddr make_sockaddr(char* hostName, long hostAddr, short hostPort)
{
    // address that client socket should connect to
    struct sockaddr_in addr;

    // set up port and protocol of address structure
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(hostPort);

    if(hostName == 0)
    {
        /*
         * hostName is not provided; just use {hostAddr} as address...
         */

        // set address structure's address
        addr.sin_addr.s_addr = htonl(hostAddr);
    }
    else
    {
        /*
         * hostName is provided; do query for host then result as address...
         */

        // resolve host (if needed)
        struct hostent* host;
        if((host = gethostbyname(hostName)) == 0)
        {
            herror("failed to resolve host");
            struct sockaddr ret;
            memset(&ret,0,sizeof(ret));
            return ret;
        }

        // set address structure's address from query results
        memcpy(&addr.sin_addr, host->h_addr, host->h_length);
    }

    // return...
    {
        struct sockaddr ret;
        memcpy(&ret, &addr, sizeof(ret));
        return ret;
    }
}

/**
 * reads from a socket, and returns when the read finishes, EOF occurs, or an
 *   error is thrown.
 *
 * @function   read_file
 *
 * @date       2015-03-05
 *
 * @revision   none
 *
 * @designer   Eric Tsang
 *
 * @programmer Eric Tsang
 *
 * @note       none
 *
 * @signature  int read_file(int socket, void* buffer, int bytesToRead)
 *
 * @param      socket socket file descriptor.
 * @param      buffer pointer to a buffer to read data from the socket into.
 * @param      bytesToRead number of bytes to read from socket into buffer.
 *
 * @return     non-zero if the read was successful, 0 on when the socket closes,
 *   and -1 on error.
 */
int read_file(int socket, void* bufferPointer, int bytesToRead)
{
    int bytesRead;
    int result = 0;
    char* bufPtr = (char*) bufferPointer;

    // read message from socket
    while(bytesToRead > 0
        && (bytesRead = read(socket,bufPtr,bytesToRead)) > 0)
    {
        bufPtr += bytesRead;
        bytesToRead -= bytesRead;
        result += bytesRead;
    }

    return result;
}

/**
 * prints the error message, then exits the program.
 *
 * @function   fatal_error
 *
 * @date       2015-03-22
 *
 * @revision   none
 *
 * @designer   EricTsang
 *
 * @programmer EricTsang
 *
 * @note       none
 *
 * @signature  static void fatal_error(const char* errstr)
 *
 * @param      errstr string to print before exiting the program
 */
static void fatal_error(const char* errstr)
{
    perror(errstr);
    exit(errno);
}
