#ifndef TESTS_MOCKS_H
#define TESTS_MOCKS_H

#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

#include "../sip_server.h"

typedef struct {
    char payload[BUFFER_SIZE + 1];
    size_t len;
    struct sockaddr_storage addr;
    socklen_t addr_len;
} mock_message_t;

void mocks_reset(void);
size_t mocks_count(void);
const mock_message_t *mocks_get(size_t index);
const mock_message_t *mocks_find_payload_substr(const char *needle);

void mock_send_sip_message(const sip_message_t *message, const char *destination, int port);

#endif /* TESTS_MOCKS_H */
