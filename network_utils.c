/**
 * @file network_utils.c
 * @brief Implementation of network utility functions for the SIP server.
 */

#include "network_utils.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

/**
 * @brief Sends a SIP message to the specified destination and port.
 * 
 * @param message The SIP message to be sent.
 * @param destination The IP address of the destination.
 * @param port The port number on the destination.
 */
void send_sip_message(const sip_message_t *message, const char *destination, int port) {
    int sockfd;
    struct sockaddr_in dest_addr;
    struct sockaddr_in local_addr;
    socklen_t local_addr_len = sizeof(local_addr);
    char source_ip_str[INET_ADDRSTRLEN] = {0};

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        return;
    }

    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, destination, &dest_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        close(sockfd);
        return;
    }

    // Get local address from the socket
    getsockname(sockfd, (struct sockaddr *)&local_addr, &local_addr_len);

    // Convert source IP address to string format
    inet_ntop(AF_INET, &(local_addr.sin_addr), source_ip_str, INET_ADDRSTRLEN);

    // Send the message
    if (sendto(sockfd, message->buffer, strlen(message->buffer), 0,
               (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
        perror("Send failed");
    } else {
        //printf("\r\n===========================================================\r\n");
        //printf("Message sent to %s:%d, Source: %s:%d\r\n", destination, port, source_ip_str, ntohs(local_addr.sin_port));
        // printf("Raw SIP message :\r\n%s\r\n", message->buffer);
        // printf("==============================================================\r\n");
    }

    close(sockfd);
}