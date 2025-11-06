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

static int active_call_count(void) {
    int count = 0;
    for (int i = 0; i < MAX_CALLS; ++i) {
        if (call_map.calls[i].is_active) {
            count++;
        }
    }
    return count;
}

static int test_full_call_flow(void) {
    int failures = 0;
    mocks_reset();
    init_call_map();

    const char *call_id_a = "flow-001@example.com";
    sip_message_t invite_a;
    const char *invite_payload =
        "INVITE sip:1002@example.com SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 10.0.0.1:5060;rport;branch=z9hG4bKflow1\r\n"
        "From: <sip:1001@example.com>;tag=aaa\r\n"
        "To: <sip:1002@example.com>\r\n"
        "Call-ID: flow-001@example.com\r\n"
        "CSeq: 1 INVITE\r\n"
        "Contact: <sip:1001@10.0.0.1:5060>\r\n"
        "Content-Type: application/sdp\r\n"
        "Content-Length: 129\r\n\r\n"
        "v=0\r\n"
        "o=- 0 0 IN IP4 10.0.0.1\r\n"
        "s=-\r\n"
        "c=IN IP4 10.0.0.1\r\n"
        "t=0 0\r\n"
        "m=audio 4000 RTP/AVP 0\r\n"
        "a=rtpmap:0 PCMU/8000\r\n";
    build_message(&invite_a, invite_payload, "10.0.0.1", 5060);
    handle_state_machine(NULL, REQUEST_METHOD, "INVITE", true, &invite_a, invite_a.buffer, A_LEG);

    int leg = 0;
    call_t *call = find_call_by_callid(&call_map, call_id_a, &leg);
    EXPECT_TRUE(call != NULL);
    if (call == NULL) {
        destroy_call_map();
        return failures;
    }

    const char *call_id_b = call->b_leg_uuid;

    sip_message_t ringing_b;
    char ringing_payload[512];
    snprintf(ringing_payload, sizeof(ringing_payload),
             "SIP/2.0 180 Ringing\r\n"
             "Via: SIP/2.0/UDP 10.0.0.2:5070;branch=z9hG4bKflow2\r\n"
             "From: <sip:1002@example.com>;tag=bbb\r\n"
             "To: <sip:1001@example.com>;tag=ccc\r\n"
             "Call-ID: %s\r\n"
             "CSeq: 1 INVITE\r\n"
             "Content-Length: 0\r\n\r\n",
             call_id_b);
    build_message(&ringing_b, ringing_payload, "10.0.0.2", 5070);
    leg = 0;
    call = find_call_by_callid(&call_map, call_id_b, &leg);
    handle_state_machine(call, STATUS_CODE, "180", false, &ringing_b, ringing_b.buffer, leg);

    sip_message_t ok_b;
    char ok_payload[1024];
    snprintf(ok_payload, sizeof(ok_payload),
             "SIP/2.0 200 OK\r\n"
             "Via: SIP/2.0/UDP 10.0.0.2:5070;branch=z9hG4bKflow3\r\n"
             "From: <sip:1002@example.com>;tag=bbb\r\n"
             "To: <sip:1001@example.com>;tag=ccc\r\n"
             "Call-ID: %s\r\n"
             "CSeq: 1 INVITE\r\n"
             "Contact: <sip:1002@10.0.0.2:5070>\r\n"
             "Content-Type: application/sdp\r\n"
             "Content-Length: 120\r\n\r\n"
             "v=0\r\n"
             "o=- 0 0 IN IP4 10.0.0.2\r\n"
             "s=-\r\n"
             "c=IN IP4 10.0.0.2\r\n"
             "t=0 0\r\n"
             "m=audio 5000 RTP/AVP 0\r\n"
             "a=rtpmap:0 PCMU/8000\r\n",
             call_id_b);
    build_message(&ok_b, ok_payload, "10.0.0.2", 5070);
    leg = 0;
    call = find_call_by_callid(&call_map, call_id_b, &leg);
    handle_state_machine(call, STATUS_CODE, "200", true, &ok_b, ok_b.buffer, leg);

    sip_message_t ack_a;
    const char *ack_payload =
        "ACK sip:1002@example.com SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 10.0.0.1:5060;branch=z9hG4bKflow4\r\n"
        "From: <sip:1001@example.com>;tag=aaa\r\n"
        "To: <sip:1002@example.com>;tag=ccc\r\n"
        "Call-ID: flow-001@example.com\r\n"
        "CSeq: 1 ACK\r\n"
        "Content-Length: 0\r\n\r\n";
    build_message(&ack_a, ack_payload, "10.0.0.1", 5060);
    leg = 0;
    call = find_call_by_callid(&call_map, call_id_a, &leg);
    handle_state_machine(call, REQUEST_METHOD, "ACK", false, &ack_a, ack_a.buffer, leg);

    sip_message_t bye_a;
    const char *bye_payload =
        "BYE sip:1002@example.com SIP/2.0\r\n"
        "Via: SIP/2.0/UDP 10.0.0.1:5060;branch=z9hG4bKflow5\r\n"
        "From: <sip:1001@example.com>;tag=aaa\r\n"
        "To: <sip:1002@example.com>;tag=ccc\r\n"
        "Call-ID: flow-001@example.com\r\n"
        "CSeq: 2 BYE\r\n"
        "Content-Length: 0\r\n\r\n";
    build_message(&bye_a, bye_payload, "10.0.0.1", 5060);
    leg = 0;
    call = find_call_by_callid(&call_map, call_id_a, &leg);
    handle_state_machine(call, REQUEST_METHOD, "BYE", false, &bye_a, bye_a.buffer, leg);

    sip_message_t ok_bye;
    char ok_bye_payload[512];
    snprintf(ok_bye_payload, sizeof(ok_bye_payload),
             "SIP/2.0 200 OK\r\n"
             "Via: SIP/2.0/UDP 10.0.0.2:5070;branch=z9hG4bKflow6\r\n"
             "From: <sip:1002@example.com>;tag=bbb\r\n"
             "To: <sip:1001@example.com>;tag=ccc\r\n"
             "Call-ID: %s\r\n"
             "CSeq: 2 BYE\r\n"
             "Content-Length: 0\r\n\r\n",
             call_id_b);
    build_message(&ok_bye, ok_bye_payload, "10.0.0.2", 5070);
    leg = 0;
    call = find_call_by_callid(&call_map, call_id_b, &leg);
    handle_state_machine(call, STATUS_CODE, "200", false, &ok_bye, ok_bye.buffer, leg);

    EXPECT_EQ_INT(active_call_count(), 0);

    const mock_message_t *invite_b = mocks_find_payload_substr("INVITE sip:1002@");
    EXPECT_TRUE(invite_b != NULL);
    if (invite_b != NULL) {
        EXPECT_STRCONTAINS(invite_b->payload, call_id_b);
        EXPECT_STRCONTAINS(invite_b->payload, "CSeq: 1 INVITE");
        EXPECT_STRCONTAINS(invite_b->payload, "Content-Length: 129");
    }

    const mock_message_t *ack_b = mocks_find_payload_substr("ACK sip:1002@");
    EXPECT_TRUE(ack_b != NULL);
    if (ack_b != NULL) {
        EXPECT_STRCONTAINS(ack_b->payload, call_id_b);
        EXPECT_STRCONTAINS(ack_b->payload, "CSeq: 1 ACK");
        EXPECT_STRCONTAINS(ack_b->payload, "Content-Length: 0");
    }

    const mock_message_t *bye_b = mocks_find_payload_substr("BYE sip:1002@");
    EXPECT_TRUE(bye_b != NULL);
    if (bye_b != NULL) {
        EXPECT_STRCONTAINS(bye_b->payload, call_id_b);
        EXPECT_STRCONTAINS(bye_b->payload, "CSeq: 2 BYE");
        EXPECT_STRCONTAINS(bye_b->payload, "Content-Length: 0");
    }

    const mock_message_t *ok_a = mocks_find_payload_substr("SIP/2.0 200 OK\r\nVia: SIP/2.0/UDP 10.0.0.1:5060");
    EXPECT_TRUE(ok_a != NULL);
    if (ok_a != NULL) {
        EXPECT_STRCONTAINS(ok_a->payload, call_id_a);
    }

    destroy_call_map();
    return failures;
}

int main(void) {
    const test_case_t cases[] = {
        {"full_call_flow", test_full_call_flow},
    };

    test_stats_t stats;
    return run_tests(cases, sizeof(cases) / sizeof(cases[0]), &stats);
}
