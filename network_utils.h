/**
 * @file network_utils.h
 * @brief Network utility functions for sending SIP messages.
 */

#ifndef NETWORK_UTILS_H
#define NETWORK_UTILS_H

#include "sip_server.h"

/**
 * @brief Sends a SIP message to a specified destination and port.
 * 
 * @param message The SIP message to be sent.
 * @param destination The IP address of the destination.
 * @param port The port number on the destination.
 */
void send_sip_message(const sip_message_t *message, const char *destination, int port);

#endif // NETWORK_UTILS_H
