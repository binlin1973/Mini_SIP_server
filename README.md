# Mini_SIP_server (C)
A lightweight SIP server written in C, implementing the full SIP call flow—from the initial `INVITE` through interim states to the final `200 OK` response for `BYE`. Focuses solely on call setup and teardown.

### NOTE 1: Server IP
Before building, open `sip_server.c` and set `SIP_SERVER_IP_ADDRESS` to your server’s actual runtime IP:

#define SIP_SERVER_IP_ADDRESS "192.168.184.128"


### NOTE 2: Registered SIP Phones

The array below lists the only SIP phone numbers that can register. To add or change users, simply edit the username entries (e.g., “1001”–“1008”). Do not touch the default ip_str and port values — these will be automatically overwritten with each phone’s actual IP and port upon REGISTER.

location_entry_t location_entries[] = {
    {"1001","defaultpassword", "192.168.192.1", 5060, SIP_SERVER_IP_ADDRESS, false},
    {"1002","defaultpassword",  "192.168.192.1", 5070, SIP_SERVER_IP_ADDRESS, false},
    {"1003","defaultpassword",  "192.168.1.103", 5060, SIP_SERVER_IP_ADDRESS, false},
    {"1004","defaultpassword",  "192.168.1.104", 5060, SIP_SERVER_IP_ADDRESS, false},
    {"1005","defaultpassword",  "192.168.184.1", 5060, SIP_SERVER_IP_ADDRESS, false},
    {"1006","defaultpassword",  "192.168.184.1", 5070, SIP_SERVER_IP_ADDRESS, false},
    {"1007","defaultpassword",  "192.168.1.4", 5060, SIP_SERVER_IP_ADDRESS, false},
    {"1008","defaultpassword",  "192.168.1.4", 5070, SIP_SERVER_IP_ADDRESS, false},
};


For a deeper dive into our state machine and full usage instructions, see:

State Machine Design.pdf

SIP_Server_Usage_Instructions.pdf
