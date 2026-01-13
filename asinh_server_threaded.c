#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>
#include <pthread.h>

// Global mutex to serialize serial number generation
pthread_mutex_t serial_mutex = PTHREAD_MUTEX_INITIALIZER;

// Global thread counter
int thread_counter = 0;
pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

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
    } else {
        perror("Error opening counter file");
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
    char hex32[33];
    char uuid[37];

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
    
    // Create string with serial and UUID
    char serial_uuid_str[128];
    snprintf(serial_uuid_str, sizeof(serial_uuid_str), "%08d-%s", j+1, uuid);
    
    // Calculate CRC32 of the serial+UUID string
    uint32_t crc = crc32_compute(serial_uuid_str);
    
    // Format final output: serial-UUID CRC32
    snprintf(output, output_size, "%s %08X", serial_uuid_str, crc);
}

// Thread data structure
struct thread_data {
    int sock;
    int thread_id;
};

// Thread handler
void *connection_handler(void *arg)
{
    struct thread_data *data = (struct thread_data*)arg;
    int sock = data->sock;
    int thread_id = data->thread_id;
    free(data);
    
    char buf[1024];
    int n;

    printf("[THREAD %d] New client connected\n", thread_id);

    while (1) {
        bzero(buf, sizeof(buf));
        n = recv(sock, buf, sizeof(buf), 0);
        
        if (n <= 0) {
            if (n == 0) {
                printf("[THREAD %d] Client disconnected\n", thread_id);
            } else {
                perror("[THREAD] recv error");
            }
            break;
        }

        printf("[THREAD %d] Message from client: %s\n", thread_id, buf);
        sleep(1);

        // Check if "GET" command
        if (strcmp(buf, "GET") == 0) {
            // Lock mutex - only one thread can process serial number at a time
            pthread_mutex_lock(&serial_mutex);
            printf("[THREAD %d] Mutex locked - processing GET request\n", thread_id);
            
            char serial_uuid[128];
            generate_uuid_with_serial(serial_uuid, sizeof(serial_uuid));
            printf("[THREAD %d] Server response: %s\n", thread_id, serial_uuid);
            
            // Send response
            if (send(sock, serial_uuid, strlen(serial_uuid) + 1, 0) < 0) {
                perror("[THREAD] send failed");
                pthread_mutex_unlock(&serial_mutex);
                break;
            }

            sleep(1);

            // Wait for "PREJETO" acknowledgment
            bzero(buf, sizeof(buf));
            printf("[THREAD %d] Waiting for PREJETO acknowledgment...\n", thread_id);
            
            // Set timeout
            struct timeval tv; 
            tv.tv_sec = 10;
            tv.tv_usec = 0;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            
            n = recv(sock, buf, sizeof(buf), 0);
            
            // Reset to blocking
            tv.tv_sec = 0;
            tv.tv_usec = 0;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            
            if (n <= 0) {
                perror("[THREAD] recv error or timeout");
                pthread_mutex_unlock(&serial_mutex);
                break;
            }

            printf("[THREAD %d] Received acknowledgment: %s\n", thread_id, buf);
            sleep(1);

            // Parse "PREJETO" message and CRC32
            char text[64], received_crc[16];
            int parsed = sscanf(buf, "%63s %15s", text, received_crc);

            if (parsed == 2 && strcmp(text, "PREJETO") == 0) {
                uint32_t calculated_crc = crc32_compute("PREJETO");
                char calculated_crc_hex[16];
                snprintf(calculated_crc_hex, sizeof(calculated_crc_hex), "%08X", calculated_crc);

                printf("[THREAD %d] DEBUG: Calculated CRC: %s, Received CRC: %s\n",
                       thread_id, calculated_crc_hex, received_crc);
                sleep(1);

                if (strcasecmp(calculated_crc_hex, received_crc) == 0) {
                    printf("[THREAD %d] CRC32 verified successfully! Incrementing counter.\n", thread_id);
                    int j = load_counter();
                    save_counter(j + 1);
                } else {
                    printf("[THREAD %d] CRC32 mismatch! Counter not incremented.\n", thread_id);
                    const char error_msg[] = "NAPAKA 4900B4DB";
                    send(sock, error_msg, strlen(error_msg) + 1, 0);
                }
            } else {
                printf("[THREAD %d] Invalid format: parsed=%d\n", thread_id, parsed);
                const char error_msg[] = "NAPAKA 4900B4DB";
                send(sock, error_msg, strlen(error_msg) + 1, 0);
            }

            pthread_mutex_unlock(&serial_mutex);
            printf("[THREAD %d] Mutex unlocked\n", thread_id);

        } else if (strcmp(buf, "X") == 0) {
            printf("[THREAD %d] Shutdown signal received.\n", thread_id);
            close(sock);
            printf("[MAIN] Server shutting down...\n");
            exit(0);
        } else {
            printf("[THREAD %d] Unrecognized command: %s\n", thread_id, buf);
            const char error_msg[] = "NEPREPOZNAVEN UKAZ";
            send(sock, error_msg, strlen(error_msg) + 1, 0);
        }

        sleep(1);
    }

    printf("[THREAD %d] Thread finished\n", thread_id);
    close(sock);
    return 0;
}

int main(int argc, char *argv[]) {
    int sock, client_sock, c;
    struct sockaddr_in server, client;

    srand((unsigned)time(NULL));

    // Create TCP socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) error("Opening socket");

    // Prepare the sockaddr_in structure
    bzero((char *)&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(5095);

    // Bind
    if (bind(sock, (struct sockaddr *)&server, sizeof(server)) < 0)
        error("Binding");

    // Listen
    listen(sock, 5);

    printf("TCP server started on port 5095.\n");
    printf("Waiting for incoming connections...\n");

    c = sizeof(struct sockaddr_in);

    while ((client_sock = accept(sock, (struct sockaddr *)&client, (socklen_t*)&c))) {
        // Increment thread counter
        pthread_mutex_lock(&counter_mutex);
        thread_counter++;
        int current_thread_id = thread_counter;
        pthread_mutex_unlock(&counter_mutex);

        printf("[MAIN] Connection accepted - assigning to Thread %d\n", current_thread_id);

        pthread_t handler_thread;
        struct thread_data *data = malloc(sizeof(struct thread_data));
        data->sock = client_sock;
        data->thread_id = current_thread_id;

        if (pthread_create(&handler_thread, NULL, connection_handler, (void*)data) < 0) {
            perror("[MAIN] could not create thread");
            free(data);
            continue;
        }

        printf("[MAIN] Handler assigned to Thread %d\n", current_thread_id);
        pthread_detach(handler_thread);
    }

    if (client_sock < 0) {
        perror("accept failed");
        return 1;
    }

    close(sock);
    return 0;
}