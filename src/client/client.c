
#define _POSIX_C_SOURCE 200112L

#include "buffer/buffer.h"
#include "client/clientCommandController.h"

#include <arpa/inet.h>
#include <netinet/in.h> 
#include <netdb.h> 
#include <stdio.h> 
#include <stdlib.h> 
#include <stdint.h> 
#include <string.h> 
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#define BUFSIZE 512

#define CREDENTIALS_LENGTH 256
#define AUTH_MESSAGE_LENGTH 515
#define AUTH_RESPONSE_LENGTH 2
#define PIPO_PROTOCOL_VERSION 1

static int new_ipv6_connection(struct in6_addr ip, in_port_t port) ;
static int new_ipv4_connection(struct in_addr ip, in_port_t port) ;
static void interactive_client(int fd);
static void print_help();

static CommandController controllers[COMMAND_COUNT];

int main(int argc, char *argv[]) {
		
	fd_set fds;
	int fd;
	char * ip = "127.0.0.1";
	uint16_t port = 8080;

	if(argc == 3) {
		ip = argv[2];
		port = atoi(argv[3]);
	}

	struct in_addr addr4;
	struct in6_addr addr6;

	if(inet_pton(AF_INET, ip, &addr4)){
		fd = new_ipv4_connection(addr4, htons(port));
	}
	else if(inet_pton(AF_INET6, ip, &addr6)){
		fd = new_ipv6_connection(addr6, htons(port));
	}
	else {
		perror("The provided IP address is invalid.");
	}

	if(log_in(fd)) {
		client_command_controller_init(controllers);
		interactive_client(fd);
	}

	close(fd);

	return 0;
}

static int new_ipv6_connection(struct in6_addr ip, in_port_t port) {
	
	int sock;
	struct sockaddr_in6 addr; 
  
    // socket create and varification 
    sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_SCTP); 
    if (sock == -1) { 
        return -1;
    } 
    
	memset(&addr, '\0', sizeof(addr)); 

    addr.sin6_family = AF_INET;
    addr.sin6_port = port; 
	addr.sin6_addr = ip;

	int ans;

	do {
		ans = connect(sock, (struct sockaddr*) &addr, sizeof(addr));
	}while(ans == -1 && errno != EINTR);
	
	if(ans == -1) {  
        close(sock);
		return -1;
    } 

	return sock;
}

static int new_ipv4_connection(struct in_addr ip, in_port_t port) {
	
	int sock;
	struct sockaddr_in addr; 
  
    // socket create and varification 
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_SCTP); 
    if (sock == -1) { 
        return -1;
    } 
    
	memset(&addr, '\0', sizeof(addr)); 

    addr.sin_family = AF_INET;
    addr.sin_port = port; 
	addr.sin_addr = ip;

	int ans;

	do {
		ans = connect(sock, (struct sockaddr*) &addr, sizeof(addr));
	}while(ans == -1 && errno != EINTR);
	
	if(ans == -1) {  
        close(sock);
		return -1;
    } 

	return sock;
}

static bool log_in(int fd) {

	size_t i;
	uint8_t ulen;
	uint8_t plen;
	
	printf("Insert username: (max 255 characters. Finisish with enter)");
	char username[CREDENTIALS_LENGTH];
	for (i = 0; i < CREDENTIALS_LENGTH && username[i] != '\n'; i++) {
		username[i] = getchar();
		ulen++;
	}
	username[i] = '\0';

	if(username[0] == '\0'){
		perror("Invalid username");
	}

	printf("Insert password: (max 255 characters. Finisish with enter)");
	char password[CREDENTIALS_LENGTH];
	for (i = 0; i < CREDENTIALS_LENGTH && password[i] != '\n'; i++) {
		password[i] = getchar();
		plen++;
	}
	password[i] = '\0';

	if(password[0] == '\0'){
		perror("Invalid password");
	}

	uint8_t authMessage[AUTH_MESSAGE_LENGTH];
	memset(authMessage, '\0', AUTH_MESSAGE_LENGTH);
	authMessage[0] = PIPO_PROTOCOL_VERSION;
	authMessage[1] = ulen;
	strcpy(authMessage, username);
	authMessage[ulen + 2] = plen;
	strcpy(authMessage, password);
	
	size_t bytesToSend = ulen + plen + 3;
	size_t bytesSent = 0;
	ssize_t writeBytes;

	do {
		writeBytes = write(fd, authMessage + bytesSent, bytesToSend - bytesSent);

		if(writeBytes > 0){
			bytesSent += writeBytes;
		}

	} while (bytesSent < bytesToSend && (writeBytes != -1 || errno !=  EINTR));

	if(writeBytes == -1){
		perror("Error sending auth");
	}

	uint8_t authAns[AUTH_RESPONSE_LENGTH];
	ssize_t readBytes;
	size_t bytesRecieved = 0;

	do {
		readBytes = read(fd, authAns + bytesRecieved, AUTH_RESPONSE_LENGTH - bytesRecieved);
		if(readBytes > 0){
			bytesRecieved += readBytes;
		}
	} while (bytesRecieved < AUTH_RESPONSE_LENGTH && (readBytes != -1 || errno !=  EINTR));

	if(readBytes == -1){
		perror("Error reading auth response");
	}

	// Auth successful
	if(authAns[1] == 0x00) {
		printf("Logged in succesfully\n");
		return true;
	}

	// Auth Unsuccessful
	else {
		printf("Error ocurred during log in: ");

		if(authAns[1] == 0x01){
			printf("Authentication failed.\n");
		}
		else if(authAns[1] == 0x02){
			printf("Invalid version.\n");
		}
		else {
			printf("Unexpected answer\n");
		}
		return false;
	}

}

static void interactive_client(int fd) {

	while(1) {

		print_help();
		
		char command;
		printf("Insert new command: ");

		// TODO: getchar() ?? - Tobi
		scanf("%1c", &command);

		if(command == 'x') {
			break;
		}

		command -= 'a';

		controllers[command].sender(fd);

		controllers[command].receiver(fd);
	}
}