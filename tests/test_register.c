#include "test_common.h"
#include "mocks.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

#define UNIT_TESTING 1
#define send_sip_message mock_send_sip_message
#include "../sip_server.c"

static void build_register_message(sip_message_t *msg, const char *username,
                                   const char *contact_uri, const char *ip, int port,
                                   const char *call_id) {
    const char *domain = "example.com";
    memset(msg, 0, sizeof(*msg));
    snprintf(msg->buffer, BUFFER_SIZE,
             "REGISTER sip:%s SIP/2.0\r\n"
             "Via: SIP/2.0/UDP %s:%d;rport;branch=z9hG4bKreg\r\n"
             "From: <sip:%s@%s>;tag=tag1\r\n"
             "To: <sip:%s@%s>\r\n"
             "Call-ID: %s\r\n"
             "CSeq: 2 REGISTER\r\n"
             "Contact: <%s>\r\n"
             "Content-Length: 0\r\n\r\n",
             username, ip, port,
             username, domain,
             username, domain,
             call_id, contact_uri);
    msg->client_addr.sin_family = AF_INET;
    msg->client_addr_len = sizeof(msg->client_addr);
    inet_pton(AF_INET, ip, &msg->client_addr.sin_addr);
    msg->client_addr.sin_port = htons(port);
}

static int test_register_existing_user(void) {
    int failures = 0;
    mocks_reset();

    const char *user = "1001";
    location_entry_t *entry = find_location_entry_by_userid(user);
    EXPECT_TRUE(entry != NULL);
    if (entry == NULL) {
        return failures;
    }

    char original_ip[INET_ADDRSTRLEN];
    strncpy(original_ip, entry->ip_str, sizeof(original_ip));
    int original_port = entry->port;
    bool original_registered = entry->registered;

    sip_message_t reg;
    build_register_message(&reg, user, "sip:1001@10.0.0.5:5062", "10.0.0.5", 5062, "reg-001@example.com");

    handle_register(&reg);

    EXPECT_TRUE(entry->registered);
    EXPECT_TRUE(strcmp(entry->ip_str, "10.0.0.5") == 0);
    EXPECT_EQ_INT(entry->port, 5062);

    const mock_message_t *resp = mocks_find_payload_substr("SIP/2.0 200 OK");
    EXPECT_TRUE(resp != NULL);
    if (resp != NULL) {
        EXPECT_STRCONTAINS(resp->payload, "Contact: <sip:1001@10.0.0.5:5062>;expires=7200");
        EXPECT_STRCONTAINS(resp->payload, "Content-Length: 0");
    }

    strncpy(entry->ip_str, original_ip, sizeof(entry->ip_str));
    entry->port = original_port;
    entry->registered = original_registered;

    return failures;
}

static int test_register_unknown_user(void) {
    int failures = 0;
    mocks_reset();

    sip_message_t reg;
    build_register_message(&reg, "9999", "sip:9999@10.0.0.9:5090", "10.0.0.9", 5090, "reg-404@example.com");

    handle_register(&reg);

    const mock_message_t *resp = mocks_find_payload_substr("SIP/2.0 404 Not Found");
    EXPECT_TRUE(resp != NULL);
    if (resp != NULL) {
        EXPECT_STRCONTAINS(resp->payload, "Content-Length: 0");
    }

    return failures;
}

int main(void) {
    const test_case_t cases[] = {
        {"register_existing_user", test_register_existing_user},
        {"register_unknown_user", test_register_unknown_user},
    };

    test_stats_t stats;
    return run_tests(cases, sizeof(cases) / sizeof(cases[0]), &stats);
}
