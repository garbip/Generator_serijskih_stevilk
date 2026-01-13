/* TCP client */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
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

    // Create TCP socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) error("socket");

    // Prepare server address
    bzero((char *)&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr("127.0.0.1");
    server.sin_port = htons(5095);

    // Connect to server
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        error("connect");
    }

    printf("Connected to server. Type 'X' to exit.\n");

    while (1) {
        fd_set readfds;
        int maxfd;

        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(sock, &readfds);
        
        maxfd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO;

        if (select(maxfd + 1, &readfds, NULL, NULL, NULL) < 0) {
            error("select");
        }

        /* Input from keyboard -> send to server */
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            bzero(buf, sizeof(buf));
            if (fgets(buf, sizeof(buf), stdin) == NULL) {
                printf("EOF on stdin, exiting.\n");
                break;
            }

            /* Remove newline */
            size_t len = strlen(buf);
            if (len > 0 && buf[len - 1] == '\n') {
                buf[len - 1] = '\0';
            }

            // Send using send() for TCP
            n = send(sock, buf, strlen(buf) + 1, 0);
            if (n < 0) error("send");

            // Set flag if we sent "GET"
            if (strcmp(buf, "GET") == 0) {
                waiting_for_serial = 1;
            }

            if (buf[0] == 'X') {
                printf("You sent X, exiting.\n");
                break;
            }
        }

        /* Data from socket -> display message */
        if (FD_ISSET(sock, &readfds)) {
            bzero(buf, sizeof(buf));
            n = recv(sock, buf, sizeof(buf), 0);
            
            if (n <= 0) {
                if (n == 0) {
                    printf("Server closed connection.\n");
                } else {
                    perror("recv");
                }
                break;
            }

            printf("Server: %s\n", buf);

            /* Check for error message */
            if (strncmp(buf, "NAPAKA", 6) == 0) {
                printf("Error received from server!\n");
                waiting_for_serial = 0;
                continue;
            }

            /* Parse serial-UUID and CRC32 only if we're expecting it */
            if (waiting_for_serial) {
                char serial_uuid[100], crc32_hex[16];
                
                int parsed = sscanf(buf, "%99s %15s", serial_uuid, crc32_hex);
                if (parsed == 2) {
                    printf("Serial-UUID: %s, CRC32: %s (received OK)\n",
                           serial_uuid, crc32_hex);

                    const char ack[] = "PREJETO D5A07B2F";
                    n = send(sock, ack, strlen(ack) + 1, 0);
                    if (n < 0) {
                        error("send");
                    } else {
                        printf("Sent acknowledgment: %s\n", ack);
                    }
                    
                    waiting_for_serial = 0;
                } else {
                    printf("Server message: %s (not in serial/UUID/CRC32 format)\n", buf);
                }
            }
        }
    }

    close(sock);
    return 0;
}