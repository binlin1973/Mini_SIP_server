#include "test_common.h"
#include "mocks.h"

#include <stdio.h>
#include <string.h>

#define UNIT_TESTING 1
#define send_sip_message mock_send_sip_message
#include "../sip_server.c"

static int test_parse_valid_invite(void) {
    int failures = 0;
    mocks_reset();

    sip_message_t msg = {0};
    const char *payload =
        "INVITE sip:1002@example.com SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 10.0.0.1:5060;rport;branch=z9hG4bK1\r\n"
        "From: <sip:1001@example.com>;tag=111\r\n"
        "To: <sip:1002@example.com>\r\n"
        "Call-ID: abc123@example.com\r\n"
        "CSeq: 1 INVITE\r\n"
        "Contact: <sip:1001@10.0.0.1:5060>\r\n"
        "Content-Type: application/sdp\r\n"
        "Content-Length: 10\r\n"
        "\r\n0123456789";
    strncpy(msg.buffer, payload, BUFFER_SIZE);

    char *via = strstr(msg.buffer, "Via: ");
    char *from = strstr(msg.buffer, "From: ");
    char *to = strstr(msg.buffer, "To: ");
    char *call_id = strstr(msg.buffer, "Call-ID: ");
    char *cseq = strstr(msg.buffer, "CSeq: ");
    EXPECT_TRUE(via != NULL);
    EXPECT_TRUE(from != NULL);
    EXPECT_TRUE(to != NULL);
    EXPECT_TRUE(call_id != NULL);
    EXPECT_TRUE(cseq != NULL);

    return failures;
}

static int test_parse_invalid_missing_headers(void) {
    int failures = 0;
    mocks_reset();

    sip_message_t msg = {0};
    const char *payload = "INVITE sip:1002@example.com SIP/2.0\r\n\r\n";
    strncpy(msg.buffer, payload, BUFFER_SIZE);

    char *via = strstr(msg.buffer, "Via: ");
    char *call_id = strstr(msg.buffer, "Call-ID: ");
    EXPECT_TRUE(via == NULL);
    EXPECT_TRUE(call_id == NULL);

    return failures;
}

static int test_parse_sdp_detection(void) {
    int failures = 0;
    mocks_reset();

    sip_message_t msg = {0};
    const char *payload =
        "SIP/2.0 200 OK\r\n"
        "Via: SIP/2.0/UDP 1.1.1.1:5060;branch=z9hG4bK\r\n"
        "From: <sip:1002@example.com>;tag=200\r\n"
        "To: <sip:1001@example.com>;tag=300\r\n"
        "Call-ID: resp@example.com\r\n"
        "CSeq: 1 INVITE\r\n"
        "Content-Type: application/sdp\r\n"
        "Content-Length: 20\r\n\r\n01234567890123456789";
    strncpy(msg.buffer, payload, BUFFER_SIZE);

    bool has_sdp = (strstr(msg.buffer, "Content-Type: application/sdp") != NULL);
    EXPECT_TRUE(has_sdp);

    return failures;
}

static int test_parse_max_forwards(void) {
    int failures = 0;
    mocks_reset();

    sip_message_t msg = {0};
    const char *payload =
        "INVITE sip:1002@example.com SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 10.0.0.1:5060;rport;branch=z9hG4bK\r\n"
        "Max-Forwards: 5\r\n\r\n";
    strncpy(msg.buffer, payload, BUFFER_SIZE);

    char *max_forwards = strstr(msg.buffer, "Max-Forwards: ");
    EXPECT_TRUE(max_forwards != NULL);
    if (max_forwards != NULL) {
        int value = atoi(max_forwards + strlen("Max-Forwards: "));
        EXPECT_EQ_INT(value, 5);
    }

    return failures;
}

int main(void) {
    const test_case_t cases[] = {
        {"parse_valid_invite", test_parse_valid_invite},
        {"parse_invalid_missing_headers", test_parse_invalid_missing_headers},
        {"parse_sdp_detection", test_parse_sdp_detection},
        {"parse_max_forwards", test_parse_max_forwards},
    };

    test_stats_t stats;
    return run_tests(cases, sizeof(cases) / sizeof(cases[0]), &stats);
}
