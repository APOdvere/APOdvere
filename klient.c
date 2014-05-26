//client
//author: honsdomi

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>


#define MAXDATASIZE 256
#define PORT "55556"
#define HOST "127.0.0.1"

int getSocket(char *address, char *port, struct addrinfo hint) {
    struct addrinfo *res, *p;
    int r, sock_fd;

    if ((r = getaddrinfo(address, port, &hint, &res)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(r));
    }

    for (p = res; p != NULL; p = p->ai_next) {
        if ((sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("client: socket");
            continue;
        }

        if (connect(sock_fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sock_fd);
            continue;
        }

        break;
    }
    if (p == NULL) {
        return -1;
    }
    return sock_fd;
}

int sendMessage(int sfd, int gate_id, int user_id, int user_pin) {
    int sent = 0;
    char message[64];
    sprintf(message, "checkaccess %d %d %d\n", gate_id, user_id, user_pin);
    int mlen = strlen(message);
    int bytesleft = mlen;
    int n;

    while (sent < mlen) {
        n = send(sfd, message + sent, bytesleft, 0);
        if (n == -1) {
            return -1;
        }

        sent += n;
        bytesleft -= n;
    }
    return 0;
}

int recvMessage(int sfd, char *message) {
    int r;
    while (1) {

        r = recv(sfd, message, MAXDATASIZE, 0);

        if (r == -1) {
            return -1;
        }
        if (r == 0) {
            return 0;
        }
        if (strstr(message, "\n") != NULL) {
            message[r] = '\0';
            return 1;
        }
    }

}

int mainKlient(int argc, char *argv[]) {
    int sockfd, numbytes;
    char buf[MAXDATASIZE];
    struct addrinfo hints;


    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;



    if ((sockfd = getSocket(HOST, PORT, hints)) == -1) {
        printf("Nelze se pripojit.");
        exit(1);

    }
    if ((sendMessage(sockfd, 55, 33, 3333)) == -1) {
        perror("send");
        exit(1);
    }

    if ((numbytes = recvMessage(sockfd, buf)) <= 0) {
        //printf("Server nedostupny.\n");
    }

    printf("Server: %s", buf);


    close(sockfd);

    return 0;
}
