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
#include "support.h"

// Setting non block io
void SetNonBlockIO(int fd) {
	int val = fcntl(fd, F_GETFL, 0);
	if (fcntl(fd, F_SETFL, val | O_NONBLOCK) != 0) {
		Error("Cannot set nonblocking I/O.");
	}
}

// Initializing server
void initServer(server_t *server){
	server->port = 0;
	server->sock_fd = 0;
	server->n_clients = 0;
}

// Resetting server (delete all database of users)
void reset(){
	FILE *fp;
	fp = fopen("credentials.txt","w");
	fclose(fp);
	printf("Server reset successfully!\n");
}

// print out error
void Error(const char * format, ...) {
	char msg[4096];
	va_list argptr;
	va_start(argptr, format);
	vsprintf(msg, format, argptr);
	va_end(argptr);
	fprintf(stderr, "Error: %s\n", msg);
	exit(-1);
}

// Log if something wrong
void Log(const char * format, ...) {
	char msg[2048];
	va_list argptr;
	va_start(argptr, format);
	vsprintf(msg, format, argptr);
	va_end(argptr);
	fprintf(stderr, "%s\n", msg);
}

// Send data to client
int Send_NonBlocking(int i, int sockFD, char * data, int len, server_t *server) {
	//pStat keeps tracks of how many bytes have been sent, allowing us to "resume"
	//when a previously non-writable socket becomes writable.
	while (server->connStat[i].nSent < len) {
		int n = send(sockFD, data + server->connStat[i].nSent, len - server->connStat[i].nSent, 0);
		if (n >= 0) {
			server->connStat[i].nSent += n;
		} else if (n < 0 && (errno == ECONNRESET || errno == EPIPE)) {
			logout(i, sockFD, server);
			return -1;
		} else if (n < 0 && (errno == EWOULDBLOCK)) {
			//The socket becomes non-writable. Exit now to prevent blocking.
			//OS will notify us when we can write
			server->peers[i].events |= POLLWRNORM;
			return 0;
		} else {
			Error("Unexpected send error %d: %s", errno, strerror(errno));
		}
	}
	server->peers[i].events &= ~POLLWRNORM;
	server->connStat[i].nSent = 0;
	return 0;
}

// Receive data from client
int Recv_NonBlocking(int i, int sockFD, char * data, int len, server_t *server, int choice) {
	//pStat keeps tracks of how many bytes have been rcvd, allowing us to "resume"
	//when a previously non-readable socket becomes readable.
	if(choice == NUM){  // if the receiving data is the size of upcoming data
		while (server->connStat[i].nRecvS < len) {
			int n = recv(sockFD, data + server->connStat[i].nRecvS, len - server->connStat[i].nRecvS, 0);
			if (n > 0) {
				server->connStat[i].nRecvS += n;
			} else if (n == 0 || (n < 0 && errno == ECONNRESET)) {
				logout(i, sockFD, server);
				return -1;
			} else if (n < 0 && (errno == EWOULDBLOCK)) {
				//The socket becomes non-readable. Exit now to prevent blocking.
				//OS will notify us when we can read
				return 0;
			} else {
				Error("Unexpected recv error %d: %s.", errno, strerror(errno));
			}
	  }
	}
	else if(choice == DATA){ // if the receiving data is actual data
		while (server->connStat[i].nRecvD < len) {
			int n = recv(sockFD, data + server->connStat[i].nRecvD, len - server->connStat[i].nRecvD, 0);
			if (n > 0) {
				server->connStat[i].nRecvD += n;
			}else if (n == 0 || (n < 0 && errno == ECONNRESET)) {
				logout(i, sockFD, server);
				return -1;
			} else if (n < 0 && (errno == EWOULDBLOCK)) {
				//The socket becomes non-readable. Exit now to prevent blocking.
				//OS will notify us when we can read
				return 0;
			} else {
				Error("Unexpected recv error %d: %s.", errno, strerror(errno));
			}
		}
	}
	return 0;
}

// Read user credentials from stored database
void readCredentials(server_t *server){
	FILE *fp;
	int n_clients = server->n_clients;
	char *line = NULL;
	size_t len = 0;
	ssize_t nread;

	fp = fopen("credentials.txt", "a+");
	while ((nread = getline(&line, &len, fp)) != -1) {
		char *token = strtok(line," ");
		strcpy(server->client[n_clients].username,token);
		token = strtok(NULL, " ");
		strcpy(server->client[n_clients].password,token);
		server->client[n_clients].password[strlen(server->client[n_clients].password)] = '\0';
		server->client[n_clients].password[strlen(server->client[n_clients].password)-1] = '\0';
		server->client[n_clients].login = FALSE;
		n_clients++;
	}
	server->n_clients = n_clients;
	fclose(fp);
	free(line);
}

// Remove connection from the server
void RemoveConnection(int i,server_t *server) {
	if (i < server->c_clients) {
		memmove(server->peers + i, server->peers + i + 1, (server->c_clients-i) * sizeof(struct pollfd));
		memmove(server->connStat + i, server->connStat + i + 1, (server->c_clients-i) * sizeof(struct CONN_STAT));
	}
	server->c_clients--;
}

// Remove login connection from the server
void removeLogin(int i, server_t *server){
	for(int a = i; a<server->o_clients; a++){
		server->login[a].fd = server->login[a+1].fd;
		strcpy(server->login[a].username, server->login[a+1].username);
	}
	server->o_clients--;
}

// Register notice, whether successful or not
void regNotice(int i, int ret, int sock_fd, server_t *server){
	char msg[MAX_COMMAND];
	if(ret == FALSE){
		strcpy(msg, "ELSE [-] Register unsuccessful...");
		printf("[-] Register attempt...unsuccessful...\n");
		int size = strlen(msg);
		Send_NonBlocking(i, sock_fd, (char *)&size, sizeof(size), server );
		Send_NonBlocking(i, sock_fd, msg, strlen(msg), server);
	} else {
		// register successful
		strcpy(msg, "ELSE [+] Register successful!");
		int size = strlen(msg);
		Send_NonBlocking(i, sock_fd, (char *)&size, sizeof(size), server );
		Send_NonBlocking(i, sock_fd, msg, strlen(msg), server);
	}
	server->connStat[i].nSent = 0;
}

// Login notice, whether successful or not
void loginNotice(int i, int ret, int sock_fd, server_t *server){
	char msg[MAX_COMMAND];
	if(ret == FALSE){
		strcpy(msg, "LOGIN [-] Login Unsuccessful...");
		printf("message to send: %s\n",msg);
		int size = strlen(msg);
		Send_NonBlocking(i, sock_fd, (char *)&size, sizeof(size), server );
		Send_NonBlocking(i, sock_fd, msg, strlen(msg), server);
	} else {
		// register successful
		strcpy(msg, "LOGIN [+] Login Successful!");
		int size = strlen(msg);
		Send_NonBlocking(i, sock_fd, (char *)&size, sizeof(size), server );
		Send_NonBlocking(i, sock_fd, msg, strlen(msg), server);
	}
	server->connStat[i].nSent = 0;
}

// Get the word from the sentence
char* getWord(char *buf, int num){
	char* token = strtok(buf, " ");
	int count = 0;
	while(token != NULL){
		token = strtok(NULL, " ");
		if(count == 0 && num == count+1){
			return token;
		}
		else if(count == 1 && num == count+1){
			return token;
		}
		count++;
	}
	return NULL;
}

// Account registration
int registerAccount(char *buf, server_t *server){
	char username[MAX_USERNAME+1];
	char password[MAX_PASSWORD+1];
	char temp[MAX_COMMAND];
	strcpy(temp, buf);
	strcpy(username, getWord(buf, 1));
	strcpy(password, getWord(temp, 2));
	if(server->n_clients < MAX_CLIENTS){        // If number of clients exceed database
		for(int i=0; i<server->n_clients; i++){   // If username exist
			if(strcmp(username,server->client[i].username) == 0){
				return FALSE;
			}
		}
		// if username not taken
		strcpy(server->client[server->n_clients].username, username);
		strcpy(server->client[server->n_clients].password, password);
		server->n_clients++;
		server->client[server->n_clients].login = FALSE;
		printf("\n[^] New account created\n");
		printf("[^] Username          : %s\n", username);
		printf("[^] Password          : %s\n", password);
		printf("[^] Number of clients : %d\n", server->n_clients);

		// save the user credentials inside database
		FILE *fp;
		fp = fopen("credentials.txt", "a+");
		fprintf(fp, "%s %s\n",username, password);
		fflush(fp);
		fclose(fp);
		return TRUE;
	}
return FALSE;
}

// Check whether to give login access or not
int login(int i, char *buf,int sock_fd, server_t *server){
	char username[MAX_USERNAME+1];
	char password[MAX_PASSWORD+1];
	char temp[MAX_COMMAND+1];
	char msg[MAX_COMMAND+1];
	char msgTemp[MAX_COMMAND+1];
	strcpy(temp, buf);
	strcpy(username, getWord(buf, 1));
	strcpy(password, getWord(temp, 2));

	for(int a=0; a<server->n_clients; a++){
		// check if username exist inside the user credentials database
		if(strcmp(username, server->client[a].username) == 0){
			// check if password match
			if(strcmp(password, server->client[a].password) == 0){
				if(server->client[a].login == TRUE){ // if the account is already login somewhere
				return FALSE;
				}
				else{																// login successful
					server->client[a].sock_fd = sock_fd;
					server->client[a].login = TRUE;
					server->login[server->o_clients].fd = sock_fd;
					strcpy(server->login[server->o_clients].username, username);
					server->o_clients++;
					printf("----- %s join the chat -----\n",username);
					sprintf(msgTemp,"----- %s join the chat -----",username);
					strcpy(msg, "ELSE ");
					strcat(msg,msgTemp);
					for(int a=0; a<server->o_clients; a++){
						if(server->login[a].fd != sock_fd){
							// send notice to all online users that new user has join the chat
							int size = strlen(msg);
							Send_NonBlocking(i, server->login[a].fd, (char *)&size, sizeof(size), server );
							Send_NonBlocking(i, server->login[a].fd, msg, strlen(msg), server);
						}
					}
					return TRUE;
				}
			}
		}
	}
	return FALSE;
}

// Logout from the account
void logout(int i, int fd, server_t *server){
	char username[MAX_USERNAME];
	char msg[MAX_COMMAND+1];
	char msgTemp[MAX_COMMAND+1];
	for(int a=0; a<server->o_clients; a++){
		if(server->login[a].fd == fd){
			// set the user as not login
			strcpy(username,server->login[a].username);
			removeLogin(a, server);
		}
	}
	for(int a=0; a<server->n_clients; a++){
		if(strcmp(server->client[a].username, username) == 0 ){
			server->client[a].login = FALSE;
			printf("----- %s left the chat -----\n",server->client[a].username);
			sprintf(msgTemp,"----- %s left the chat -----",username);
			strcpy(msg, "ELSE ");
			strcat(msg,msgTemp);
			for(int a=0; a<server->o_clients; a++){
				// send to all online users that the user has left the chat
				int size = strlen(msg);
				Send_NonBlocking(i, server->login[a].fd, (char *)&size, sizeof(size), server );
				Send_NonBlocking(i, server->login[a].fd, msg, strlen(msg), server);
			}
		}
	}
	RemoveConnection(i, server);
	close(fd);
}

// Sending message to public
void sendMessagePublic(char *buf, int i, int fd, server_t *server){
	char msg_actual[MESSAGE_SIZE + 16];
	char *msg = msg_actual;
	char display[MESSAGE_SIZE + 12];
	char temp[MAX_USERNAME+5];
	for(int a=MESSAGE_SIZE+12; a>= 0; a--){
		display[a] = '\0';
	}
	strcpy(msg, "ELSE ");
	char* token = strtok(buf, " ");
	if(strcmp(token, "SEND") == 0){
		//add username to message
		int b;
		for(b = 0; b<server->o_clients; b++){
			if(fd == server->login[b].fd){
				break; // if the username is found
			}
		}
		sprintf(temp,"[%s]:",server->login[b].username);
		strcat(display, temp);
	}
	else if(strcmp(token, "SENDA") == 0){
		//add anonymous to message
		strcat(display, "[Anonymous]:");
	}
	while(token != NULL){ // add all the rest of the message
		token = strtok(NULL, " ");
		if(token!=NULL){
			strcat(display," ");
			strcat(display,token);
		}
	}
	printf("%s\n",display);
	strcat(msg, display);
	for(int a=0; a<server->o_clients; a++){
		if(server->login[a].fd != fd){
			// send the message to all other online users
			int size = strlen(msg);
			Send_NonBlocking(i, server->login[a].fd, (char *)&size, sizeof(size), server );
			Send_NonBlocking(i, server->login[a].fd, msg, strlen(msg), server);
		}
	}
}

// Sending message privately
void sendMessagePrivate(char *buf, int i,int fd, server_t *server){
	char msg_actual[MAX_COMMAND];
	char *msg = msg_actual;
	char bufTemp[MAX_COMMAND];
	char display[MAX_COMMAND];
	char temp[MAX_COMMAND];
	char userToSend[MAX_USERNAME];
	int a;
	for(int b=MAX_COMMAND; b>= 0; b--){
		display[b] = '\0';
	}
	strcpy(bufTemp,buf);
	strcpy(msg, "ELSE ");

	char* token = strtok(bufTemp, " ");
	if(strcmp(token, "SEND2") == 0){
	//add username to message
		int b;
		for(b = 0; b<server->o_clients; b++){
			if(fd == server->login[b].fd){
				break; // if the username is found
			}
		}
		sprintf(temp,"[%s|Private]",server->login[b].username);
		strcat(display, temp);
		strcat(msg, temp);
		strcat(msg,":");
	}
	else if(strcmp(token, "SENDA2") == 0){
		//add anonymous to message
		strcat(display, "[Anonymous|Private]");
		strcat(msg, "[Anonymous|Private]:");
	}
	strcpy(userToSend,strtok(NULL, " ")); // store username to send
	int ret = FALSE;
	for(a=0; a<server->o_clients; a++){
		if(strcmp(server->login[a].username,userToSend) == 0){
			ret = TRUE;
			break;
		}
	}
	if(ret == TRUE){
		char to[MAX_COMMAND];
		sprintf(to,"->[%s]:",userToSend);	// so that server know who send who privately
		strcat(display,to);
		while(token != NULL){ // add all the rest of the message
			token = strtok(NULL, " ");
			if(token!=NULL){
				strcat(msg," ");
				strcat(msg,token);
				strcat(display," ");
				strcat(display,token);
			}
		}
		if(fd != server->login[a].fd){
			// send message to specified user
			printf("%s\n",display);
			int size = strlen(msg);
			Send_NonBlocking(i, server->login[a].fd, (char *)&size, sizeof(size), server );
			Send_NonBlocking(i, server->login[a].fd, msg, strlen(msg), server);
		}
	}
	else{
		// if the specified user is not online or not inside the databse
		strcpy(msg,"ELSE [!] The username is invalid or doesn't exist.");
		int size = strlen(msg);
		Send_NonBlocking(i, fd, (char *)&size, sizeof(size), server );
		Send_NonBlocking(i, fd, msg, strlen(msg), server);
	}
}

// Sending file to public
void sendFilePublic(int i, int fd, char *buf, int size, server_t *server){
	for(int a=0; a<server->o_clients; a++){
		if(fd != server->login[a].fd){
			// Send the file to everyone
			Send_NonBlocking(a, server->login[a].fd, (char *)&size, 4, server);
			Send_NonBlocking(a, server->login[a].fd, buf, strlen(buf), server);
		}
	}
}

// Sending file privately
void sendFilePrivate(int i, int fd, char *buf, int size, server_t *server){
	char full[MAX_BUFFER_SIZE];
	strcpy(full,buf);
	char* token = strtok(full, " ");
	token = strtok(NULL, " ");
	for(int a=0; a<server->o_clients; a++){
		if(strcmp(server->login[a].username,token) == 0){
			// send file to specified user
			Send_NonBlocking(a, server->login[a].fd, (char *)&size, 4, server);
			Send_NonBlocking(a, server->login[a].fd, buf, strlen(buf), server);
			break;
		}
	}
}

// List out all current online clients
void list(int i, int fd, server_t *server){
	char msg[MAX_COMMAND];
	char temp[MAX_COMMAND];
	strcpy(msg,"");
	printf("Online users: %d\n",server->o_clients);
	strcpy(msg,"LIST");
	for(int a=0; a<server->o_clients; a++){
		sprintf(temp," %s",server->login[a].username);
		strcat(msg,temp);
	}
	int size = strlen(msg);
	// send the list of online users
	Send_NonBlocking(i, fd, (char *)&size, sizeof(size), server );
	Send_NonBlocking(i, fd, msg, strlen(msg), server);
}

// Check message from client and determine what to do next
int checkMesg(int i, int fd, char *buf, int size, server_t *server){
	int ret;
	char * full = (char *)malloc(MAX_BUFFER_SIZE);
	char * check = (char *)malloc(MAX_BUFFER_SIZE);
	strcpy(full, buf);
	strcpy(check, buf);
	strtok(check, " ");
	if(strcmp(check, "REGISTER") == 0){
		// account registration
		ret = registerAccount(full, server);
		regNotice(i, ret, fd, server);
	}
	else if(strcmp(check, "LOGIN") == 0){
		// to login
		ret = login(i, full, fd, server);
		loginNotice(i, ret, fd, server);
	}
	else if(strcmp(buf, "LOGOUT") == 0){
		// to logout
		logout(i,fd, server);
		return 1;
	}
	else if (strcmp(check, "SEND") == 0 || strcmp(check, "SENDA") == 0){
		// send message to public
		sendMessagePublic(buf, i, fd, server);
	}
	else if (strcmp(check, "SEND2") == 0 || strcmp(check, "SENDA2") == 0){
		// send message privately
		sendMessagePrivate(buf, i, fd, server);
	}
	else if(strcmp(check, "SENDF") == 0){
		// send file to public
		sendFilePublic(i, fd, buf, size, server);
	}
	else if(strcmp(check, "SENDF2") == 0){
		// send file privately
		sendFilePrivate(i, fd, buf, size, server);
	}
	else if(strcmp(buf, "LIST") == 0){
		// list out all online users
		list(i,fd,server);
	}
	return 1;
}
