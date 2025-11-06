# Mini_SIP_server (C)

[![CI](https://github.com/binlin1973/Mini_SIP_server/actions/workflows/ci.yml/badge.svg)](https://github.com/binlin1973/Mini_SIP_server/actions/workflows/ci.yml)

A **lightweight SIP signaling server** written in C.  
Implements the full SIP call flow — from the initial `INVITE`, through interim states (`100 Trying`, `180 Ringing`),  
to the final `200 OK` for `BYE`.  
It focuses purely on SIP **signaling and call control**, not RTP or media forwarding.

---

## 🧩 Overview

`Mini_SIP_server` is designed for **learning, testing, and VoIP integration**.  
It enables any standard SIP softphone (e.g., **Linphone**, **MicroSIP**, **Zoiper**) to register and make direct peer-to-peer calls.

The server runs as a **standalone lightweight binary**,  
storing registration and call state entirely in memory — no database or external dependency required.

---

## ⚙️ Build & Run

### 1. Configure Server IP

Before building, open [`sip_server.h`](./sip_server.h)  
and set your actual host IP address for `SIP_SERVER_IP_ADDRESS`:

#define SIP_SERVER_IP_ADDRESS "192.168.32.131"  // Example — replace with your machine's IP

### 2. Build

make distclean && make

### 3. Run

./build/bin/sip_server

By default, the server listens on UDP port 5060.

##  📱 Softphone Configuration

Any standard SIP softphone can register to this server.

### Setting	Example	Description
| Setting                | Example              | Description                 |
| ---------------------- | -------------------- | --------------------------- |
| **SIP Server / Proxy** | `192.168.32.131`    | Replace with your server IP |
| **Port**               | `5060`               | Default UDP port            |
| **Username**           | `1001` – `1006`      | Any user ID in this range   |
| **Password**           | any non-empty string | Password is not validated   |
| **Transport**          | `UDP`                | Required                    |


### Example (MicroSIP)
| Field        | Value           |
| ------------ | --------------- |
| Account name | 1001            |
| SIP server   | 192.168.32.131 |
| User         | 1001            |
| Domain       | 192.168.32.131 |
| Password     | 1234            |
| Transport    | UDP             |

After saving, the status should show Registered.

##  📞 Making a Call
Register two clients, e.g.:

Client A → 1001

Client B → 1002

From Client A, dial 1002

Client B will ring and can answer the call.

You’ll see the full SIP signaling printed in the server console:

INVITE → 100 Trying → 180 Ringing → 200 OK → ACK → BYE → 200 OK

##  🧠 Internal State Machine
For a deeper understanding of how SIP states transition through the call lifecycle,

see State_Machine_Design.pdf

##  🧪 run tests
make distclean && make && make test

## License
MIT License © Bin Lin

Lightweight, educational, and open to contributions.



