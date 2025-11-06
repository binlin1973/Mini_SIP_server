#include "mocks.h"

#include <string.h>
#include <arpa/inet.h>

#ifndef MOCK_HISTORY_SIZE
#define MOCK_HISTORY_SIZE 32
#endif

static mock_message_t history[MOCK_HISTORY_SIZE];
static size_t write_index = 0;
static size_t stored = 0;

void mocks_reset(void) {
    memset(history, 0, sizeof(history));
    write_index = 0;
    stored = 0;
}

size_t mocks_count(void) {
    return stored;
}

const mock_message_t *mocks_get(size_t index) {
    if (stored == 0 || index >= stored) {
        return NULL;
    }
    size_t start = (stored < MOCK_HISTORY_SIZE) ? 0 : (write_index % MOCK_HISTORY_SIZE);
    size_t actual_index = (start + index) % MOCK_HISTORY_SIZE;
    return &history[actual_index];
}

const mock_message_t *mocks_find_payload_substr(const char *needle) {
    if (needle == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < stored; ++i) {
        const mock_message_t *msg = mocks_get(i);
        if (msg != NULL && strstr(msg->payload, needle) != NULL) {
            return msg;
        }
    }
    return NULL;
}

static void record_message(const sip_message_t *message, const char *destination, int port) {
    mock_message_t *slot = &history[write_index % MOCK_HISTORY_SIZE];
    memset(slot, 0, sizeof(*slot));

    if (message != NULL) {
        strncpy(slot->payload, message->buffer, BUFFER_SIZE);
        slot->payload[BUFFER_SIZE] = '\0';
        size_t length = strlen(slot->payload);
        if (length > BUFFER_SIZE) {
            length = BUFFER_SIZE;
        }
        slot->len = length;
        slot->addr_len = sizeof(struct sockaddr_in);
        struct sockaddr_in *addr = (struct sockaddr_in *)&slot->addr;
        memset(addr, 0, sizeof(*addr));
        addr->sin_family = AF_INET;
        addr->sin_port = htons(port);
        if (destination != NULL) {
            inet_pton(AF_INET, destination, &addr->sin_addr);
        }
    }

    write_index = (write_index + 1) % MOCK_HISTORY_SIZE;
    if (stored < MOCK_HISTORY_SIZE) {
        stored++;
    }
}

void mock_send_sip_message(const sip_message_t *message, const char *destination, int port) {
    record_message(message, destination, port);
}
