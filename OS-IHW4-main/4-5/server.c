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

int database[ARRAY_SIZE];
sem_t db_sem;
sem_t writer_sem;
int server_fd;

void init_db() {
    for (int i = 1; i < ARRAY_SIZE + 1; ++i) {
        database[i - 1] = i;
    }
}

void handle_request(char *request, struct sockaddr_in *client_addr, socklen_t client_len) {
    char buffer[1024];
    if (strncmp(request, "READ", 4) == 0) {
        int index = atoi(request + 5);
        int value;

        sem_wait(&db_sem);
        value = database[index];
        sem_post(&db_sem);

        char response[1024];
        snprintf(response, sizeof(response), "VALUE %d", value);
        sendto(server_fd, response, strlen(response), 0, (struct sockaddr *)client_addr, client_len);
    } else if (strncmp(request, "WRITE", 5) == 0) {
        int index, new_value;
        sscanf(request + 6, "%d %d", &index, &new_value);

        sem_wait(&writer_sem);
        sem_wait(&db_sem);
        database[index] = new_value;
        sem_post(&db_sem);
        sem_post(&writer_sem);

        char *response = "UPDATED";
        sendto(server_fd, response, strlen(response), 0, (struct sockaddr *)client_addr, client_len);
    }
}

void signal_handler(int signal) {
    printf("Caught signal %d, terminating server...\n", signal);
    close(server_fd);
    exit(0);
}

int main(int argc, char const *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <ip-address> <port>\n", argv[0]);
        return -1;
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);

    init_db();

    sem_init(&db_sem, 0, 1);
    sem_init(&writer_sem, 0, 1);

    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_DGRAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt failed");
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
            handle_request(buffer, &client_addr, client_len);
        } else {
            fprintf(stderr, "Failed to receive message\n");
        }
    }

    close(server_fd);
    return 0;
}
