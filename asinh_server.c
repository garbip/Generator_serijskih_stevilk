#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>

void error(const char *msg) {
    perror(msg);
    exit(1);
}

uint32_t crc32_compute(const char *data) {
    uint32_t crc = 0xFFFFFFFFu;
    while (*data) {
        crc ^= (uint8_t)*data++;
        for (int i = 0; i < 8; ++i) {
            crc = (crc >> 1) ^ (0xEDB88320u & (-(int)(crc & 1)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

void save_counter(int j) {
    FILE *file = fopen("counter.txt", "w");
    if (file) {
        fprintf(file, "%d", j);
        fclose(file);
    } else {
        perror("Error saving counter");
    }
}

int load_counter(void) {
    FILE *file = fopen("counter.txt", "r");
    int j = 0;
    if (file) {
        if (fscanf(file, "%d", &j) != 1) {
            j = 0;  // Reset to 0 if read fails
        }
        fclose(file);
    }
    return j;
}

void generate_uuid_with_serial(char *output, size_t output_size) {
    const char hex_chars[] = "0123456789abcdef";
    char hex32[33];    
    char uuid[37];

    for (int i = 0; i < 32; ++i) {
        hex32[i] = hex_chars[rand() & 0xF]; 
    }

    hex32[12] = '4';
    hex32[16] = hex_chars[(rand() & 0x3) | 0x8];

    int out = 0;
    for (int i = 0; i < 32; ++i) {
        if (out == 8 || out == 13 || out == 18 || out == 23) {
            uuid[out++] = '-';
        }
        uuid[out++] = hex32[i];
    }
    uuid[out] = '\0';

    uint32_t crc = crc32_compute(uuid);
    int j = load_counter();
    
    snprintf(output, output_size, "%08d %s %08" PRIx32, j+1, uuid, crc);
    
    j++;
    save_counter(j);
}

int main(int argc, char *argv[]) {
    int sock, n;
    struct sockaddr_in server, from;
    socklen_t fromlen;
    char buf[1024];
    int client_known = 0;
    struct sockaddr_in client;
    socklen_t clientlen;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    srand((unsigned)time(NULL));

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) error("Opening socket");

    bzero((char *)&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(atoi(argv[1]));

    if (bind(sock, (struct sockaddr *)&server, sizeof(server)) < 0)
        error("binding");

    fromlen = sizeof(from);

    printf("Asynchronous UDP server started on port %s.\n", argv[1]);
    printf("Waiting for first client message...\n");
    printf("Type 'X' to exit after a client connects.\n");

    while (1) {
        fd_set readfds;
        int maxfd;

        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        FD_SET(STDIN_FILENO, &readfds);

        maxfd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO;

        if (select(maxfd + 1, &readfds, NULL, NULL, NULL) < 0) {
            error("select");
        }

        if (FD_ISSET(sock, &readfds)) {
            bzero(buf, sizeof(buf));
            n = recvfrom(sock, buf, sizeof(buf), 0,
                         (struct sockaddr *)&from, &fromlen);
            if (n < 0) error("recvfrom");

            if (!client_known) {
                client = from;
                clientlen = fromlen;
                client_known = 1;
                printf("Client connected (address stored).\n");
            }


            printf("Client: %s\n", buf);

            if (buf[0] == 'X' && buf[1] == '\0')  {
                printf("Client sent X, exiting.\n");
                break;
            }
            else if (buf[0] == 'G' && buf[1] == 'E' && buf[2] == 'T' && buf[3] == '\0') { 
                char serial_uuid[128];
                generate_uuid_with_serial(serial_uuid, sizeof(serial_uuid));
                sendto(sock, serial_uuid, strlen(serial_uuid) + 1, 0,
                       (struct sockaddr *)&from, fromlen);
                
                // Wait for "prejeto" acknowledgment
                bzero(buf, sizeof(buf));
                n = recvfrom(sock, buf, sizeof(buf), 0,
                             (struct sockaddr *)&from, &fromlen);
                if (n < 0) error("recvfrom");
                
                // Parse "prejeto" message and CRC32
                char text[64], received_crc[16];
                int parsed = sscanf(buf, "%63s %15s", text, received_crc);
                
                printf("DEBUG text:%s crc:%s\n", text, received_crc);

                if (parsed == 2 && strcmp(text, "PREJETO") == 0) {
                    // Calculate CRC32 of "prejeto"
                    uint32_t calculated_crc = crc32_compute("PREJETO");
                    
                    // Compare CRCs (case-insensitive hex comparison)
                    char calculated_crc_hex[16];
                    snprintf(calculated_crc_hex, sizeof(calculated_crc_hex), "%08x", calculated_crc);
                    
                    

                    if (strcasecmp(calculated_crc_hex, received_crc) == 0) {
                        printf("CRC32 verified successfully for 'prejeto'\n");
                        // Counter already incremented in generate_uuid_with_serial
                    } else {
                        printf("CRC32 mismatch! Expected: %s, Received: %s\n", calculated_crc_hex, received_crc);
                        const char error_msg[] = "NAPAKA 4900B4DB";
                        sendto(sock, error_msg, strlen(error_msg) + 1, 0,
                               (struct sockaddr *)&from, fromlen);
                        // Decrement counter since verification failed
                        int j = load_counter();
                        save_counter(j - 1);
                    }
                } else {
                    printf("Invalid acknowledgment format\n");
                    const char error_msg[] = "NAPAKA 4900B4DB";
                    sendto(sock, error_msg, strlen(error_msg) + 1, 0,
                           (struct sockaddr *)&from, fromlen);
                    // Decrement counter since verification failed
                    int j = load_counter();
                    save_counter(j - 1);
                }
            }
            else {
                char response[] = "Napacen ukaz";
                sendto(sock, response, strlen(response) + 1, 0,
                    (struct sockaddr *)&from, fromlen);
            }
        }

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            bzero(buf, sizeof(buf));
            if (fgets(buf, sizeof(buf), stdin) == NULL) {
                printf("EOF on stdin, exiting.\n");
                break;
            }

            size_t len = strlen(buf);
            if (len > 0 && buf[len - 1] == '\n') {
                buf[len - 1] = '\0';
            }

            if (!client_known) {
                printf("No client connected yet, cannot send.\n");
                continue;
            }

            n = sendto(sock, buf, strlen(buf) + 1, 0,
                       (struct sockaddr *)&client, clientlen);
            if (n < 0) error("sendto");

            if (buf[0] == 'X') {
                printf("You sent X, exiting.\n");
                break;
            }
        }
    }

    close(sock);
    return 0;
}