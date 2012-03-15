#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 

void error(const char *msg)
{
    perror(msg);
    exit(0);
}

int main(int argc, char *argv[])
{
    int sockfd, portno, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;

    char buffer[42000];
    if (argc < 4) {
       printf("TLS8892: ERROR usage %s hostname port \"command\"\n", argv[0]);
       exit(0);
    }
    portno = atoi(argv[2]);
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        printf("TLS8892: ERROR opening socket");
		exit(0);
	}
    server = gethostbyname(argv[1]);
    if (server == NULL) {
        printf("TLS8892: ERROR, no such host");
        exit(0);
    }
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr,
         (char *)&serv_addr.sin_addr.s_addr,
         server->h_length);
    serv_addr.sin_port = htons(portno);
    if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) {
        printf("TLS8892: ERROR connecting\n");
		exit(0);
	}
    n = write(sockfd,argv[3],strlen(argv[3]));
    if (n < 0) {
        printf("TLS8892: ERROR writing to socket");
		exit(0);
	}
    bzero(buffer,sizeof(buffer));
    n = read(sockfd,buffer,sizeof(buffer)-1);
    if (n < 0) {
        printf("TLS8892: ERROR reading from socket");
		exit(0);
	}
    printf("%s",buffer);
    close(sockfd);
    return 0;
}
