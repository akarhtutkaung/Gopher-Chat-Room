/*
 * Author: Akar (Ace) Htut Kaung
 * Email: kaung006@umn.edu
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/tcp.h>
#include <sys/timeb.h>
#include <fcntl.h>
#include <stdarg.h>
#include <poll.h>
#include <signal.h>

#define MAX_USERNAME 8
#define MIN_USERNAME 4
#define MAX_PASSWORD 8
#define MIN_PASSWORD 4
#define MESSAGE_SIZE 256
#define MAX_CLIENTS 10000
#define MAX_FILE_SIZE 10000000
#define MAX_BUFFER_SIZE 1048576
#define CONCURRENT_FILE_TRANSFER 8
#define MAX_COMMAND 277
#define TRUE 1
#define FALSE -1
#define REGISTER 10
#define LOGIN 20
#define MAX_ONLINE_USER 32
#define NUM 1
#define DATA 2

typedef unsigned int DWORD;
typedef unsigned short WORD;

struct CONN_STAT{
  int size;
  int nRecvS;
  int nRecvD;
  int nSent;
};

typedef struct {
  int fd;                             // sock fd of login client
  char username[MAX_USERNAME];        // username of login client
} login_t;

typedef struct {
   char first[MAX_COMMAND];
} mesg_t;

typedef struct {
  char username[MAX_USERNAME+1];        // username of the client
  char password[MAX_PASSWORD+1];        // password of the client
  char script[MAX_COMMAND];             // script from client if required
  int login;                            // flag indicating the user has login or not
  int sock_fd;                          // socket to communicate with server
} client_t;

// server_t: data pertaining to server operations
typedef struct {
  int port;                             // port to join server
  int sock_fd;                          // main socket fd
  struct sockaddr_in clientAddr;
  struct pollfd peers[MAX_ONLINE_USER+1];
  struct CONN_STAT connStat[MAX_ONLINE_USER+1];
  socklen_t clientAddrLen;
  int n_clients;                        // total number of client data inside the database
  int o_clients;                        // number of online clients
  int c_clients;                        // clients connected to server
  client_t client[MAX_CLIENTS];         // array of clients populated up to n_clients
  login_t login[MAX_ONLINE_USER];       // login clients database
} server_t;

// server_funcs.c
void SetNonBlockIO(int fd);
void initServer(server_t *server);
void reset();
void Error(const char * format, ...) ;
void Log(const char * format, ...);
int Send_NonBlocking(int i, int sockFD, char * data, int len, server_t *server);
int Recv_NonBlocking(int i, int sockFD, char * data, int len, server_t *server, int choice);
void readCredentials(server_t *server);
void RemoveConnection(int i, server_t *server);
void removeLogin(int i, server_t *server);
void regNotice(int i, int ret, int sock_fd, server_t *server);
void loginNotice(int i, int ret, int sock_fd, server_t *server);
char* getWord(char *buf, int num);
int registerAccount(char *buf, server_t *server);
int login(int i, char *buf,int sock_fd, server_t *server);
void logout();
void sendMessagePublic(char *buf, int i, int fd, server_t *server);
void sendMessagePrivate(char *buf, int i, int fd, server_t *server);
void sendFilePublic(int i, int fd, char *buf, int size, server_t *server);
void sendFilePrivate(int i, int fd, char *buf, int size, server_t *server);
void list(int i, int fd, server_t *server);
int checkMesg(int i, int fd, char *buf, int size, server_t *server);
