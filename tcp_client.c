/*
 * Author: Akar (Ace) Htut Kaung
 * Email: kaung006@umn.edu
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/stat.h>
#include <ctype.h>
#include <signal.h>
#include "support.h"

client_t client;
struct pollfd notice[1];
struct CONN_STAT connStat;

char user_input_data[MAX_COMMAND];
char tempUsername[MAX_USERNAME];
char fileNameMain[MAX_COMMAND];
FILE *logFp;
int logInfo = FALSE;

pthread_t user_thread;
pthread_t server_thread;

// print out error
void cError(const char * format, ...) {
	char msg[4096];
	va_list argptr;
	va_start(argptr, format);
	vsprintf(msg, format, argptr);
	va_end(argptr);
	fprintf(stderr, "Error: %s\n", msg);
	exit(-1);
}

// Reset the message database.
void reset_mesg(mesg_t *server_send_data){
	for(int i=MAX_COMMAND; i>= 0; i--){
		server_send_data->first[i] = '\0';
	}
}

// Split the message from server into words.
void split_serv_mesg(char *buf, mesg_t *server_send_data){
  char* token = strtok(buf, " ");
  reset_mesg(server_send_data);
  while(token != NULL){
    token = strtok(NULL, " ");
		if(token != NULL){
		  strcat(server_send_data->first, token);
      strcat(server_send_data->first, " ");
		}
  }
}

// Set non block io.
void client_SetNonBlockIO(int fd) {
	int val = fcntl(fd, F_GETFL, 0);
	if (fcntl(fd, F_SETFL, val | O_NONBLOCK) != 0) {
		cError("Cannot set nonblocking I/O.");
	}
}

// Send data to server.
int client_Send_NonBlocking(int sockFD, char * data, int len) {
	while (connStat.nSent < len) {
		int n = send(sockFD, data + connStat.nSent, len - connStat.nSent, 0);
    if (n >= 0) {
			connStat.nSent += n;
    }
    else if (n < 0 && (errno == ECONNRESET || errno == EPIPE)) {
      cError("[-] Connection closed.\n");
      close(sockFD);
	    exit(EXIT_FAILURE);
    }
    else if (n < 0 && (errno == EWOULDBLOCK)) {
      //The socket becomes non-writable. Exit now to prevent blocking.
      //OS will notify us when we can write
			notice->events |= POLLWRNORM;
			connStat.nSent = 0;
      return 0;
    }
    else {
      cError("[-] Unexpected error %d: %s.\n", errno, strerror(errno));
	    exit(EXIT_FAILURE);
    }
  }
  notice->events &= ~POLLWRNORM;
  connStat.nSent = 0;
  return TRUE;
}

// Receive data from server.
int client_Recv_NonBlocking(int sockFD, char * data, int len, int choice) {
	if(choice == NUM){ // if the receiving data is the size of upcoming data
		while (connStat.nRecvS < len){
			int n = recv(sockFD, data + connStat.nRecvS, len - connStat.nRecvS, 0);
	    if (n > 0) {
	      connStat.nRecvS += n;
	    } else if (n == 0 || (n < 0 && errno == ECONNRESET)) {
          cError("[-] Connection closed from server.\n");
          close(sockFD);
          exit(EXIT_FAILURE);
      } else if (n < 0 && (errno == EWOULDBLOCK)) {
          return TRUE;
      } else {
          cError("[-] Unexpected error %d: %s.\n", errno, strerror(errno));
          exit(EXIT_FAILURE);
      }
		}
	}
	else if(choice == DATA){ // if the receiving data is actual data
		while (connStat.nRecvD < len) {
      int n = recv(sockFD, data + connStat.nRecvD, len - connStat.nRecvD, 0);
      if (n > 0) {
        connStat.nRecvD += n;
      } else if (n == 0 || (n < 0 && errno == ECONNRESET)) {
          cError("[-] Connection closed from server.\n");
          close(sockFD);
          exit(EXIT_FAILURE);
      } else if (n < 0 && (errno == EWOULDBLOCK)) {
          return TRUE;
      } else {
          cError("[-] Unexpected error %d: %s.\n", errno, strerror(errno));
          exit(EXIT_FAILURE);
      }
    }
	}
	return TRUE;
}

// Check if the input doesn't have special characters
bool checkInput(char *str){
  for(int i=0; i<strlen(str); i++){
    if(str[i] == '!' || str[i] == '@' || str[i] == '#' || str[i] == '$'
      || str[i] == '%' || str[i] == '^' || str[i] == '&' || str[i] == '*'
      || str[i] == '(' || str[i] == ')' || str[i] == '-' || str[i] == '{'
      || str[i] == '}' || str[i] == '[' || str[i] == ']' || str[i] == ':'
      || str[i] == ';' || str[i] == '"' || str[i] == '\''|| str[i] == '<'
      || str[i] == '>' || str[i] == '.' || str[i] == '/' || str[i] == '?'
      || str[i] == '~' || str[i] == '`' || str[i] == '|' || str[i] == '+'
      || str[i] == '_' || str[i] == ',' ){
        return true;
      }
  }
  return false;
}

// Check if the given commands has proper parameter.
bool checkParameter(char *buf, int i){
  int count = 0;
  char* token = strtok(buf, " ");
  while(token != NULL){
    token = strtok(NULL, " ");
    count++;
  }
  if(count > i || count < i){
    return false;
  }
  return true;
}

// Check if the login message from server is approved or not
int client_checkMesg(char *buf){
   if(strcmp(buf,"[+] Login Successful! ") == 0){
			client.login = TRUE;
    return TRUE;
  }
  return FALSE;
}

// Check if user input is valid or not
int checkRegLogWord(char *buf, int z) {
  int count = 1;
  char* token = strtok(buf, " ");
  while(count <= 2){
    token = strtok(NULL, " ");
    if(count == 1){
      if((strlen(token) < MIN_USERNAME || strlen(token) > MAX_USERNAME
        || checkInput(token)) && (z == REGISTER)){
        printf("[!] username must be between 4-8 characters.\n");
        printf("[!] usable characters: A-Z, a-z, digits\n");
        return FALSE-1;
      }
      else if((strlen(token) < MIN_USERNAME || strlen(token) > MAX_USERNAME)
        && z == LOGIN){
        printf("[-] Incorrect username or password.\n");
        return FALSE-1;
      }
    }
    else if(count == 2){
      if((strlen(token) < MIN_PASSWORD || strlen(token) > MAX_PASSWORD
        || checkInput(token)) && z == REGISTER){
        printf("[!] password must be between 4-8 characters.\n");
        printf("[!] usable characters: A-Z, a-z, digits\n");
        return FALSE-1;
      }
      else if((strlen(token) < MIN_PASSWORD || strlen(token) > MAX_PASSWORD)
      && z == LOGIN){
        printf("[-] Incorrect username or password.\n");
        return FALSE-1;
      }
    }
    count++;
  }
  return TRUE;
}

// Check if "REGISTER" has proper usage. If so, send data to server.
int client_registerAccount(char *buf){
  char full[MAX_COMMAND];
  strcpy(full, buf);

  if(checkParameter(buf, 3) == false){
		printf("[!] Usage: REGISTER [username] [password]\n");
    return FALSE-1;
  }
  strcpy(buf, full);
  int ret = checkRegLogWord(buf, REGISTER);
  return ret;
}

// Check if "LOGIN" has proper usage. If so, send data to server.
int client_login(char *buf){
	char full[MAX_COMMAND];
  strcpy(full, buf);

	if(checkParameter(buf, 3) == false){
		printf("[!] Usage: LOGIN [username] [password]\n");
		return FALSE;
	}
	strcpy(buf, full);
	char *token = strtok(full, " ");
  token = strtok(NULL, " ");
  strcpy(tempUsername,token);
  int ret = checkRegLogWord(buf, LOGIN);
	return ret;
}

// Check the length of the message.
bool checkLength(char *buf, int i){
	char temp[MAX_COMMAND];
	strcpy(temp, buf);
	char* token = strtok(temp, " ");
	int countA = strlen(token)+1;
	int count;
	int word = 1;
	char second[MAX_COMMAND];

	while(token!=NULL){
		if(word == 2 && token !=NULL && i == 1){
			word+=2;
			break;
		}
		if(word == 3 && token != NULL && i == 2){
			word+=2;
			break;
		}
		if(i == 2){
			strcpy(second,token);
		}
		token = strtok(NULL," ");
    word++;
	}
	if(word < 3 && i == 1){                                         // check parameter
    printf("[!] Usage: SEND | SENDA [msg]\n");
    return false;
  }
  else if(word < 4 && i == 2){                                    // check parameter
    printf("[!] Usage: SEND2 | SENDA2 [username] [msg]\n");
    return false;
  }
	if(i == 1){
		count = strlen(buf) - countA;
	}
	else if(i == 2){
		if(strlen(second) < 4 || strlen(second) > 8){   // check if username is within range
			printf("[!] Invalid username.\n");
			return false;
		}
		count = strlen(buf) - countA - (strlen(second)+1);
	}
	if(count > MESSAGE_SIZE){                                   // check if message is within range
    printf("[!] Message length is over the limit.\n");
    printf("[!] Maximum message length: 256 letters.\n");
		return false;
	}
	return true;
}

// Check if "SEND" or "SENDA" has proper usage.
int client_sendMessagePublic(char *buf){
  char full[MAX_COMMAND];
  strcpy(full, buf);
  if(checkLength(full, 1) == false){
    return FALSE;
  }
  return TRUE;
}

// Check if "SEND2" or "SEND2A" has proper usage.
int client_sendMessagePrivate(char *buf){
  char full[MAX_COMMAND];
  strcpy(full, buf);
  char *token = strtok(full," ");
  token = strtok(NULL," ");
  if(strcmp(client.username,token) == 0){ // if client is sending message to itself
    printf("[!] You cannot send message to yourself.\n");
    return FALSE;
  }
  strcpy(full, buf);
  if(checkLength(full, 2) == false){
    return FALSE;
  }
  return TRUE;
}

// Check if "SENDF" has proper usage. If so, send data to server.
int client_sendFilePublic(char *buf){
  char full[MAX_COMMAND];
  strcpy(full, buf);

  if(checkParameter(full,2) == false){
    printf("[!] Usage: SENDF [local_file]\n");
    return FALSE;
  }
	strcpy(full, buf);
  char* fileName = strtok(full, " ");
	fileName = strtok(NULL, " ");
  if(access(fileName,F_OK) == -1){
    printf("[!] Error: file cannot be open/does not exist\n");
    return FALSE;
  }
  else{
    struct stat st;
    stat(fileName, &st);
    int size = st.st_size;
	  if(st.st_size > MAX_FILE_SIZE){
	    printf("[!] File size cannot be larger than 10mb\n");
	    return FALSE;
	  }
		FILE *fp;
		fp = fopen(fileName, "rb");								// open the file in binary mode
	  char toSend[MAX_BUFFER_SIZE];
    char buffer[MAX_BUFFER_SIZE];
	  int n=0;
	  sprintf(toSend,"SENDF %s NEW ", fileName);
		strcat(toSend, buffer);

		char tmp[MAX_COMMAND];
		sprintf(tmp,"SEND Sending file: %s \n",fileName);
		int sendN = strlen(tmp);
		client_Send_NonBlocking(client.sock_fd, (char *)&sendN, 4);
		client_Send_NonBlocking(client.sock_fd, tmp, strlen(tmp));

		while(n < size){		// read data until all the data are done reading
			n += fread(buffer,sizeof(char), 1024,fp);
      strcat(toSend,buffer);
      int toSendSize = strlen(toSend);
	    client_Send_NonBlocking(client.sock_fd, (char *)&toSendSize, 4); // tell the size of data to server
			client_Send_NonBlocking(client.sock_fd, toSend, strlen(toSend));
			memset(toSend, 0, MAX_BUFFER_SIZE);
			memset(buffer, 0, MAX_BUFFER_SIZE);
      sprintf(toSend, "SENDF %s OLD ",fileName);
	  }
    fclose(fp);
  }
  return TRUE;
}

// Check if "SENDF2" has proper usage. If so, send data to server.
int client_sendFilePrivate(char *buf){
  char full[MAX_COMMAND];
  char username[MAX_USERNAME];
  char fileName[MAX_COMMAND];
  strcpy(full, buf);
  if(checkParameter(full,3) == false){
    printf("[!] Usage: SENDF2 [username] [local_file]\n");
    return FALSE;
  }

  strcpy(full, buf);
  char* token = strtok(full, " ");
//  char* fileName = strtok(full, " ");
	token = strtok(NULL, " ");
	strcpy(username,token);
  if(strcmp(client.username,username) == 0){
    printf("[!] You cannot send message to yourself.\n");
    return FALSE;
  }
	token = strtok(NULL, " ");
	strcpy(fileName,token);
  if(access(fileName,F_OK) == -1){
    printf("[!] Error: file cannot be open/does not exist\n");
    return FALSE;
  }
  else{
    struct stat st;
    stat(fileName, &st);
    int size = st.st_size;
    FILE *fp;
    fp = fopen(fileName, "rb");
    if(st.st_size > MAX_FILE_SIZE){
      printf("[!] File size cannot be larger than 10mb\n");
      fclose(fp);
      return FALSE;
    }
    char toSend[MAX_BUFFER_SIZE];
    char buffer[MAX_BUFFER_SIZE];
    int n=0;
    sprintf(toSend,"SENDF2 %s %s NEW ",username, fileName);
    strcat(toSend, buffer);
    char tmp[MAX_COMMAND];
    sprintf(tmp, "SEND2 %s Sending file: %s\n",username,fileName);
    int sendN = strlen(tmp);

    client_Send_NonBlocking(client.sock_fd, (char *)&sendN, 4);
    client_Send_NonBlocking(client.sock_fd, tmp, strlen(tmp));

    while(n < size){
      n += fread(buffer,sizeof(char), 1024,fp);

      strcat(toSend,buffer);
      int toSendSize = strlen(toSend);
      client_Send_NonBlocking(client.sock_fd, (char *)&toSendSize, 4); // tell the size of data to server
      client_Send_NonBlocking(client.sock_fd, toSend, strlen(toSend));
      memset(toSend, 0, MAX_BUFFER_SIZE);
      memset(buffer, 0, MAX_BUFFER_SIZE);
      sprintf(toSend, "SENDF2 %s OLD ",fileName);
    }
    fclose(fp);
  }
  return TRUE;
}

// Check if "DELAY" has proper usage. If so, send data to server.
int client_delay(char *buf){
  char full[MAX_COMMAND];
  strcpy(full, buf);
  if(checkParameter(buf, 2) == false){
    printf("Usage: DELAY [N]\n");
    return 0;
  }
  //send data to server
  return 1;
}

// Usage for the client.
void client_help(){
  printf("Usage: \n");
  printf("  REGISTER [username] [password]\n");
  printf("  LOGOUT\n");
  printf("  SEND [msg]\n");
  printf("  SEND2 [username] [msg]\n");
  printf("  SENDA [msg]\n");
  printf("  SENDA2 [username] [msg]\n");
  printf("  SENDF [local file]\n");
  printf("  SENDF2 [username] [local file]\n");
  printf("  LIST\n");
  printf("  DELAY [N]\n");
	printf("  LOGSTART\n");
	printf("  LOGSTOP\n");
}

// Check if client can access to specific mode.
bool loginCheck(){
	if(client.login == FALSE){
    printf("[!] You need to LOGIN to use this mode.\n");
    return false;
  }
  return true;
}

// Check the message from server.
void checkServerMsg(char *reply){
	mesg_t server_send_data;
	char * temp = (char *)malloc(MAX_BUFFER_SIZE);

	strcpy(temp, reply);
	strtok(temp, " ");

	if(strcmp(temp, "ELSE") == 0){
		// If the message if just to display to client
	  split_serv_mesg(reply, &server_send_data);
	  printf("%s\n", server_send_data.first);
		if(logInfo == TRUE){
			fprintf(logFp,"%s\n",server_send_data.first);
			fflush(logFp);
		}
	}
	else if (strcmp(temp, "LOGIN") == 0){
		// Login data
	  split_serv_mesg(reply, &server_send_data);
	  client_checkMesg(server_send_data.first);
	  strcpy(client.username,tempUsername);
		printf("%s\n", server_send_data.first);
		if(logInfo == TRUE){
			fprintf(logFp,"%s\n",server_send_data.first);
			fflush(logFp);
		}
	}
	else if (strcmp(temp, "LIST") == 0){
		// Listing out online users.
		if(logInfo == TRUE){
			fprintf(logFp,"\n----------------\n");
			fprintf(logFp,"| Online users |\n");
			fprintf(logFp,"----------------\n");
			fflush(logFp);
		}
	  printf("\n----------------\n");
	  printf("| Online users |\n");
	  printf("----------------\n");
	  char* token = strtok(reply, " ");
	  int count = 1;
	  while(token != NULL){
	    token = strtok(NULL, " ");
	    if(token != NULL){
	      printf("[%d] %s\n", count, token);
				if(logInfo == TRUE){
					fprintf(logFp,"[%d] %s\n", count, token);
					fflush(logFp);
				}
	    }
	    count++;
	  }
	  printf("----------------\n\n");
		if(logInfo == TRUE){
			fprintf(logFp,"----------------\n\n");
			fflush(logFp);
		}
	}
	else if (strcmp(temp, "SENDF") == 0 || strcmp(temp, "SENDF2") == 0){
		// Receiving file from server.
    char fileName[MAX_COMMAND];
    char version[4];
    char fileData[MAX_BUFFER_SIZE];
    char choice[7];
    strcpy(choice, temp);
    int count=0;
    memset(fileData,0,MAX_BUFFER_SIZE);
    strcpy(temp,reply);
    char* token = strtok(temp, " ");
    if(strcmp(choice, "SENDF2") == 0){
    token = strtok(NULL, " ");
    }
    token = strtok(NULL, " ");
    strcpy(fileName, token);
    token = strtok(NULL, " ");
    strcpy(version, token);
    while(token != NULL){
      token = strtok(NULL, " ");
      if(token != NULL){
        if(count != 0){
          strcat(fileData," ");
        }
	      strcat(fileData,token);
	      count++;
      }
    }
    FILE* fp;
    if(strcmp(version, "NEW") == 0){
    fp = fopen(fileName, "wb");
    fprintf(fp, "%s",fileData);
    fflush(fp);
    }
    else if(strcmp(version, "OLD") == 0){
    fp = fopen(fileName, "ab");
    fprintf(fp, "%s",fileData);
    fflush(fp);
    }
    fclose(fp);
	}
}

// start recording all the messages
void logStart(){
	time_t t;
	struct tm * tinfo;

	time (&t);
	tinfo = localtime (&t);

	logFp = fopen("log.txt", "a+");
	logInfo = TRUE;
	fprintf(logFp, "========= Start\n");
	fprintf(logFp, "========= Date: %d/%d/%d\n",tinfo->tm_mon, tinfo->tm_mday, tinfo->tm_year + 1900);
	fprintf(logFp, "========= Time: %d:%d:%d\n",tinfo->tm_hour, tinfo->tm_min, tinfo->tm_sec);
	fflush(logFp);
}

// stop recording all the messages
void logStop(){
	logFp = fopen("log.txt", "a+");
	fprintf(logFp, "========= Stop\n");
	fflush(logFp);
	logInfo = FALSE;
	fclose(logFp);
}

// get info from user and send it to server
void *user_worker(void *arg){
	int allGood=FALSE;
	char full[MAX_COMMAND];
	char buf[MAX_COMMAND];
  FILE *fp;
  char *line = NULL;
  size_t len = 0;
  ssize_t nread;

  if(client.script[0] != '\0'){	// reading script if needed
		fp = fopen(client.script, "r");
  }

	do {
		if(client.script[0] != '\0'){
			nread = getline(&line, &len, fp);
			if(nread < 0){
				printf("Reading script is done!\n");
				break;
			}
			if(logInfo == TRUE){
				fprintf(logFp,"%s",line);
				fflush(logFp);
			}
			strcpy(buf,line);
			printf("%s",line);
    }
    else{ // if no script, then get input from user
      fgets(buf, MAX_COMMAND, stdin);
			if(logInfo == TRUE){
				fprintf(logFp,"%s",buf);
				fflush(logFp);
			}
    }
    if(strcmp(buf,"\n") != 0){
		  strtok(buf, "\n");
		  strcpy(full, buf);
		  strtok(full, " ");
		  if(strcmp(full, "REGISTER") == 0){
		    strcpy(full, buf);
				if(client.login == TRUE){
		      printf("[!] You need to LOGOUT to use this mode.\n");
        }
				else {
					strcpy(full, buf);
					allGood = client_registerAccount(full);	// register usage is correct or not
				}
			}
		  else if(strcmp(full, "LOGIN") == 0){
		    strcpy(full, buf);
		    if(client.login == TRUE){
		      printf("[!] You need to LOGOUT to use this mode.\n");
        }
        else {
	        strcpy(full, buf);
          allGood = client_login(full);   // login usage is correct or not
        }
		  }
		  else if(strcmp(buf, "LOGOUT") == 0){
		    if(loginCheck()){ // cannot use this mode unless the user is login
		      strcpy(full, buf);
		      int size = strlen(full);
		      client_Send_NonBlocking(client.sock_fd, (char *)&size, sizeof(size));
          client_Send_NonBlocking(client.sock_fd, full, strlen(user_input_data)); // send server to logout
					printf("[+] You have logout successfully!\n");
					break;
		    }
		  }
      else if (strcmp(full, "SEND") == 0 || strcmp(full, "SENDA") == 0){
        if(loginCheck()){ // cannot use this mode unless the user is login
          strcpy(full, buf);
          allGood = client_sendMessagePublic(full); // sending message usage is correct or not
        }
      }
      else if (strcmp(full, "SEND2") == 0 || strcmp(full, "SENDA2") == 0){
        if(loginCheck()){ // cannot use this mode unless the user is login
          strcpy(full, buf);
          allGood = client_sendMessagePrivate(full); // sending message usage is correct or not
        }
      }
			else if(strcmp(full, "SENDF") == 0){
				if(loginCheck()){ // cannot use this mode unless the user is login
					strcpy(full, buf);
			    client_sendFilePublic(full); // send file to server
				}
			}
			else if(strcmp(full, "SENDF2") == 0){
				if(loginCheck()){
					strcpy(full, buf);
			    client_sendFilePrivate(full); // send file to server
				}
			}
		  else if(strcmp(buf, "LIST") == 0){
			  if(loginCheck()){ // cannot use this mode unless the user is login
	        strcpy(full, buf);
	        allGood = TRUE;
	      }
		  }
      else if(strcmp(full, "DELAY") == 0){
       mesg_t client_data;
       strcpy(full, buf);
       int ret = client_delay(full);
       if(ret == TRUE){
        strcpy(full, buf);
        split_serv_mesg(full, &client_data);
        sleep(atoi(client_data.first)); // delay user sending until moment specify
       }
      }
		  else if(strcmp(buf, "HELP") == 0){ // show usage of modes
		    client_help();
		  }
			else if(strcmp(buf, "LOGSTART") == 0){
				if(logInfo == FALSE){
					logStart();
				}
				else{
					printf("[-] Please stop log to use this mode.\n");
				}
			}
			else if(strcmp(buf, "LOGSTOP") == 0){
				if(logInfo == TRUE){
					logStop();
				}
				else{
					printf("[-] Please start log to use this mode.\n");
				}
			}
		  else{
				// no modes specify by user
		    char * token = strtok(buf, " ");
		    printf("[!] The command '%s' cannot be found. Use 'HELP' to see the usage.\n",token);
			}
		  if(allGood == TRUE){
				// send data to server
		    strcpy(user_input_data, buf);
		    int size = strlen(user_input_data);
				client_Send_NonBlocking(client.sock_fd, (char*)&size, sizeof(size));
		    client_Send_NonBlocking(client.sock_fd, user_input_data, strlen(user_input_data)); // send typed data to server
		  }
		  allGood = FALSE;
    }
	}while(1);
  printf("[-] Connection closed.\n");
  pthread_cancel(server_thread);                                  // kill the server thread
  return NULL;
}

// get info from server and show it to user
void *server_worker(void *arg){
	char * reply = (char *)malloc(MAX_BUFFER_SIZE);
	 do{
			poll(notice, 1, 0);
	    if(notice->revents & POLLRDNORM){
				for(int i=MAX_COMMAND; i>= 0; i--){
					reply[i] = '\0';
				}
				int size;
					if (connStat.nRecvS < 4) { // get the size
						client_Recv_NonBlocking(client.sock_fd, (char *)&connStat.size, 4, NUM);
	          if (connStat.nRecvS == 4) {
	            size = connStat.size;
	            if (size <= 0 || size > MAX_BUFFER_SIZE) {
		              cError("Invalid size: %d.", size);
									// printf("Invalid size: %d.\n",size);
	            }
	          }
	        }
	        if(connStat.nRecvD < connStat.size && connStat.nRecvS != 0){ // get the data
						client_Recv_NonBlocking(client.sock_fd, reply, connStat.size, DATA);
	          if(connStat.nRecvD == connStat.size){ // if all data r received
	            connStat.nRecvS = 0;
	            connStat.nRecvD=0;
	          }
	            checkServerMsg(reply);
	        }
	    }
	  }while(1);
	  pthread_cancel(user_thread);                                 // kill the user_thread
	  return NULL;
}

// main function of client
int main(int argc, char *argv[]) {
  if(argc < 3){
    printf("Usage: ./client [ip address] [port]\n");
    exit(EXIT_FAILURE);
  }
  client.script[0] = '\0';
  if (argc == 4){
    strcpy(client.script, argv[3]);
  }
  char* ipAddress = argv[1];
  int port = atoi (argv[2]);

  //Set the destination IP address and port number
  struct sockaddr_in serverAddr;
  memset(&serverAddr, 0, sizeof(serverAddr));
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons((unsigned short) port);
  inet_pton(AF_INET, ipAddress, &serverAddr.sin_addr);

  //Create the socket, connect to the server
  client.sock_fd = socket(AF_INET, SOCK_STREAM, 0);
  int connect_suc = connect(client.sock_fd, (const struct sockaddr *) &serverAddr, sizeof(serverAddr));
	if(connect_suc != 0){         // server offline
    printf("Fail to connect the server.\n");
    exit(EXIT_FAILURE);
  }

	//Set poll
	notice->fd = client.sock_fd;
	notice->events = POLLRDNORM;

	memset(&connStat, 0, sizeof(struct CONN_STAT));

	client_SetNonBlockIO(client.sock_fd);
  char *full= (char *)malloc(MAX_COMMAND);
  char *buf = (char *)malloc(MAX_COMMAND);
	printf("Welcome!\n");
	printf("Enter commands to continue...\n\n");
  client.login = FALSE;

  pthread_create(&user_thread, NULL, user_worker, NULL);
  pthread_create(&server_thread, NULL, server_worker, NULL);

  pthread_join(user_thread, NULL);
  pthread_join(server_thread,NULL);

	if(logInfo == TRUE){
		logStop();
	}

  //Close socket
  free(buf);
  free(full);
  close(client.sock_fd);
  return 0;
}
