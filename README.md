# Mini_SIP_server (C)
A lightweight SIP server written in C, implementing the full SIP call flow—from the initial `INVITE` through interim states to the final `200 OK` response for `BYE`. Focuses solely on call setup and teardown.

### NOTE 1: Server IP
Before building, open `sip_server.h` and set `SIP_SERVER_IP_ADDRESS` to your server’s actual runtime IP:

#define SIP_SERVER_IP_ADDRESS "192.168.184.128"


### NOTE 2: 
    make distclean  && make
    ./build/bin/sip_server


For a deeper dive into the state machine and full usage instructions, see:

`State_Machine_Design.pdf`

`SIP_Server_Usage_Instructions.pdf`
