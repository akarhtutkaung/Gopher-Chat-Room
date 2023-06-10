# Gopher Chat Room
@Author: Akar (Ace) Kaung

## Summary
GopherChat is a platform where clients can send data between to one another. This platform utilizes
TCP connection to serve the clients. In order for the data to get to other clients, it need to pass the
server first which determine who/what to send. Clients can send messages and files with cool features
that everyone like to have. GopherChat can hold up to 10,000 user credentials inside its database. But
currently, it can only let 32 users to be online.

## Instruction
1. Compile everything and create the executable files.
    ```
    make all
    ```
2. **Server**, start the server with specific port number for clients to join.
    ```
    ./server [port]
    ```

3. **Clients**, start the client session with the server
    ```
    ./client [server_ip] [port]
    ```

## Features and Usages

### Server
Server will be able to see if someone register new account and successful or not. Including login, if
someone successfully login, the server will notify all other online users that the new user has join the
chat. If someone logout, the server will notify all other online users that the person has left the chat.
Server will be able to see which messages are send public and which are send private and from whom to
whom. 

### Client
| Command  | Description |
| ------------- | ------------- |
| LOGSTART | (New feature) Start recording all the messages that is send
to or receive from server inside the local directory |
| LOGSTOP | (New feature) Stop recording all the messages that is send
to or receive from server inside the local directory |
| REGISTER [username] [password] | Register a new account |
| LOGIN [username] [password] | Log in with an existing accountand enter the chat room |
| LOGOUT | Log out and leave the chat room |
| SEND [msg] | Send a public message |
| SENDF2 [username] [msg] | Send a private message to a user |
| SENDA [msg] | Send an anonymous public message |
| SENDA2 [username] [msg] | Send an anonymous private message to a user |
| SENDF [local file] | Send a file publicly |
| SENDF2 [username] [local file] | Send a file to a user privately |
| LIST | List all online users |
| DELAY | A special command that delays for N seconds before |
executing the next command in the script |