/**
 * @file main.c
 * @brief Main entry point for the SIP server.
 */

#include "sip_server.h"
#include "network_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>


worker_thread_t worker_threads[MAX_THREADS];

void setup_server_socket(int *server_socket, struct sockaddr_in *server_addr);
void handle_new_message(int server_socket);

int server_socket;

int main() {
    
    struct sockaddr_in server_addr;

    // Setup server socket
    setup_server_socket(&server_socket, &server_addr);

    // Initialize worker threads and their queues
    for (int i = 0; i < MAX_THREADS; i++) {
        initialize_message_queue(&worker_threads[i].queue, QUEUE_CAPACITY);
        if (pthread_create(&worker_threads[i].thread, NULL, process_sip_messages, &worker_threads[i].queue) != 0) {
            perror("Failed to create worker thread");
            close(server_socket);
            exit(EXIT_FAILURE);
        }
    }

    // Main server loop
    while (1) {
        handle_new_message(server_socket);
    }

    // Cleanup (not reached in current setup)
    for (int i = 0; i < MAX_THREADS; i++) {
        pthread_join(worker_threads[i].thread, NULL);
        destroy_message_queue(&worker_threads[i].queue);
    }
    close(server_socket);

    return 0;
}

void setup_server_socket(int *server_socket, struct sockaddr_in *server_addr) {
    *server_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (*server_socket < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int flags = fcntl(*server_socket, F_GETFL, 0);
    if (flags == -1 || fcntl(*server_socket, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("Failed to set non-blocking socket");
        close(*server_socket);
        exit(EXIT_FAILURE);
    }

    memset(server_addr, 0, sizeof(struct sockaddr_in));
    server_addr->sin_family = AF_INET;
    server_addr->sin_addr.s_addr = INADDR_ANY;
    server_addr->sin_port = htons(SIP_PORT);

    if (bind(*server_socket, (struct sockaddr *)server_addr, sizeof(struct sockaddr_in)) < 0) {
        perror("Socket bind failed");
        close(*server_socket);
        exit(EXIT_FAILURE);
    }

    printf("SIP server started on port %d (non-blocking mode)\n", SIP_PORT);
}

void handle_new_message(int server_socket) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(server_socket, &read_fds);

    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };

    if (select(server_socket + 1, &read_fds, NULL, NULL, &tv) < 0) {
        perror("Select error");
        return;
    }

    if (FD_ISSET(server_socket, &read_fds)) {
        sip_message_t *message = malloc(sizeof(sip_message_t));
        if (message == NULL) {
            fprintf(stderr, "Memory allocation failed\n");
            return;
        }

        message->client_addr_len = sizeof(message->client_addr);
        ssize_t bytes_received = recvfrom(server_socket, message->buffer, BUFFER_SIZE, 0,
                                          (struct sockaddr *)&message->client_addr, &message->client_addr_len);

        if (bytes_received > 0) {
            message->buffer[bytes_received] = '\0';

            // TODO: update on how the thread should be selected based on your
            // routing logic
            int selected_thread = 0;
            if (!enqueue_message(&worker_threads[selected_thread].queue, message)) {
                fprintf(stderr, "Failed to enqueue message\n");
                free(message);
            }
        } else {
            if (bytes_received < 0 && errno != EWOULDBLOCK) {
                perror("Error receiving data");
            }
            free(message);
        }
    }
}
