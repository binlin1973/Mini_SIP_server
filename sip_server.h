/**
 * @file sip_server.h
 * @brief Header for SIP server functionalities, including message processing and queue management.
 */

#ifndef SIP_SERVER_H
#define SIP_SERVER_H

#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <arpa/inet.h>

// The SIP server uses SIP_SERVER_IP_ADDRESS to generate its Via: and Contact: headers.
// *MUST* be set to your SIP server's interface address before compiling.
#define SIP_SERVER_IP_ADDRESS "192.168.32.131"

#define BUFFER_SIZE 1400
#define MAX_THREADS 5
#define QUEUE_CAPACITY 10
#define SIP_PORT 5060

#define MAX_CALLS 32                // Define max calls number
#define HEADER_SIZE 256             // Define the max size for header strings
#define AUTH_HEADER_SIZE 512        // Define the max size for header strings
#define MAX_UUID_LENGTH 128         // Define max uuid length
#define MAX_USERNAME_LENGTH 16      // Define username length
#define MAX_PASSWORD_LENGTH 16      // Define password length
#define MAX_REALM_LENGTH 16         // Define password length
#define MAX_NONCE_LENGTH 64         // Define nonce length
#define MAX_RESPONSE_LENGTH 64      // Define nonce length

// Define a-leg (also called O-leg) and b-leg (also called T-leg) macros
#define A_LEG 1
#define B_LEG 2

// Define message type macro
#define REQUEST_METHOD 1
#define STATUS_CODE 2

/**
 * @struct sip_message_t
 * @brief Structure to hold SIP message data and client address information.
 */
typedef struct {
    char buffer[BUFFER_SIZE + 1];
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
} sip_message_t;

/**
 * @struct message_queue_t
 * @brief Structure for a thread-safe message queue used by the SIP server.
 */
typedef struct {
    sip_message_t **messages;
    int capacity;
    int size;
    int front;
    int rear;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} message_queue_t;

/**
 * @struct worker_thread_t
 * @brief Structure for worker thread and its associated message queue.
 */
typedef struct {
    message_queue_t queue;
    pthread_t thread;
} worker_thread_t;

/**
 * @struct location_entry_t
 * @brief Structure to hold location information for a SIP user.
 */
typedef struct {
    char username[MAX_USERNAME_LENGTH];    // User's ID (e.g username, +1234567890), without domain and @, sip: or tel: prefix
    char password[MAX_PASSWORD_LENGTH];    // User's password (e.g., for authentication)
    char ip_str[INET_ADDRSTRLEN];          // IP address in string format
    int port;                              // Port number
    char realm[MAX_REALM_LENGTH];          // realm
    bool registered;                       // Registration status
} location_entry_t;

/**
 * @struct media_state_t
 * @brief Structure to hold media status.
 */
typedef struct {
    bool local_media;   // Flag to mark if has local media
    bool remote_media;  // Flag to mark if has remote media
} media_state_t;

/**
 * @enum call_state_t
 * @brief Define Call State Enums for O-Leg (A-leg) and T-Leg (B-leg).
 */
typedef enum {
    CALL_STATE_IDLE,
    CALL_STATE_ROUTING,
    CALL_STATE_RINGING,
    CALL_STATE_ANSWERED,
    CALL_STATE_CONNECTED,
    CALL_DISCONNECTING
} call_state_t;

/**
 * @struct sip_header_info_t
 * @brief Structure to hold SIP header information for each leg.
 */
typedef struct {
    char from[HEADER_SIZE];
    char via[HEADER_SIZE];
    char cseq[HEADER_SIZE];
    char to[HEADER_SIZE];
} sip_header_info_t;

/**
 * @struct call_t
 * @brief Structure to hold call specific data.
 */
typedef struct {
    char a_leg_uuid[MAX_UUID_LENGTH];              // A-leg uuid
    char b_leg_uuid[MAX_UUID_LENGTH];              // B-leg uuid
    //channel_state_t a_leg_state;                 // A-leg state, not necessary for this minimal demo 
    //channel_state_t b_leg_state;                 // B-leg state, not necessary for this minimal demo 
    call_state_t call_state;                       // Current call state
    media_state_t a_leg_media;                     // media state of A-leg
    media_state_t b_leg_media;                     // media state of B-leg
    char a_leg_ip_str[INET_ADDRSTRLEN];            // A-leg client IP address string for send_sip_message
    char b_leg_ip_str[INET_ADDRSTRLEN];            // B-leg client IP address string for send_sip_message    
    int  a_leg_port;                               // A-leg client port for send_sip_message
    int  b_leg_port;                               // B-leg client port for send_sip_message 
    int index;                                     // Index of the call struct in call map
    sip_header_info_t a_leg_header;                // Store a-leg SIP header
    sip_header_info_t b_leg_header;                // Store b-leg SIP header
    char caller[32];                               // Caller
    char callee[32];                               // Callee
    char a_leg_contact[HEADER_SIZE];               // Store A-leg Contact header
    char b_leg_contact[HEADER_SIZE];               // Store B-leg Contact header    
    bool is_active;                                // Flag to mark if the call struct is active
    pthread_mutex_t mutex;                         // Mutex for call, needed for multi-threaded queue processing with high concurrency, not necessary for single queue
} call_t;

/**
 * @struct call_map_t
 * @brief Structure to hold all call data.
 */
typedef struct {
    call_t calls[MAX_CALLS]; 
    int size;
    pthread_mutex_t mutex;
} call_map_t;

// Declare the global call map
extern call_map_t call_map;

void* process_sip_messages(void* arg);
void initialize_message_queue(message_queue_t *queue, int capacity);
void destroy_message_queue(message_queue_t *queue);
int enqueue_message(message_queue_t *queue, sip_message_t *message);
int dequeue_message(message_queue_t *queue, sip_message_t **message);
void init_call_map(); // Declare the initialization function
void destroy_call_map();
call_t* find_call_by_callid(call_map_t *call_map, const char *call_id, int *leg_type);
call_t* allocate_new_call(call_map_t *call_map);
void init_call(call_t *call, int index);
void handle_state_machine(call_t *call, int message_type, const char *method_or_code, bool has_sdp, sip_message_t *message, const char *raw_sip_message, int leg_type);
location_entry_t* find_location_entry_by_userid(const char *uri);

#endif // SIP_SERVER_H