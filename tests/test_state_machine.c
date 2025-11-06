#include "test_common.h"
#include "mocks.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

#define UNIT_TESTING 1
#define send_sip_message mock_send_sip_message
#include "../sip_server.c"

static void build_message(sip_message_t *msg, const char *payload, const char *ip, int port) {
    memset(msg, 0, sizeof(*msg));
    strncpy(msg->buffer, payload, BUFFER_SIZE);
    msg->buffer[BUFFER_SIZE] = '\0';
    msg->client_addr.sin_family = AF_INET;
    msg->client_addr_len = sizeof(msg->client_addr);
    inet_pton(AF_INET, ip, &msg->client_addr.sin_addr);
    msg->client_addr.sin_port = htons(port);
}

static int test_initial_invite_allocates_call(void) {
    int failures = 0;
    mocks_reset();
    init_call_map();

    const char *call_id = "call-001@example.com";
    sip_message_t invite;
    const char *payload =
        "INVITE sip:1002@example.com SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 10.0.0.1:5060;rport;branch=z9hG4bK111\r\n"
        "From: <sip:1001@example.com>;tag=aaa\r\n"
        "To: <sip:1002@example.com>\r\n"
        "Call-ID: call-001@example.com\r\n"
        "CSeq: 1 INVITE\r\n"
        "Contact: <sip:1001@10.0.0.1:5060>\r\n"
        "Content-Type: application/sdp\r\n"
        "Content-Length: 10\r\n\r\n0123456789";
    build_message(&invite, payload, "10.0.0.1", 5060);

    handle_state_machine(NULL, REQUEST_METHOD, "INVITE", true, &invite, invite.buffer, A_LEG);

    int leg = 0;
    call_t *call = find_call_by_callid(&call_map, call_id, &leg);
    EXPECT_TRUE(call != NULL);
    if (call != NULL) {
        EXPECT_EQ_INT(leg, A_LEG);
        EXPECT_EQ_INT(call->call_state, CALL_STATE_ROUTING);
        EXPECT_TRUE(strncmp(call->b_leg_uuid, "b-leg", 5) == 0);
        EXPECT_TRUE(call->is_active);
    }

    destroy_call_map();
    return failures;
}

static int test_b_leg_180_generates_response(void) {
    int failures = 0;
    mocks_reset();
    init_call_map();

    const char *call_id = "call-002@example.com";
    sip_message_t invite;
    const char *invite_payload =
        "INVITE sip:1003@example.com SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 10.0.0.1:5060;rport;branch=z9hG4bK222\r\n"
        "From: <sip:1001@example.com>;tag=bbb\r\n"
        "To: <sip:1003@example.com>\r\n"
        "Call-ID: call-002@example.com\r\n"
        "CSeq: 1 INVITE\r\n"
        "Contact: <sip:1001@10.0.0.1:5060>\r\n"
        "Content-Type: application/sdp\r\n"
        "Content-Length: 8\r\n\r\nABCDEFGH";
    build_message(&invite, invite_payload, "10.0.0.1", 5060);
    handle_state_machine(NULL, REQUEST_METHOD, "INVITE", true, &invite, invite.buffer, A_LEG);

    int leg = 0;
    call_t *call = find_call_by_callid(&call_map, call_id, &leg);
    EXPECT_TRUE(call != NULL);
    if (call == NULL) {
        destroy_call_map();
        return failures;
    }

    const char *b_leg_uuid = call->b_leg_uuid;
    mocks_reset();

    sip_message_t ringing;
    char ringing_payload[512];
    snprintf(ringing_payload, sizeof(ringing_payload),
             "SIP/2.0 180 Ringing\r\n"
             "Via: SIP/2.0/UDP 10.0.0.2:5070;branch=z9hG4bK333\r\n"
             "From: <sip:1003@example.com>;tag=ccc\r\n"
             "To: <sip:1001@example.com>;tag=ddd\r\n"
             "Call-ID: %s\r\n"
             "CSeq: 1 INVITE\r\n"
             "Content-Length: 0\r\n\r\n",
             b_leg_uuid);
    build_message(&ringing, ringing_payload, "10.0.0.2", 5070);

    leg = 0;
    call = find_call_by_callid(&call_map, b_leg_uuid, &leg);
    EXPECT_TRUE(call != NULL);
    EXPECT_EQ_INT(leg, B_LEG);
    handle_state_machine(call, STATUS_CODE, "180", false, &ringing, ringing.buffer, leg);

    EXPECT_EQ_INT(call->call_state, CALL_STATE_RINGING);

    const mock_message_t *response = mocks_find_payload_substr("SIP/2.0 180 Ringing");
    EXPECT_TRUE(response != NULL);
    if (response != NULL) {
        EXPECT_STRCONTAINS(response->payload, "Via: ");
        EXPECT_STRCONTAINS(response->payload, "From: ");
        EXPECT_STRCONTAINS(response->payload, "To: ");
        EXPECT_STRCONTAINS(response->payload, "Call-ID: ");
        EXPECT_STRCONTAINS(response->payload, "CSeq: ");
        EXPECT_STRCONTAINS(response->payload, "Content-Length: ");
    }

    destroy_call_map();
    return failures;
}

static int test_b_leg_failure_releases_call(void) {
    int failures = 0;
    mocks_reset();
    init_call_map();

    const char *call_id = "call-003@example.com";
    sip_message_t invite;
    const char *invite_payload =
        "INVITE sip:1004@example.com SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 10.0.0.1:5060;rport;branch=z9hG4bK444\r\n"
        "From: <sip:1001@example.com>;tag=eee\r\n"
        "To: <sip:1004@example.com>\r\n"
        "Call-ID: call-003@example.com\r\n"
        "CSeq: 1 INVITE\r\n"
        "Contact: <sip:1001@10.0.0.1:5060>\r\n"
        "Content-Type: application/sdp\r\n"
        "Content-Length: 20\r\n\r\n01234567890123456789";
    build_message(&invite, invite_payload, "10.0.0.1", 5060);
    handle_state_machine(NULL, REQUEST_METHOD, "INVITE", false, &invite, invite.buffer, A_LEG);

    int leg = 0;
    call_t *call = find_call_by_callid(&call_map, call_id, &leg);
    EXPECT_TRUE(call != NULL);
    if (call == NULL) {
        destroy_call_map();
        return failures;
    }

    const char *b_leg_uuid = call->b_leg_uuid;
    mocks_reset();

    sip_message_t failure;
    char failure_payload[512];
    snprintf(failure_payload, sizeof(failure_payload),
             "SIP/2.0 486 Busy Here\r\n"
             "Via: SIP/2.0/UDP 10.0.0.2:5070;branch=z9hG4bK555\r\n"
             "From: <sip:1004@example.com>;tag=fff\r\n"
             "To: <sip:1001@example.com>;tag=ggg\r\n"
             "Call-ID: %s\r\n"
             "CSeq: 1 INVITE\r\n"
             "Content-Length: 0\r\n\r\n",
             b_leg_uuid);
    build_message(&failure, failure_payload, "10.0.0.2", 5070);

    leg = 0;
    call = find_call_by_callid(&call_map, b_leg_uuid, &leg);
    EXPECT_TRUE(call != NULL);
    EXPECT_EQ_INT(leg, B_LEG);
    handle_state_machine(call, STATUS_CODE, "486", false, &failure, failure.buffer, leg);

    const mock_message_t *ack = mocks_find_payload_substr("ACK ");
    EXPECT_TRUE(ack != NULL);
    if (ack != NULL) {
        EXPECT_STRCONTAINS(ack->payload, "ACK sip:1004");
        EXPECT_STRCONTAINS(ack->payload, "Content-Length: 0");
    }

    const mock_message_t *err = mocks_find_payload_substr("SIP/2.0 486");
    EXPECT_TRUE(err != NULL);
    if (err != NULL) {
        EXPECT_STRCONTAINS(err->payload, "Call-ID: call-003@example.com");
    }

    call = find_call_by_callid(&call_map, call_id, &leg);
    EXPECT_TRUE(call == NULL);

    destroy_call_map();
    return failures;
}

int main(void) {
    const test_case_t cases[] = {
        {"initial_invite_allocates_call", test_initial_invite_allocates_call},
        {"b_leg_180_generates_response", test_b_leg_180_generates_response},
        {"b_leg_failure_releases_call", test_b_leg_failure_releases_call},
    };

    test_stats_t stats;
    return run_tests(cases, sizeof(cases) / sizeof(cases[0]), &stats);
}
