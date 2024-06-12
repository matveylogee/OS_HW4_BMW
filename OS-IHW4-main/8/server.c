#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <semaphore.h>

#define ARRAY_SIZE 10
#define MAX_CLIENTS 5

int db[ARRAY_SIZE];
sem_t db_sem;
sem_t writer_sem;
int server_fd;

struct observer_info {
    struct sockaddr_in addr;
    socklen_t addr_len;
};

struct observer_info observer_clients[MAX_CLIENTS];
sem_t observer_sem;

void notify_observers(const char *message) {
    sem_wait(&observer_sem);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (observer_clients[i].addr_len != 0) {
            sendto(server_fd, message, strlen(message), 0,
                   (struct sockaddr *)&observer_clients[i].addr, observer_clients[i].addr_len);
        }
    }
    sem_post(&observer_sem);
}

void init_db() {
    for (int i = 0; i < ARRAY_SIZE; ++i) {
        db[i] = i + 1;
    }
}

void handle_request(char *request, struct sockaddr_in *client_addr, socklen_t client_len) {
    char buffer[1024];
    if (strncmp(request, "READ", 4) == 0) {
        int index = atoi(request + 5);
        int value;

        sem_wait(&db_sem);
        value = db[index];
        sem_post(&db_sem);

        char response[1024];
        snprintf(response, sizeof(response), "VALUE %d", value);
        sendto(server_fd, response, strlen(response), 0, (struct sockaddr *)client_addr, client_len);

        snprintf(response, sizeof(response), "read value %d from index %d", value, index);
        notify_observers(response);
    } else if (strncmp(request, "WRITE", 5) == 0) {
        int index, new_value;
        sscanf(request + 6, "%d %d", &index, &new_value);

        sem_wait(&writer_sem);
        sem_wait(&db_sem);
        int old_value = db[index];
        db[index] = new_value;
        sem_post(&db_sem);
        sem_post(&writer_sem);

        char response[1024];
        snprintf(response, sizeof(response), "UPDATED FROM %d TO %d", old_value, new_value);
        sendto(server_fd, response, strlen(response), 0, (struct sockaddr *)client_addr, client_len);

        char response_log[1024];
        snprintf(response_log, sizeof(response_log), "DB[%d] updated to %d (old value %d)", index, new_value, old_value);
        notify_observers(response_log);
    }
}

void signal_handler(int signal) {
    printf("Caught signal %d, terminating server...\n", signal);
    close(server_fd);
    sem_destroy(&db_sem);
    sem_destroy(&writer_sem);
    sem_destroy(&observer_sem);
    exit(0);
}

int main(int argc, char const *argv[]) {
    for(int i = 0; i < MAX_CLIENTS; ++i) {
        observer_clients[i].addr_len = 0;
    }

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <ip_address> <port>\n", argv[0]);
        return -1;
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);

    init_db();

    sem_init(&db_sem, 0, 1);
    sem_init(&writer_sem, 0, 1);
    sem_init(&observer_sem, 0, 1);

    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_DGRAM, 0)) == 0) {
        perror("socket() failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(server_ip);
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind() failed");
        exit(EXIT_FAILURE);
    }

    signal(SIGINT, signal_handler);

    printf("Server listening on <ip:port> %s:%d\n", server_ip, port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        char buffer[1024] = {0};
        int n = recvfrom(server_fd, buffer, sizeof(buffer), 0, (struct sockaddr *)&client_addr, &client_len);
        if (n > 0) {
            buffer[n] = '\0';

            if (strcmp(buffer, "OBSERVER") == 0) {
                sem_wait(&observer_sem);
                int i;
                for (i = 0; i < MAX_CLIENTS; ++i) {
                    if (observer_clients[i].addr_len == 0) {
                        observer_clients[i].addr = client_addr;
                        observer_clients[i].addr_len = client_len;
                        break;
                    }
                }
                sem_post(&observer_sem);
                printf("Observer connected\n");
            } else {
                handle_request(buffer, &client_addr, client_len);
            }
        } else {
            fprintf(stderr, "Failed to receive message\n");
        }
    }

    close(server_fd);
    sem_destroy(&db_sem);
    sem_destroy(&writer_sem);
    sem_destroy(&observer_sem);
    return 0;
}
