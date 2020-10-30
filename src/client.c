#include <arpa/inet.h>
#include <netinet/in.h> 
#include <netdb.h> 
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#define BUFSIZE 512
#define STDIN 0

void handleStdIn(int sock);

void handleSocket(int sock);

int main(int argc, char *argv[]) {

	int sock;
	struct sockaddr_in servaddr; 
  
    // socket create and varification 
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); 
    if (sock == -1) { 
        printf("socket creation failed...\n"); 
        exit(0); 
    } 
    else
        printf("Socket successfully created..\n"); 
    
	bzero(&servaddr, sizeof(servaddr)); 
    // assign IP, PORT 
    servaddr.sin_family = PF_INET; 
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1"); 
    servaddr.sin_port = htons(1080); 
	
    // connect the client socket to server socket 
    if (connect(sock, (struct sockaddr*) &servaddr, sizeof(servaddr)) != 0) { 
        printf("connection with the server failed...\n"); 
        exit(0); 
    } 
    else
        printf("connected to the server..\n"); 
  
	fd_set fds;

	while (1) {
		FD_SET(STDIN, &fds);
		FD_SET(sock, &fds);

		select(sock + 1, &fds, NULL, NULL, NULL);

		if(FD_ISSET(STDIN, &fds))
			handleStdIn(sock);
		
		if(FD_ISSET(sock, &fds))
			handleSocket(sock);
	}

	close(sock);
	return 0;
}

void handleStdIn(int sock) {

	char buffer[BUFSIZE];
	ssize_t readBytes = read(STDIN, buffer, BUFSIZE - 1);
	for (size_t i = 0; i < readBytes; i++) {
		buffer[i] -= '0';
	}
	
	ssize_t numBytes = send(sock, buffer, readBytes, 0);
	if (numBytes < 0 || numBytes != readBytes)
        exit(1);
}

void handleSocket(int sock) {
	// Receive the same string back from the server
	ssize_t numBytes;
	char buffer[BUFSIZE]; 
	/* Receive up to the buffer size (minus 1 to leave space for a null terminator) bytes from the sender */
	numBytes = recv(sock, buffer, BUFSIZE - 1, 0);
	if (numBytes < 0) {
		exit(1);
	}  
	else if (numBytes == 0)
		exit(1);
	else {
		buffer[numBytes] = '\0';    // Terminate the string!
	}
	printf("Recieved: %s", buffer);
}