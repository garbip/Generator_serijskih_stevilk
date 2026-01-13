#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <arpa/inet.h>

void error(const char *msg) {
    perror(msg);
    exit(1);
}

int main(int argc, char *argv[]) {
    int sock, n;
    struct sockaddr_in server;
    char buf[1024];
    int waiting_for_serial = 0;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) error("socket");

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr("127.0.0.1");
    server.sin_port = htons(5095);

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0)
        error("connect");

    printf("Connected to server\n");

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(sock, &readfds);
        
        int maxfd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO;
        if (select(maxfd + 1, &readfds, NULL, NULL, NULL) < 0) error("select");

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            bzero(buf, sizeof(buf));
            if (fgets(buf, sizeof(buf), stdin) == NULL) break;

            size_t len = strlen(buf);
            if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';

            if (send(sock, buf, strlen(buf) + 1, 0) < 0) error("send");

            if (strcmp(buf, "GET") == 0) waiting_for_serial = 1;
            if (strcmp(buf, "X") == 0) break;
        }

        if (FD_ISSET(sock, &readfds)) {
            bzero(buf, sizeof(buf));
            n = recv(sock, buf, sizeof(buf), 0);
            
            if (n <= 0) {
                printf("Server closed connection\n");
                break;
            }

            printf("Server: %s\n", buf);

            if (strncmp(buf, "NAPAKA", 6) == 0) {
                waiting_for_serial = 0;
                continue;
            }

            if (waiting_for_serial) {
                char serial_uuid[100], crc[16];
                if (sscanf(buf, "%99s %15s", serial_uuid, crc) == 2) {
                    printf("Received: %s %s\n", serial_uuid, crc);
                    
                    if (send(sock, "PREJETO D5A07B2F", 17, 0) < 0) error("send");
                    printf("Sent: PREJETO D5A07B2F\n");
                    
                    waiting_for_serial = 0;
                }
            }
        }
    }

    close(sock);
    return 0;
}
