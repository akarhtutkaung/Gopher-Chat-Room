/*
 * Author: Akar (Ace) Htut Kaung
 * Email: kaung006@umn.edu
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include "support.h"

server_t server;

bool checkDigit(char num[]){
	if (num[0] == '-'){
		return false;
	}
	for(int i=0; i<strlen(num); i++){
		if(!isdigit(num[i])){
			return false;
		}
	}
	return true;
}

int main(int argc, char *argv[]){
  struct sockaddr_in serverAddr;
  if(argc < 2){
    printf("Usage: \n");
    printf("  ./server [port]\n");
    printf("  ./server reset\n");
    exit(EXIT_FAILURE);
  }
  if(strcmp(argv[1], "reset") == 0){
    reset();
    return 0;
  }
  else if(checkDigit(argv[1]) == false){
    printf("Usage: \n");
    printf("  ./server [port]\n");
    printf("  ./server reset\n");
    exit(EXIT_FAILURE);
  }
  initServer(&server);  // initializing server database
  server.port = atoi(argv[1]);

  //Create a "listening" socket, binds it with the above address/port
  server.sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  if(server.sock_fd < 0){
    Error("[-]Error in creating server socket.\n");
  }
  printf("[+]Server Socket is created.\n");
  SetNonBlockIO(server.sock_fd);

  //Set the server address and port number
  memset(&serverAddr, 0, sizeof(struct sockaddr_in));
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons((unsigned short) server.port);
  serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

  int optval = 1;
  int ret = setsockopt(server.sock_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
	if (ret != 0) {
		Error("[-]Cannot enable SO_REUSEADDR option.\n");
	}
  printf("[+]Enable SO_REUSEADDR option.\n");
  signal(SIGPIPE, SIG_IGN);

  ret = bind(server.sock_fd, (struct sockaddr *)&serverAddr, sizeof(serverAddr));
  if(ret < 0){
    Error("[-]Cannot bind to port %d\n",server.port);
  }
  printf("[+]Bind to port %d\n", server.port);

  if(listen(server.sock_fd, 32) == 0){
    printf("[+]Listening....\n");
  }else{
    Error("[-]Cannot listen to port %d.\n", server.port);
  }

  server.c_clients = 0;
  memset(server.peers, 0, sizeof(server.peers));
  server.peers[0].fd = server.sock_fd;
  server.peers[0].events = POLLRDNORM;
  memset(server.connStat, 0, sizeof(server.connStat));

  // int connID = 0;
  server.clientAddrLen = sizeof(server.clientAddr);

	readCredentials(&server);

  while(1){
    //monitor the listening sock and data socks, nConn+1 in total
    ret = poll(server.peers, server.c_clients + 1, -1);
    if (ret < 0) {
    Error("[-]Invalid poll() return value.\n");
    }
    if(server.sock_fd < 0){
      exit(1);
    }
    //new incoming connection
		if ((server.peers[0].revents & POLLRDNORM) && (server.c_clients < MAX_ONLINE_USER)) {
			int sock_fd = accept(server.sock_fd, (struct sockaddr*)&server.clientAddr, &server.clientAddrLen);
			if (sock_fd != -1) {
				SetNonBlockIO(sock_fd);
				server.c_clients++;
				server.peers[server.c_clients].fd = sock_fd;
				server.peers[server.c_clients].events = POLLRDNORM;
				server.peers[server.c_clients].revents = 0;

				memset(&server.connStat[server.c_clients], 0, sizeof(struct CONN_STAT));
        printf("[+]Connection accepted from %s:%d\n", inet_ntoa(server.clientAddr.sin_addr), ntohs(server.clientAddr.sin_port));
			}
		}
    for (int i=1; i<=server.c_clients; i++) {
		  char * buff = (char *)malloc(MAX_BUFFER_SIZE);
			if (server.peers[i].revents & (POLLRDNORM | POLLERR | POLLHUP)) {
				int fd = server.peers[i].fd;
				int size=0;
				//recv request
				if (server.connStat[i].nRecvS < 4) {
					Recv_NonBlocking(i, fd, (char *)&server.connStat[i].size, 4, &server, NUM); // get the size of incoming data
					if (server.connStat[i].nRecvS == 4) {
            size = server.connStat[i].size;
            if (size <= 0 || size > MAX_BUFFER_SIZE) {
              Error("Invalid size: %d.", size);
            }
          }
				}
				if(server.connStat[i].nRecvD < server.connStat[i].size && server.connStat[i].nRecvS != 0){
					// receive actual data
					Recv_NonBlocking(i, fd, buff, server.connStat[i].size, &server, DATA); // get the actual data
					if(server.connStat[i].nRecvD == server.connStat[i].size){ // if all data r received
						server.connStat[i].nRecvS = 0;
						server.connStat[i].nRecvD=0;
					}
            checkMesg(i, fd, buff, server.connStat[i].size, &server);
				}
			}
		}
  }
  close(server.sock_fd);
  return 0;
}
