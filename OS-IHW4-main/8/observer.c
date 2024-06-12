#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#define BUFF_SIZE 2048  // Увеличиваем размер буфера

int client_socket;

void signal_handler(int signal) {
    printf("Caught signal %d, terminating monitor client...\n", signal);
    close(client_socket);
    exit(0);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        return -1;
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);

    client_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (client_socket == -1) {
        perror("Socket creation failed");
        return -1;
    }

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = inet_addr(server_ip);

    printf("Observer connected to server.\n");

    const char *handshake_message = "OBSERVER";
    if (sendto(client_socket, handshake_message, strlen(handshake_message), 0,
               (struct sockaddr *)&server_address, sizeof(server_address)) == -1) {
        perror("sendto() failed");
        close(client_socket);
        return -1;
    }

    signal(SIGINT, signal_handler);

    char buffer[BUFF_SIZE];
    socklen_t addr_len = sizeof(server_address);
    int bytes_received;
    while (1) {
        bytes_received = recvfrom(client_socket, buffer, BUFF_SIZE - 1, 0,
                                  (struct sockaddr *)&server_address, &addr_len);
        if (bytes_received < 0) {
            perror("Receive failed");
            break;
        }
        buffer[bytes_received] = '\0';
        printf("From server: %s\n", buffer);
    }

    close(client_socket);
    printf("Connection closed.\n");

    return 0;
}
