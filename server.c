#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <pthread.h>

pthread_mutex_t serial_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;
int thread_counter = 0;

void error(const char *msg) {
    perror(msg);
    exit(1);
}

uint32_t crc32_compute(const char *data) {
    uint32_t crc = 0xFFFFFFFFu;
    while (*data) {
        crc ^= (uint8_t)*data++;
        for (int i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (-(int32_t)(crc & 1)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

void save_counter(int j) {
    FILE *file = fopen("counter.txt", "w");
    if (file) {
        fprintf(file, "%d", j);
        fclose(file);
    }
}

int load_counter(void) {
    FILE *file = fopen("counter.txt", "r");
    int j = 0;
    if (file) {
        fscanf(file, "%d", &j);
        fclose(file);
    }
    return j;
}

void generate_uuid_with_serial(char *output, size_t output_size) {
    const char hex_chars[] = "0123456789abcdef";
    char hex32[33], uuid[37];

    for (int i = 0; i < 32; ++i) {
        hex32[i] = hex_chars[rand() % 16];
    }
    hex32[12] = '4';
    hex32[16] = hex_chars[(rand() & 0x3) | 0x8];

    int out = 0;
    for (int i = 0; i < 32; ++i) {
        uuid[out++] = hex32[i];
        if (i == 7 || i == 11 || i == 15 || i == 19) {
            uuid[out++] = '-';
        }
    }
    uuid[out] = '\0';

    int j = load_counter();
    char serial_uuid_str[128];
    snprintf(serial_uuid_str, sizeof(serial_uuid_str), "%08d-%s", j+1, uuid);
    
    uint32_t crc = crc32_compute(serial_uuid_str);
    snprintf(output, output_size, "%s %08X", serial_uuid_str, crc);
}

struct thread_data {
    int sock;
    int thread_id;
};

void *connection_handler(void *arg) {
    struct thread_data *data = (struct thread_data*)arg;
    int sock = data->sock;
    int thread_id = data->thread_id;
    free(data);
    
    char buf[1024];
    int n;

    printf("[THREAD %d] Client connected\n", thread_id);

    while (1) {
        bzero(buf, sizeof(buf));
        n = recv(sock, buf, sizeof(buf), 0);
        
        if (n <= 0) {
            printf("[THREAD %d] Client disconnected\n", thread_id);
            break;
        }

        printf("[THREAD %d] Received: %s\n", thread_id, buf);

        if (strcmp(buf, "GET") == 0) {
            pthread_mutex_lock(&serial_mutex);
            printf("[THREAD %d] Processing GET\n", thread_id);
            
            char serial_uuid[128];
            generate_uuid_with_serial(serial_uuid, sizeof(serial_uuid));
            
            if (send(sock, serial_uuid, strlen(serial_uuid) + 1, 0) < 0) {
                pthread_mutex_unlock(&serial_mutex);
                break;
            }
            printf("[THREAD %d] Sent: %s\n", thread_id, serial_uuid);

            //////////////////  ZA TESTIRANJE  //////////////////////////////////////////////////////////////////////////////////////////
            sleep(5);


            bzero(buf, sizeof(buf));
            printf("[THREAD %d] Waiting for PREJETO\n", thread_id);
            
            n = recv(sock, buf, sizeof(buf), 0);
            
            if (n <= 0) {
                printf("[THREAD %d] Timeout/error\n", thread_id);
                pthread_mutex_unlock(&serial_mutex);
                break;
            }

            printf("[THREAD %d] Received: %s\n", thread_id, buf);

            char text[64], received_crc[16];
            if (sscanf(buf, "%63s %15s", text, received_crc) == 2 && 
                strcmp(text, "PREJETO") == 0) {
                
                uint32_t calc_crc = crc32_compute("PREJETO");
                char calc_crc_hex[16];
                snprintf(calc_crc_hex, sizeof(calc_crc_hex), "%08X", calc_crc);

                if (strcasecmp(calc_crc_hex, received_crc) == 0) {
                    printf("[THREAD %d] CRC OK - Counter incremented\n", thread_id);
                    save_counter(load_counter() + 1);
                } else {
                    printf("[THREAD %d] CRC mismatch\n", thread_id);
                    send(sock, "NAPAKA 4900B4DB", 16, 0);
                }
            } else {
                printf("[THREAD %d] Invalid format\n", thread_id);
                send(sock, "NAPAKA 4900B4DB", 16, 0);
            }

            pthread_mutex_unlock(&serial_mutex);

        } else if (strcmp(buf, "X") == 0) {
            printf("[THREAD %d] Shutdown\n", thread_id);
            close(sock);
            exit(0);
        } else {
            printf("[THREAD %d] Unknown command\n", thread_id);
            send(sock, "NEPREPOZNAVEN UKAZ", 19, 0);
        }
    }

    printf("[THREAD %d] Finished\n", thread_id);
    close(sock);
    return 0;
}

int main(int argc, char *argv[]) {
    int sock, client_sock;
    struct sockaddr_in server, client;
    socklen_t c = sizeof(struct sockaddr_in);

    srand((unsigned)time(NULL));

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) error("socket");

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(5095);

    if (bind(sock, (struct sockaddr *)&server, sizeof(server)) < 0)
        error("bind");

    listen(sock, 5);
    printf("Server started on port 5095\n");

    while ((client_sock = accept(sock, (struct sockaddr *)&client, &c))) {
        pthread_mutex_lock(&counter_mutex);
        int tid = ++thread_counter;
        pthread_mutex_unlock(&counter_mutex);

        printf("[MAIN] Connection %d accepted\n", tid);

        struct thread_data *data = malloc(sizeof(struct thread_data));
        data->sock = client_sock;
        data->thread_id = tid;

        pthread_t thread;
        if (pthread_create(&thread, NULL, connection_handler, data) < 0) {
            perror("pthread_create");
            free(data);
            continue;
        }
        pthread_detach(thread);
    }

    close(sock);
    return 0;
}