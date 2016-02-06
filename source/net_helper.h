#ifndef _NET_HELPER_H_
#define _NET_HELPER_H_

int make_tcp_server_socket(short port, bool isNonBlocking);
int make_tcp_client_socket(char* remoteName, long remoteAddr, short remotePort, short localPort);
struct sockaddr make_sockaddr(char* hostName, long hostAddr, short hostPort);
int read_file(int socket, void* bufferPointer, int bytesToRead);

#endif
