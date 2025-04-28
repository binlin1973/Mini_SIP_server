/** 
 * @file sip_server.c
 * @brief Implementation of SIP server functionalities, including message processing and queue management.
 */

#include "sip_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>
#include <arpa/inet.h> 
#include "network_utils.h"


// Each entry represents a softphone/UE with its corresponding phone number, IP address, and SIP port,
// so that they can be correctly reached as the called party by the SIP server.
// *MUST* be set before compiling.
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

int location_size = sizeof(location_entries) / sizeof(location_entry_t);

// Define the global call map
call_map_t call_map;

// Define global cseq number
int cseq_number = 1;

/**
 * @brief Initializes a message queue.
 * @param queue Pointer to the message queue to initialize.
 * @param capacity The maximum number of messages the queue can hold.
 */
void initialize_message_queue(message_queue_t *queue, int capacity) {
    queue->messages = malloc(sizeof(sip_message_t*) * capacity);
    queue->capacity = capacity;
    queue->size = 0;
    queue->front = 0;
    queue->rear = -1;
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);
}

/**
 * @brief Destroys a message queue, freeing its resources.
 * @param queue Pointer to the message queue to destroy.
 */
void destroy_message_queue(message_queue_t *queue) {
    for (int i = 0; i < queue->size; i++) {
        free(queue->messages[(queue->front + i) % queue->capacity]);
    }
    free(queue->messages);
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);
}

/**
 * @brief Enqueues a message into the queue.
 * @param queue Pointer to the message queue where the message will be enqueued.
 * @param message Pointer to the message to enqueue.
 * @return 1 on success, 0 if the queue is full.
 */
int enqueue_message(message_queue_t *queue, sip_message_t *message) {
    pthread_mutex_lock(&queue->mutex);
    if (queue->size == queue->capacity) {
        pthread_mutex_unlock(&queue->mutex);
        return 0;
    }

    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->messages[queue->rear] = message;
    queue->size++;

    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
    return 1;
}

/**
 * @brief Dequeues a message from the queue.
 * @param queue Pointer to the message queue to dequeue from.
 * @param message Double pointer to store the dequeued message.
 * @return 1 on success, 0 if the queue is empty.
 */
int dequeue_message(message_queue_t *queue, sip_message_t **message) {
    pthread_mutex_lock(&queue->mutex);

    while (queue->size == 0) {
        pthread_cond_wait(&queue->cond, &queue->mutex);
    }

    *message = queue->messages[queue->front];
    queue->front = (queue->front + 1) % queue->capacity;
    queue->size--;

    pthread_mutex_unlock(&queue->mutex);
    return 1;
}

// Function to extract CSeq number from SIP message
int extract_cseq_number(const char *cseq_header) {
    // Extract the CSeq number
    if (cseq_header == NULL) return -1;

    char num_str[HEADER_SIZE] = {0};
    const char *p = cseq_header;
    int num_str_idx = 0;
    int cseq_value = 1; 
    // Skip spaces and non-digit characters before the number
    while(*p != '\0' && !isdigit(*p)) {
      p++;
     }

    // If *p is a digit, copy the number to num_str
    while (*p != '\0' && isdigit(*p) && num_str_idx < (HEADER_SIZE-1)) {
         num_str[num_str_idx++] = *p++;
    }
    num_str[num_str_idx] = '\0'; 

    if (num_str[0] != '\0') { 
        cseq_value = atoi(num_str);
    }
    return cseq_value;
}

/**
 * @brief Generate a random nonce string.
 * @param nonce A pointer to the buffer where the nonce will be stored.
 * @param len The length of the nonce to be generated (including the null terminator).
 */
void generate_nonce(char *nonce, int len) {
    static int initialized = 0;
    if (!initialized) {
        srand(time(NULL));
        initialized = 1;
    }
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < len - 1; ++i) {
        nonce[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    nonce[len - 1] = '\0';
}

/**
 * @brief Parse the Authorization header to extract username, response, nonce and realm.
 * @param authorization_header A pointer to the authorization header string.
 * @param username A pointer to the buffer to store the extracted username.
 * @param response A pointer to the buffer to store the extracted response.
 * @param nonce A pointer to the buffer to store the extracted nonce.
 * @param realm A pointer to the buffer to store the extracted realm.
 * @return True if parsing is successful, false otherwise.
 */
bool parse_authorization_header(const char *authorization_header, char *username, char *response, char *nonce, char *realm) {
    //Authorization: Digest username="1001", realm="example.com", nonce="b66d7869c7290137ab34652e0c5e839e", uri="sip:example.com", response="8b6793a25625b7b764623f56a71014d5"
    char *username_start = strstr(authorization_header, "username=\"");
    if (username_start == NULL) return false;
    username_start += strlen("username=\"");
    char *username_end = strchr(username_start, '"');
    if (username_end == NULL) return false;
    
    size_t username_len = username_end - username_start;
    if(username_len >= MAX_USERNAME_LENGTH) return false;
    strncpy(username, username_start, username_len);
    username[username_len] = '\0';

    char *realm_start = strstr(authorization_header, "realm=\"");
    if (realm_start == NULL) return false;
     realm_start += strlen("realm=\"");
    char *realm_end = strchr(realm_start, '"');
     if (realm_end == NULL) return false;
    
    size_t realm_len = realm_end - realm_start;
    if(realm_len >= MAX_REALM_LENGTH) return false;
    strncpy(realm, realm_start, realm_len);
    realm[realm_len] = '\0';

    char *nonce_start = strstr(authorization_header, "nonce=\"");
    if (nonce_start == NULL) return false;
    nonce_start += strlen("nonce=\"");
    char *nonce_end = strchr(nonce_start, '"');
    if (nonce_end == NULL) return false;

    size_t nonce_len = nonce_end - nonce_start;
    if(nonce_len >= MAX_NONCE_LENGTH) return false;
    strncpy(nonce, nonce_start, nonce_len);
    nonce[nonce_len] = '\0';

    char *response_start = strstr(authorization_header, "response=\"");
    if (response_start == NULL) return false;
    response_start += strlen("response=\"");
     char *response_end = strchr(response_start, '"');
    if (response_end == NULL) return false;
    
    size_t response_len = response_end - response_start;
    if (response_len >= MAX_RESPONSE_LENGTH) return false;
    strncpy(response, response_start, response_len);
    response[response_len] = '\0';

    return true;
}

/**
 * @brief Handles SIP REGISTER messages.
 *
 * This function processes incoming REGISTER messages. It checks whether the user exists,
 * and if the user is found, registers the user, sends a 200 OK response,
 * and records the source address and source port of the incoming request.
 * If the user is not found, it sends a 404 Not Found response.
 * Authentication (e.g., Digest) is currently not performed but will be implemented in the future (TODO).
 * @param message A pointer to the sip_message_t struct containing the received data
 * @return 0 for success, -1 if message is invalid
 */
int handle_register(sip_message_t *message) {

    char *from_start = strstr(message->buffer, "From: ");
    char *via_start = strstr(message->buffer, "Via: ");
    char *cseq_start = strstr(message->buffer, "CSeq: ");
    char *to_start = strstr(message->buffer, "To: ");
    char *call_id_start = strstr(message->buffer, "Call-ID: ");
    char *contact_start = strstr(message->buffer, "Contact: ");

    char from_header[HEADER_SIZE] = {0};
    char via_header[HEADER_SIZE] = {0};
    char cseq_header[HEADER_SIZE] = {0};
    char to_header[HEADER_SIZE] = {0};
    char call_id_header[HEADER_SIZE] = {0};
    char contact_header[HEADER_SIZE] = {0};
    
    char response_buffer[BUFFER_SIZE] = {0};

    printf("Extracted Headers: \r\n");

    if (via_start != NULL) {
        char *via_end = strstr(via_start, "\r\n");
        if (via_end != NULL) {
            size_t via_len = via_end - via_start;
            strncpy(via_header, via_start, via_len);
            via_header[via_len] = '\0';
            printf("[%s]\r\n", via_header);
        }
    }

    if (from_start != NULL) {
    char *from_end = strstr(from_start, "\r\n");
        if (from_end != NULL) {
            size_t from_len = from_end - from_start;
            strncpy(from_header, from_start, from_len);
            from_header[from_len] = '\0';
            printf("[%s]\r\n", from_header);
        }
    }

    if (to_start != NULL) {
        char *to_end = strstr(to_start, "\r\n");
        if (to_end != NULL) {
            size_t to_len = to_end - to_start;
            strncpy(to_header, to_start, to_len);
            to_header[to_len] = '\0';
            printf("[%s]\r\n", to_header);
        }
    }

    if (cseq_start != NULL) {
        char *cseq_end = strstr(cseq_start, "\r\n");
        if (cseq_end != NULL) {
            size_t cseq_len = cseq_end - cseq_start;
            strncpy(cseq_header, cseq_start, cseq_len);
            cseq_header[cseq_len] = '\0';
            printf("[%s]\r\n", cseq_header);
        }
    }

    if (call_id_start != NULL) {
        char *call_id_end = strstr(call_id_start, "\r\n");
        if (call_id_end != NULL) {
            size_t call_id_len = call_id_end - call_id_start;
            strncpy(call_id_header, call_id_start, call_id_len);
            call_id_header[call_id_len] = '\0';
            printf("[%s]\r\n", call_id_header);
        }
    }

    if (contact_start != NULL) {
        char *contact_end = strstr(contact_start, "\r\n");
        if (contact_end != NULL) {
            size_t contact_len = contact_end - contact_start;
            strncpy(contact_header, contact_start, contact_len);
            contact_header[contact_len] = '\0';
            printf("[%s]\r\n", contact_header);
        }
    }

    char *from_header_user_start = strstr(from_header, "sip:");
    if (from_header_user_start == NULL) return -1;
    from_header_user_start += strlen("sip:");
    char *from_header_user_end = strchr(from_header_user_start, '@');
    if (from_header_user_end == NULL) return -1;
  
    size_t username_len = from_header_user_end - from_header_user_start;
    if (username_len >= MAX_USERNAME_LENGTH) {
        return -1; // Username too long
    }
    char username[MAX_USERNAME_LENGTH] = {0};
    strncpy(username, from_header_user_start, username_len);
    username[username_len] = '\0';

    location_entry_t *user = find_location_entry_by_userid(username);
    if (user == NULL) {
         snprintf(response_buffer, BUFFER_SIZE,
                 "SIP/2.0 404 Not Found\r\n"
                 "%s\r\n"
                 "%s\r\n"
                 "%s\r\n"
                 "%s\r\n"
                 "%s\r\n"
                 "Content-Length: 0\r\n\r\n",
                 via_header,
                 from_header,
                 to_header,
                 call_id_header,
                 cseq_header
            );
        printf("User '%s' not found. Sending 404 Not Found.\n", username);
        
        //printf("\r\n===========================================================\r\n");
        printf("Tx SIP message 404 Not Found:\r\n%s\r\n", response_buffer);
        //printf("==============================================================\r\n");
        sip_message_t response;
        memset(&response, 0, sizeof(response));
        strncpy(response.buffer, response_buffer, BUFFER_SIZE-1);
        send_sip_message(&response, inet_ntoa(message->client_addr.sin_addr), ntohs(message->client_addr.sin_port));

       return 0;
    }
   

    char temp_ip[INET_ADDRSTRLEN] = {0};
    int temp_port = 0;

    inet_ntop(AF_INET, &(message->client_addr.sin_addr), temp_ip, INET_ADDRSTRLEN);
    temp_port = ntohs(message->client_addr.sin_port);
    strncpy(user->ip_str, temp_ip, INET_ADDRSTRLEN - 1);
    user->port = temp_port;
    user->registered = true;
    printf("User %s registered successfully from %s:%d\n", user->username, user->ip_str, user->port);
    printf("Location entry for user '%s' updated to IP: %s, Port: %d\n", user->username, user->ip_str, user->port);

   snprintf(response_buffer, BUFFER_SIZE,
        "SIP/2.0 200 OK\r\n"
        "%.*s\r\n"
        "%.*s\r\n"
        "%.*s\r\n"
        "%.*s\r\n"
        "%.*s\r\n"
         "%.*s;expires=7200\r\n"
         "Content-Length: 0\r\n\r\n",
        (int)strlen(via_header), via_header,
        (int)strlen(from_header), from_header,
        (int)strlen(to_header), to_header,
        (int)strlen(call_id_header), call_id_header,
        (int)strlen(cseq_header), cseq_header,
        (int)strlen(contact_header), contact_header
        );    
    
    printf("REGISTER successful. Sending 200 OK.\n");

    //printf("\r\n===========================================================\r\n");
    printf("Tx SIP message 200 OK(response to REGISTER):\r\n%s\r\n", response_buffer);
    //printf("==============================================================\r\n");
    sip_message_t response;
    memset(&response, 0, sizeof(response));
    strncpy(response.buffer, response_buffer, BUFFER_SIZE-1);
    send_sip_message(&response, inet_ntoa(message->client_addr.sin_addr), ntohs(message->client_addr.sin_port));
  
    return 0;
}

/**
 * @brief State machine processing function.
 * @param call A pointer to the call struct (or NULL if not found).
 * @param message_type Request Method or Status Code (REQUEST_METHOD or STATUS_CODE).
 * @param method_or_code The parsed Request Method (INVITE, ACK, BYE, etc.) or Status Code string.
 * @param has_sdp A boolean value indicating if a remote media description exists.
 * @param message A pointer to the sip_message_t struct containing full message information, used for getting client transport address
 * @param raw_sip_message A pointer to the raw SIP message buffer for further parsing.
 * @param leg_type The leg type where the call_id was found. Indicates which leg (a or b) the SIP message was received from.
 */
void handle_state_machine(call_t *call, int message_type, const char *method_or_code, bool has_sdp, sip_message_t *message, const char *raw_sip_message, int leg_type) {
    char *from_start = strstr(message->buffer, "From: ");
    char *via_start = strstr(message->buffer, "Via: ");
    char *cseq_start = strstr(message->buffer, "CSeq: ");
    char *to_start = strstr(message->buffer, "To: ");
    char *call_id_start = strstr(message->buffer, "Call-ID: ");
    
    char from_header[HEADER_SIZE] = {0};
    char via_header[HEADER_SIZE] = {0};
    char cseq_header[HEADER_SIZE] = {0};
    char to_header[HEADER_SIZE] = {0};
    char call_id_header[HEADER_SIZE] = {0};

    printf("Extracted Headers: \r\n");

    if (via_start != NULL) {
        char *via_end = strstr(via_start, "\r\n");
        if (via_end != NULL) {
            size_t via_len = via_end - via_start;
            strncpy(via_header, via_start, via_len);
            via_header[via_len] = '\0';
            printf("[%s]\r\n", via_header);
        }
    }

    if (from_start != NULL) {
        char *from_end = strstr(from_start, "\r\n");
        if (from_end != NULL) {
            size_t from_len = from_end - from_start;
            strncpy(from_header, from_start, from_len);
            from_header[from_len] = '\0';
            printf("[%s]\r\n", from_header);
        }
    }

    if (to_start != NULL) {
        char *to_end = strstr(to_start, "\r\n");
        if (to_end != NULL) {
            size_t to_len = to_end - to_start;
            strncpy(to_header, to_start, to_len);
            to_header[to_len] = '\0';
            printf("[%s]\r\n", to_header);
        }
    }

    if (cseq_start != NULL) {
        char *cseq_end = strstr(cseq_start, "\r\n");
        if (cseq_end != NULL) {
            size_t cseq_len = cseq_end - cseq_start;
            strncpy(cseq_header, cseq_start, cseq_len);
            cseq_header[cseq_len] = '\0';
            printf("[%s]\r\n", cseq_header);
        }
    }
    
    if (call_id_start != NULL) {
        char *call_id_end = strstr(call_id_start, "\r\n");
        if (call_id_end != NULL) {
            size_t call_id_len = call_id_end - call_id_start;
            strncpy(call_id_header, call_id_start, call_id_len);
            call_id_header[call_id_len] = '\0';
            printf("[%s]\r\n", call_id_header);
        }
    }

    int max_forwards = 70; // Default Max-Forwards
    char *max_forwards_start = strstr(message->buffer, "Max-Forwards: ");
    if (max_forwards_start != NULL) {
        max_forwards_start += strlen("Max-Forwards: ");
        char *max_forwards_end = strstr(max_forwards_start, "\r\n");
        if (max_forwards_end != NULL) {
            char max_forwards_str[10] = {0};
            strncpy(max_forwards_str, max_forwards_start, max_forwards_end - max_forwards_start);
            max_forwards_str[max_forwards_end - max_forwards_start] = '\0';
            max_forwards = atoi(max_forwards_str);
            if(max_forwards > 0){
                max_forwards--;
            }
        }
    }

    if (call == NULL) {
        printf("call does not exist, Method/Status Code: [%s] \r\n", method_or_code);
        
        if(message_type == REQUEST_METHOD && strcmp(method_or_code, "INVITE") == 0){
            // The event "INVITE from A-leg" when the call state is CALL_STATE_IDLE
            // Action 1
            // allocate_new_call, if fail 500 response  for UE A.
            // set call_t's a_leg_uuid and b_leg_uuid
            // 100 Trying response  for UE A.
            // INVITE message to UE B
            // Set call state to CALL_STATE_ROUTING
            char temp_ip[INET_ADDRSTRLEN];
            int temp_port;
            inet_ntop(AF_INET, &(message->client_addr.sin_addr), temp_ip, INET_ADDRSTRLEN);
            temp_port = ntohs(message->client_addr.sin_port);

            char new_via_header[HEADER_SIZE] = {0};
            char *rport_ptr = strstr(via_header, ";rport");
            size_t via_len = strlen(via_header);

            if (rport_ptr != NULL) {
                size_t before_rport_len = rport_ptr - via_header;
                strncpy(new_via_header, via_header, before_rport_len);
                new_via_header[before_rport_len] = '\0';
                // Append the rport and received
                snprintf(new_via_header + before_rport_len, HEADER_SIZE - before_rport_len, ";rport=%d;received=%s%s", temp_port, temp_ip, rport_ptr + strlen(";rport"));
            } else {
                strncpy(new_via_header, via_header, via_len);
                new_via_header[via_len] = '\0';
                size_t current_len = strlen(new_via_header);
                // Append only the received
                snprintf(new_via_header + current_len, HEADER_SIZE - current_len, ";received=%s", temp_ip);
            }

            strcpy(via_header, new_via_header);
            printf("Updated Via Header: [%s]\r\n", via_header);
            
            // 1. Use allocate_new_call function to get a new and unused call_t struct pointer from call_map.
            call = allocate_new_call(&call_map);
            if(call == NULL){
                // If returns NULL, print error and send a 500 error to the caller. 
                // 500 error only fill the necessary fields, the needed information can extract from INVITE. 
                // The destination address is extracted from sip_message_t *message
                printf("Error: Failed to allocate new call.\n");
                char response_500[BUFFER_SIZE] = {0};
                if (from_header[0] != '\0' && via_header[0] != '\0' && cseq_header[0] != '\0' && to_header[0] != '\0' && call_id_header[0] != '\0') {
                     char *uri_start = strchr(from_header, '<');
                     if(uri_start != NULL){
                         uri_start += 1;
                         char *uri_end = strchr(uri_start, '>');
                        if(uri_end != NULL){
                            size_t uri_len = uri_end - uri_start;
                            char uri[uri_len+1];
                            strncpy(uri, uri_start, uri_len);
                            uri[uri_len] = '\0';
                        
                            snprintf(response_500, BUFFER_SIZE,
                                "SIP/2.0 500 Server Internal Error\r\n"
                                "%s\r\n"
                                "%s\r\n"
                                "%s\r\n"
                                "%s\r\n"
                                "%s\r\n"
                                "User-Agent: TinySIP\r\n"
                                "Content-Length: 0\r\n\r\n",
                                (char*)via_header, (char*)from_header, (char*)to_header, call_id_header, (char*)cseq_header); 

                            //printf("\r\n===========================================================\r\n");
                            printf("Tx SIP message 500 Server Internal Error:\r\n%s\r\n", response_500);
                            //printf("==============================================================\r\n");
                            sip_message_t response;
                            memset(&response, 0, sizeof(response));
                            strncpy(response.buffer, response_500, BUFFER_SIZE-1);
                            send_sip_message(&response, inet_ntoa(message->client_addr.sin_addr), ntohs(message->client_addr.sin_port));
                        }
                    }
                }
                
                return;
            }
            // 2. If return not NULL, set call_t to occupied,
            // Extract Call-ID from INVITE request, set call_t's a_leg_uuid and b_leg_uuid, and b_leg_uuid is same with a_leg_uuid, but first 5 chars changed to "b-leg" 
            if (call_id_start != NULL) {
                call_id_start += strlen("Call-ID:");
                while (*call_id_start == ' ') {
                    call_id_start++;
                }
                char *call_id_end = strstr(call_id_start, "\r\n");
                if (call_id_end != NULL) {
                    size_t call_id_len = call_id_end - call_id_start;
                    strncpy(call->a_leg_uuid, call_id_start, call_id_len);
                    call->a_leg_uuid[call_id_len] = '\0';
                    strncpy(call->b_leg_uuid, call_id_start, call_id_len);
                    call->b_leg_uuid[call_id_len] = '\0'; 
                    memcpy(call->b_leg_uuid, "b-leg", 5);
                }
            }
            // Set call_t's a_leg_addr using transport address from sip_message_t structure
            inet_ntop(AF_INET, &(message->client_addr.sin_addr), call->a_leg_ip_str, INET_ADDRSTRLEN);
            call->a_leg_port = ntohs(message->client_addr.sin_port);
            
            

            // From INVITE's To header, get called number (UE B's number), since the sip server can't know UE B's transport address in advance, so need a minimal location server internally.
            char callee_uri[MAX_USERNAME_LENGTH] = {0};
            if (to_start != NULL) {
                to_start += strlen("To: ");
                 char *uri_start = strchr(to_start, '<');
                if(uri_start != NULL){
                    uri_start += 1;
                    char *uri_end = strchr(uri_start, '>');
                     if(uri_end != NULL){
                        size_t uri_len = uri_end - uri_start;
                        char full_callee_uri[uri_len+1];
                        strncpy(full_callee_uri, uri_start, uri_len);
                        full_callee_uri[uri_len] = '\0';
                        
                        char *username_start = full_callee_uri;
                        if (strncmp(username_start, "sip:", 4) == 0 || strncmp(username_start, "tel:", 4) == 0) {
                           username_start += 4;
                        }

                        // Remove trailing spaces
                        char *username_end = username_start;
                        while (*username_end != '\0' && *username_end != ' ') {
                            username_end++;
                         }
                        size_t username_len = username_end - username_start;
                         
                        char *at_pos = strchr(username_start, '@');
                        if (at_pos != NULL && (at_pos - username_start) < username_len) {
                              username_len = at_pos - username_start;
                        }
                         
                        strncpy(callee_uri, username_start, username_len);
                        callee_uri[username_len] = '\0';

                        // find location
                        location_entry_t *location = find_location_entry_by_userid(callee_uri);
                        if (location != NULL) {
                            strcpy(call->b_leg_ip_str, location->ip_str);
                            call->b_leg_port = location->port;
                            printf("Found location: %s, %s:%d\r\n", location->username, location->ip_str, location->port);
                        } else {
                            printf("Error: Location not found for user: sip:%s.\r\n", callee_uri);
                            char response_404[BUFFER_SIZE] = {0};
                            if (from_header[0] != '\0' && via_header[0] != '\0' && cseq_header[0] != '\0' && to_header[0] != '\0' && call_id_header[0] != '\0') {
                                snprintf(response_404, BUFFER_SIZE,
                                    "SIP/2.0 404 Not Found\r\n"
                                    "%s\r\n"
                                    "%s\r\n"
                                    "%s\r\n"
                                    "%s\r\n"
                                    "%s\r\n"
                                    "User-Agent: TinySIP\r\n"
                                    "Content-Length: 0\r\n\r\n",
                                (char*)via_header, (char*)from_header, (char*)to_header, (char*)call_id_header, (char*)cseq_header);
                                //printf("\r\n===========================================================\r\n");
                                printf("Tx SIP message 404 Not Found:\r\n%s\r\n", response_404);
                                //printf("==============================================================\r\n");
                                sip_message_t response;
                                memset(&response, 0, sizeof(response));
                                strncpy(response.buffer, response_404, BUFFER_SIZE-1);
                                send_sip_message(&response, inet_ntoa(message->client_addr.sin_addr), ntohs(message->client_addr.sin_port));
                            }
                            init_call(call, call->index);
                            return;
                        }
                    }
                }
            }

            // Set call_t's a_leg_media.remote_media to true.
            call->a_leg_media.remote_media = true;
            // Set call_t's b_leg_media.local_media to true.
            call->b_leg_media.local_media = true;

            // Store the headers for later use in responses (A leg)
            strncpy(call->a_leg_header.from, from_header, HEADER_SIZE - 1);
            strncpy(call->a_leg_header.via, via_header, HEADER_SIZE - 1);
            strncpy(call->a_leg_header.cseq, cseq_header, HEADER_SIZE - 1);
            strncpy(call->a_leg_header.to, to_header, HEADER_SIZE - 1);
            
            // Extract Contact
            char *contact_start = strstr(message->buffer, "Contact: ");
             
            if (contact_start != NULL) {
                char *contact_end = strstr(contact_start, "\r\n");
                 if (contact_end != NULL) {
                      size_t contact_len = contact_end - contact_start;
                       if(contact_len < HEADER_SIZE)
                       {
                           strncpy(call->a_leg_contact, contact_start, contact_len);
                            call->a_leg_contact[contact_len] = '\0';

                            char *uri_start = strchr(call->a_leg_contact, '<');
                            if (uri_start != NULL) {
                                uri_start++; // Move past '<'
                                char *uri_end = strchr(uri_start, '>');
                                if (uri_end != NULL) {
                                  size_t uri_len = uri_end - uri_start;
                                    if (uri_len < HEADER_SIZE) {
                                        
                                        memmove(call->a_leg_contact, uri_start, uri_len);
                                        call->a_leg_contact[uri_len] = '\0';
                                        printf("Extracted Contact URI: [%s]\r\n", call->a_leg_contact);
                                    }
                                }
                            }
                        }
                   }
            }          

            // Finish 100 Trying response encoding for UE A, the necessary content can extract from INVITE.
            char trying_100[BUFFER_SIZE] = {0};
            if (from_header[0] != '\0' && via_header[0] != '\0' && cseq_header[0] != '\0' && to_header[0] != '\0' && call_id_header[0] != '\0') {
                snprintf(trying_100, BUFFER_SIZE,
                   "SIP/2.0 100 Trying\r\n"
                   "%s\r\n"
                   "%s\r\n"
                   "%s\r\n"
                   "%s\r\n"
                   "%s\r\n"
                   "User-Agent: TinySIP\r\n"
                   "Content-Length: 0\r\n\r\n",
                   (char*)via_header,(char*)from_header, (char*)to_header,(char*)call_id_header,(char*)cseq_header);

                //printf("\r\n===========================================================\r\n");
                printf("Tx SIP message 100 Trying to A-leg:\r\n%s\r\n", trying_100);
                //printf("==============================================================\r\n");

                sip_message_t response;
                memset(&response, 0, sizeof(response));
                strncpy(response.buffer, trying_100, BUFFER_SIZE-1);
                send_sip_message(&response, call->a_leg_ip_str, call->a_leg_port);
            }
            
            // Finish encoding INVITE message to UE B, extract needed content from original INVITE, send the new INVITE to UE B based on b-leg address in the minimal location server
            char invite_to_b[BUFFER_SIZE] = {0};
            char *content_type_start = strstr(message->buffer, "Content-Type: application/sdp");

            if (content_type_start != NULL) {
               
                int sdp_start_index = content_type_start - message->buffer;
                
                // Generate Via header for b-leg
                snprintf(call->b_leg_header.via, HEADER_SIZE, "Via: SIP/2.0/UDP %s:%d;branch=z9hG4bK%lx\r\n",
                    SIP_SERVER_IP_ADDRESS,
                    SIP_PORT,
                    (unsigned long)time(NULL)
                );                
                
                // Generate CSeq header for b-leg
                snprintf(call->b_leg_header.cseq, HEADER_SIZE, "CSeq: %d INVITE\r\n", cseq_number++);
                
                // Extract From and To information from a-leg headers.
                strncpy(call->b_leg_header.from, from_header, HEADER_SIZE - 1);
                // strncpy(call->b_leg_header.to, to_header, HEADER_SIZE - 1);
                snprintf(call->b_leg_header.to, BUFFER_SIZE - 1, "To: <sip:%s@%s:%d;ob>", callee_uri, call->b_leg_ip_str, call->b_leg_port);
                
                strncpy(call->callee, callee_uri, sizeof(call->callee) - 1);

                snprintf(invite_to_b, BUFFER_SIZE,
                    "INVITE sip:%s@%s:%d SIP/2.0\r\n"
                    "%s" // Via header
                    "%s\r\n" // From header
                    "%s\r\n" // To header
                    "Call-ID: %s\r\n"
                    "User-Agent: TinySIP\r\n"
                     "%s" // CSeq header
                    "Max-Forwards: %d\r\n"
                    "Contact: <sip:%s@%s:%d>\r\n"
                    "%s",
                    callee_uri,
                    call->b_leg_ip_str,
                    call->b_leg_port,
                    call->b_leg_header.via, 
                    call->b_leg_header.from, 
                    call->b_leg_header.to, 
                    call->b_leg_uuid,
                    call->b_leg_header.cseq,
                    max_forwards,
                    "TinySIP", SIP_SERVER_IP_ADDRESS, SIP_PORT,
                    message->buffer + sdp_start_index
                );
                //printf("\r\n===========================================================\r\n");
                printf("Tx SIP message INVITE to B-leg:\r\n%s\r\n", invite_to_b);
                //printf("==============================================================\r\n");                
            }
            sip_message_t inv_b;
            memset(&inv_b, 0, sizeof(inv_b));
            strncpy(inv_b.buffer, invite_to_b, BUFFER_SIZE-1);
            send_sip_message(&inv_b, call->b_leg_ip_str, call->b_leg_port);
            // Set call_t's call_state to CHANNEL_STATE_ROUTING.
            call->call_state = CALL_STATE_ROUTING;
            printf("  Call %d state transitioned to CALL_STATE_ROUTING.\r\n", call->index);
        }  else {
              printf("Unexpected message, the call may have already been released Method/Status Code: [%s], leg_type: [%d]\r\n", method_or_code, leg_type);
        }
    } else {
        printf("Existing call [%d], Method/Status Code: [%s], leg_type: [%d]\r\n", call->index, method_or_code, leg_type);
        // refresh the to headers for later use in responses (B leg or A leg)
        if (leg_type == B_LEG){
            strncpy(call->b_leg_header.to, to_header, HEADER_SIZE - 1);
        }
        switch (call->call_state) {
            case CALL_STATE_ROUTING:
            case CALL_STATE_RINGING:
                if (CALL_STATE_ROUTING == call->call_state){
                    printf("  Current call state: CALL_STATE_ROUTING\r\n");
                }
                if (CALL_STATE_RINGING == call->call_state){
                    printf("  Current call state: CALL_STATE_RINGING\r\n");
                }
                
                // event CANCEL from a-leg when the call state is CALL_STATE_ROUTING/CALL_STATE_RINGING
                // Action 6
                // Build a SIP/2.0 200 OK response of CANCEL to A leg
                // Build a SIP/2.0 487 Request Terminated response for A leg
                // Build a CANCEL request for B leg
                // Set call state to DISCONNECTING
                
                if (message_type == REQUEST_METHOD && strcmp(method_or_code, "CANCEL") == 0 && leg_type == A_LEG) {
                    printf("  Processing CANCEL from A leg\r\n");

                    // 1. Build a SIP/2.0 200 OK response of CANCEL for A leg, using headers from the message data.
                    char ok_200_cancel[BUFFER_SIZE] = {0};
                    if (from_header[0] != '\0' && via_header[0] != '\0' && cseq_header[0] != '\0' && to_header[0] != '\0' && call_id_header[0] != '\0') {
                        snprintf(ok_200_cancel, BUFFER_SIZE,
                            "SIP/2.0 200 OK\r\n"
                            "%s\r\n"
                            "%s\r\n"
                            "%s\r\n"
                            "%s\r\n"
                            "%s\r\n"
                            "User-Agent: TinySIP\r\n"
                            "Content-Length: 0\r\n\r\n",
                            (char*)via_header, (char*)from_header, (char*)to_header, call_id_header, (char*)cseq_header);

                        //printf("\r\n===========================================================\r\n");
                        printf("Tx SIP message 200 OK(response to CANCEL):\r\n%s\r\n", ok_200_cancel);
                        //printf("==============================================================\r\n");

                        sip_message_t response;
                        memset(&response, 0, sizeof(response));
                        strncpy(response.buffer, ok_200_cancel, BUFFER_SIZE - 1);
                        send_sip_message(&response, inet_ntoa(message->client_addr.sin_addr), ntohs(message->client_addr.sin_port));
                    }

                    // 2. Build a SIP/2.0 487 Request Terminated response for A leg, using headers from A leg's call data, and send it to A leg.
                    char terminated_487[BUFFER_SIZE] = {0};
                    if (call->a_leg_header.from[0] != '\0' && call->a_leg_header.via[0] != '\0' && call->a_leg_header.cseq[0] != '\0' && call->a_leg_header.to[0] != '\0') {
                        snprintf(terminated_487, BUFFER_SIZE,
                            "SIP/2.0 487 Request Terminated\r\n"
                            "%s\r\n"
                            "%s\r\n"
                            "%s\r\n"
                            "Call-ID: %s\r\n"
                            "%s\r\n"
                            "User-Agent: TinySIP\r\n"
                            "Content-Length: 0\r\n\r\n",
                            call->a_leg_header.via, call->a_leg_header.from, call->a_leg_header.to, call->a_leg_uuid, call->a_leg_header.cseq
                        );

                        //printf("\r\n===========================================================\r\n");
                        printf("Tx SIP message 487 Request Terminated to A-leg:\r\n%s\r\n", terminated_487);
                        //printf("==============================================================\r\n");

                        sip_message_t response;
                        memset(&response, 0, sizeof(response));
                        strncpy(response.buffer, terminated_487, BUFFER_SIZE - 1);
                        send_sip_message(&response, call->a_leg_ip_str, call->a_leg_port);
                    }

                    // 3. Build a CANCEL request for B leg, Send it to B leg. CSeq is set to CANCEL, and the number is generated using cseq_number++.
                    char cancel_b[BUFFER_SIZE] = {0};
                    //char *first_line_end = strstr(message->buffer, "\r\n");
                     //if(first_line_end != NULL) {
                    //    size_t first_line_len = first_line_end - message->buffer;
                    //   char first_line[first_line_len+1];
                    //    strncpy(first_line, message->buffer, first_line_len);
                    //    first_line[first_line_len] = '\0';

                        int cseq_value = extract_cseq_number(call->b_leg_header.cseq);
                        snprintf(cancel_b, BUFFER_SIZE,
                                "CANCEL sip:%s@%s:%d SIP/2.0\r\n"
                                "%s"  //..
                                "%s\r\n"
                                "%s\r\n"
                                "Call-ID: %s\r\n"
                                "User-Agent: TinySIP\r\n"
                                "CSeq: %d CANCEL\r\n"
                                "Max-Forwards: %d\r\n"
                                "Content-Length: 0\r\n\r\n",
                                call->callee,
                                call->b_leg_ip_str,
                                call->b_leg_port,
                                call->b_leg_header.via,
                                call->b_leg_header.from,
                                call->b_leg_header.to,
                                call->b_leg_uuid,
                                cseq_value,
                                max_forwards
                            );
                    // }

                    //printf("\r\n===========================================================\r\n");
                    printf("Tx SIP message CANCEL to B-leg:\r\n%s\r\n", cancel_b);
                    //printf("==============================================================\r\n");

                    sip_message_t response_cancel_b;
                    memset(&response_cancel_b, 0, sizeof(response_cancel_b));
                    strncpy(response_cancel_b.buffer, cancel_b, BUFFER_SIZE - 1);
                    send_sip_message(&response_cancel_b, call->b_leg_ip_str, call->b_leg_port);
                    
                    // Set call state to DISCONNECTING
                    call->call_state = CALL_DISCONNECTING;
                    printf("  Call %d state transitioned to CALL_DISCONNECTING.\r\n", call->index);
                    break;
                }else if (message_type == STATUS_CODE && strcmp(method_or_code, "183") == 0 && leg_type == B_LEG) {
                    printf("  Response 183 from B leg\r\n");

                    // 1. Build the 183 response to A leg, using headers from A leg's call data, and send it to A leg.
                    // The destination address is also taken from A leg's data. If there is SDP in the message, extract it; otherwise, use Content-Length: 0.
                    char early_183[BUFFER_SIZE] = {0};

                    if (call->a_leg_header.from[0] != '\0' && call->a_leg_header.via[0] != '\0' && call->a_leg_header.cseq[0] != '\0' && call->a_leg_header.to[0] != '\0') {
                        char *content_type_start = strstr(message->buffer, "Content-Type: application/sdp");
                        
                        if (content_type_start != NULL) {
                            // If SDP is found, extract it from the message buffer
                            int sdp_start_index = content_type_start - message->buffer;
                            snprintf(early_183, BUFFER_SIZE,
                                     "SIP/2.0 183 Session Progress\r\n"
                                     "%s\r\n"
                                     "%s\r\n"
                                     "%s\r\n"
                                     "Call-ID: %s\r\n"
                                     "%s\r\n"
                                     "User-Agent: TinySIP\r\n"
                                     "Contact: <sip:TinySIP@%s:%d>\r\n"
                                     "%s",
                                     call->a_leg_header.via, call->a_leg_header.from, call->a_leg_header.to, call->a_leg_uuid, call->a_leg_header.cseq,
                                     SIP_SERVER_IP_ADDRESS, SIP_PORT,
                                     message->buffer + sdp_start_index
                                     );

                            //printf("\r\n===========================================================\r\n");
                            printf("Tx SIP message 183 Session Progress to A-leg:\r\n%s\r\n", early_183);
                            //printf("==============================================================\r\n");

                            sip_message_t response;
                            memset(&response, 0, sizeof(response));
                            strncpy(response.buffer, early_183, BUFFER_SIZE - 1);
                            send_sip_message(&response, call->a_leg_ip_str, call->a_leg_port);
                        } else {
                            // If no SDP, build the 183 response without SDP
                            snprintf(early_183, BUFFER_SIZE,
                                     "SIP/2.0 183 Session Progress\r\n"
                                     "%s\r\n"
                                     "%s\r\n"
                                     "%s\r\n"
                                     "Call-ID: %s\r\n"
                                     "%s\r\n"
                                     "User-Agent: TinySIP\r\n"
                                     "Contact: <sip:TinySIP@%s:%d>\r\n"
                                     "Content-Length: 0\r\n\r\n",
                                     call->a_leg_header.via, call->a_leg_header.from, call->a_leg_header.to, call->a_leg_uuid, call->a_leg_header.cseq,
                                     SIP_SERVER_IP_ADDRESS, SIP_PORT
                                     );

                            //printf("\r\n===========================================================\r\n");
                            printf("Tx SIP message 183 Session Progress to A-leg:\r\n%s\r\n", early_183);
                            //printf("==============================================================\r\n");

                            sip_message_t response;
                            memset(&response, 0, sizeof(response));
                            strncpy(response.buffer, early_183, BUFFER_SIZE - 1);
                            send_sip_message(&response, call->a_leg_ip_str, call->a_leg_port);
                        }
                    }

                    // If SDP exists, set media information
                    if (has_sdp) {
                        call->a_leg_media.local_media = true;
                        call->b_leg_media.remote_media = true;
                    }
                    break;
                }else if (message_type == STATUS_CODE && strcmp(method_or_code, "180") == 0 && leg_type == B_LEG) {
                    printf("  Processing 180 Ringing from B leg\r\n");
                    
                    // event 180 from b-leg when the call state is CALL_STATE_ROUTING
                    // Action 2
                    // Build the 180 response to A leg
                    // Set the call state to CALL_STATE_RINGING.
                    
                    char ringing_180[BUFFER_SIZE] = {0};

                    if (call->a_leg_header.from[0] != '\0' && call->a_leg_header.via[0] != '\0' && call->a_leg_header.cseq[0] != '\0' && call->a_leg_header.to[0] != '\0') {
                        char *content_type_start = strstr(message->buffer, "Content-Type: application/sdp");

                        if (content_type_start != NULL) {
                            // If SDP is found, extract it from the message buffer
                            int sdp_start_index = content_type_start - message->buffer;
                            snprintf(ringing_180, BUFFER_SIZE,
                                     "SIP/2.0 180 Ringing\r\n"
                                     "%s\r\n"
                                     "%s\r\n"
                                     "%s\r\n"
                                     "Call-ID: %s\r\n"
                                     "%s\r\n"
                                     "User-Agent: TinySIP\r\n"
                                     "Contact: <sip:TinySIP@%s:%d>\r\n"
                                     "%s",
                                     call->a_leg_header.via, call->a_leg_header.from, call->a_leg_header.to, 
                                     call->a_leg_uuid, call->a_leg_header.cseq, SIP_SERVER_IP_ADDRESS, SIP_PORT, 
                                     message->buffer + sdp_start_index);

                            //printf("\r\n===========================================================\r\n");
                            printf("Tx SIP message 180 Ringing to A-leg:\r\n%s\r\n", ringing_180);
                            //printf("==============================================================\r\n");

                            sip_message_t response;
                            memset(&response, 0, sizeof(response));
                            strncpy(response.buffer, ringing_180, BUFFER_SIZE - 1);
                            send_sip_message(&response, call->a_leg_ip_str, call->a_leg_port);
                        } else {
                            // If no SDP, build the 180 response without SDP
                            snprintf(ringing_180, BUFFER_SIZE,
                                     "SIP/2.0 180 Ringing\r\n"
                                     "%s\r\n"
                                     "%s\r\n"
                                     "%s\r\n"
                                     "Call-ID: %s\r\n"
                                     "%s\r\n"
                                     "User-Agent: TinySIP\r\n"
                                     "Contact: <sip:TinySIP@%s:%d>\r\n"
                                     "Content-Length: 0\r\n\r\n",
                                     call->a_leg_header.via, call->a_leg_header.from, call->a_leg_header.to, 
                                     call->a_leg_uuid, call->a_leg_header.cseq, SIP_SERVER_IP_ADDRESS, SIP_PORT);

                            //printf("\r\n===========================================================\r\n");
                            printf("Tx SIP message 180 Ringing to A-leg:\r\n%s\r\n", ringing_180);
                            //printf("==============================================================\r\n");

                            sip_message_t response;
                            memset(&response, 0, sizeof(response));
                            strncpy(response.buffer, ringing_180, BUFFER_SIZE - 1);
                            send_sip_message(&response, call->a_leg_ip_str, call->a_leg_port);
                        }
                    }

                    // 2. if there is SDP, set the media information.
                    if (has_sdp) {
                        call->a_leg_media.local_media = true;
                        call->b_leg_media.remote_media = true;
                    }

                    // 3. Set the call state to CALL_STATE_RINGING.
                    call->call_state = CALL_STATE_RINGING;
                    printf("  Call %d state transitioned to CALL_STATE_RINGING.\r\n", call->index);
                    break;
                }else if (message_type == STATUS_CODE && strcmp(method_or_code, "200") == 0 && leg_type == B_LEG) {
                    printf("  Processing 200 OK from B leg\r\n");
                    
                    // event 200(2xx) from b-leg when the call state is CALL_STATE_ROUTING/CALL_STATE_RINGING
                    // Action 3
                    // Construct a 200 OK response for A leg
                    // Set the state to CALL_STATE_ANSWERED.
                    
                    char ok_200[BUFFER_SIZE] = {0};

                    // Extract Contact for B leg
                    char *contact_start = strstr(message->buffer, "Contact: ");

                    if (contact_start != NULL) {
                        char *contact_end = strstr(contact_start, "\r\n");
                        if (contact_end != NULL) {
                            size_t contact_len = contact_end - contact_start;
                            if (contact_len < HEADER_SIZE) {
                                strncpy(call->b_leg_contact, contact_start, contact_len);
                                call->b_leg_contact[contact_len] = '\0';

                                char *uri_start = strchr(call->b_leg_contact, '<');
                                if (uri_start != NULL) {
                                    uri_start++; // Move past '<'
                                    char *uri_end = strchr(uri_start, '>');
                                    if (uri_end != NULL) {
                                        size_t uri_len = uri_end - uri_start;
                                        if (uri_len < HEADER_SIZE) {

                                            memmove(call->b_leg_contact, uri_start, uri_len);
                                            call->b_leg_contact[uri_len] = '\0';
                                            printf("Extracted Contact URI for B leg: [%s]\r\n", call->b_leg_contact);
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if (call->a_leg_header.from[0] != '\0' && call->a_leg_header.via[0] != '\0' && call->a_leg_header.cseq[0] != '\0' && call->a_leg_header.to[0] != '\0') {
                        char *content_type_start = strstr(message->buffer, "Content-Type: application/sdp");

                        if (content_type_start != NULL) {
                            int sdp_start_index = content_type_start - message->buffer;
                            snprintf(ok_200, BUFFER_SIZE,
                                "SIP/2.0 200 OK\r\n"
                                "%s\r\n"
                                "%s\r\n"
                                "%s\r\n"
                                "Call-ID: %s\r\n"
                                "%s\r\n"
                                "User-Agent: TinySIP\r\n"
                                "Contact: <sip:TinySIP@%s:%d>\r\n"
                                "%s",
                                call->a_leg_header.via, call->a_leg_header.from, call->a_leg_header.to, call->a_leg_uuid, call->a_leg_header.cseq,
                                SIP_SERVER_IP_ADDRESS, SIP_PORT,
                                message->buffer + sdp_start_index
                            );

                            //printf("\r\n===========================================================\r\n");
                            printf("Tx SIP message 200 OK(response to INVITE) to A leg:\r\n%s\r\n", ok_200);
                            //printf("==============================================================\r\n");

                            sip_message_t response;
                            memset(&response, 0, sizeof(response));
                            strncpy(response.buffer, ok_200, BUFFER_SIZE - 1);
                            send_sip_message(&response, call->a_leg_ip_str, call->a_leg_port);

                        } else {
                            snprintf(ok_200, BUFFER_SIZE,
                                "SIP/2.0 200 OK\r\n"
                                "%s\r\n"
                                "%s\r\n"
                                "%s\r\n"
                                "Call-ID: %s\r\n"
                                "%s\r\n"
                                "User-Agent: TinySIP\r\n"
                                "Contact: <sip:TinySIP@%s:%d>\r\n"
                                "Content-Length: 0\r\n\r\n",
                                call->a_leg_header.via, call->a_leg_header.from, call->a_leg_header.to, call->a_leg_uuid, call->a_leg_header.cseq,
                                SIP_SERVER_IP_ADDRESS, SIP_PORT
                            );

                            //printf("\r\n===========================================================\r\n");
                            printf("Tx SIP message 200 OK(response to INVITE) to A leg:\r\n%s\r\n", ok_200);
                            //printf("==============================================================\r\n");

                            sip_message_t response;
                            memset(&response, 0, sizeof(response));
                            strncpy(response.buffer, ok_200, BUFFER_SIZE - 1);
                            send_sip_message(&response, call->a_leg_ip_str, call->a_leg_port);
                        }
                    }

                    // 2. if there is SDP, set the media information.
                    if (has_sdp) {
                        call->a_leg_media.local_media = true;
                        call->b_leg_media.remote_media = true;
                    }

                    // 3. Set the state to CALL_STATE_ANSWERED.
                    call->call_state = CALL_STATE_ANSWERED;
                    printf("  Call %d state transitioned to CALL_STATE_ANSWERED.\r\n", call->index);
                    break;
                }else if (message_type == STATUS_CODE && leg_type == B_LEG) {
                    int temp_response_code = atoi(method_or_code);

                    if (temp_response_code >= 100 && temp_response_code < 200) {
                        printf("  Response [%s] from B leg, do nothing\r\n", method_or_code);
                        break;
                    }else if (temp_response_code >= 400 && temp_response_code < 700) {
                        printf("  Response [%s] from B leg, send ACK and response\r\n", method_or_code);
                        // event 400~699 from b-leg when the call state is CALL_STATE_ROUTING/CALL_STATE_RINGING
                        // Action 7
                        // Construct an ACK for B leg,
                        // Construct the same error STATUS_CODE for A leg
                        // Set the state to CALL_STATE_IDLE
                        
                        char ack_b[BUFFER_SIZE] = {0};
                        if (call->b_leg_header.from[0] != '\0' && call->b_leg_header.via[0] != '\0' && call->b_leg_header.cseq[0] != '\0' && call->b_leg_header.to[0] != '\0') {
                            int cseq_value = extract_cseq_number(cseq_header);
                            snprintf(ack_b, BUFFER_SIZE,
                                "ACK sip:%s@%s:%d SIP/2.0\r\n"
                                "Via: SIP/2.0/UDP %s:%d;branch=z9hG4bK%lx\r\n"
                                "%s\r\n"
                                "%s\r\n"   
                                "Call-ID: %s\r\n"
                                "CSeq: %d ACK\r\n"
                                 "User-Agent: TinySIP\r\n"
                                "Max-Forwards: 70\r\n"
                                "Content-Length: 0\r\n\r\n",
                                call->callee, call->b_leg_ip_str, call->b_leg_port,
                                SIP_SERVER_IP_ADDRESS, SIP_PORT,
                                (unsigned long)time(NULL),
                                call->b_leg_header.from, call->b_leg_header.to, call->b_leg_uuid, cseq_value
                            );

                            //printf("\r\n===========================================================\r\n");
                            printf("Tx SIP message ACK to B-leg:\r\n%s\r\n", ack_b);
                            //printf("==============================================================\r\n");

                            sip_message_t response_ack;
                            memset(&response_ack, 0, sizeof(response_ack));
                            strncpy(response_ack.buffer, ack_b, BUFFER_SIZE - 1);
                            send_sip_message(&response_ack, call->b_leg_ip_str, call->b_leg_port);
                        }

                        // 2. Construct the same response for A leg, using header fields from A leg's call data and sending it to A leg.
                        // The target address is taken from A leg's data. Content-Length is set to 0.
                        char err_response[BUFFER_SIZE] = {0};
                        if (call->a_leg_header.from[0] != '\0' && call->a_leg_header.via[0] != '\0' && call->a_leg_header.cseq[0] != '\0' && call->a_leg_header.to[0] != '\0') {
                            snprintf(err_response, BUFFER_SIZE,
                                "SIP/2.0 %s\r\n"
                                "%s\r\n"
                                "%s\r\n"
                                "%s\r\n"
                                "Call-ID: %s\r\n"
                                "%s\r\n"
                                "User-Agent: TinySIP\r\n"
                                "Content-Length: 0\r\n\r\n",
                                method_or_code,
                                call->a_leg_header.via,
                                call->a_leg_header.from,
                                call->a_leg_header.to,
                                call->a_leg_uuid,
                                call->a_leg_header.cseq
                            );

                            //printf("\r\n===========================================================\r\n");
                            printf("Tx SIP message forword  err_response %s to A-leg:\r\n%s\r\n", method_or_code, err_response);
                            //printf("==============================================================\r\n");

                            sip_message_t response;
                            memset(&response, 0, sizeof(response));
                            strncpy(response.buffer, err_response, BUFFER_SIZE - 1);
                            send_sip_message(&response, call->a_leg_ip_str, call->a_leg_port);
                        }

                        // 3. Set the state to CALL_STATE_IDLE and reinitialize the call.
                        init_call(call, call->index);
                        call->call_state = CALL_STATE_IDLE;
                        printf("  Call %d state transitioned to CALL_STATE_IDLE.\r\n", call->index);
                        break;
                    }
                }
                break;


            case CALL_STATE_ANSWERED:
                printf("  Current call state: CALL_STATE_ANSWERED\r\n");

                // event ACK from a-leg when the call state is CALL_STATE_ANSWERED
                // Action 4: 
                // Send ACK to B-leg
                // Set state to CALL_STATE_CONNECTED.
                if (message_type == REQUEST_METHOD && strcmp(method_or_code, "ACK") == 0 && leg_type == A_LEG) {
                    printf("  Processing ACK from A leg\r\n");

                    // 1. Construct an ACK for B leg, using header fields from B leg's call data and sending it to B leg.
                    // The target address is taken from B leg's data. Content-Length is set to 0
                    char ack_b[BUFFER_SIZE] = {0};
                    if (call->b_leg_header.from[0] != '\0' && call->b_leg_header.via[0] != '\0' && call->b_leg_header.cseq[0] != '\0' && call->b_leg_header.to[0] != '\0') {
                        int cseq_value = extract_cseq_number(call->b_leg_header.cseq);
                        snprintf(ack_b, BUFFER_SIZE,
                            "ACK sip:%s@%s:%d SIP/2.0\r\n"
                            "Via: SIP/2.0/UDP %s:%d;branch=z9hG4bK%lx\r\n"
                            "%s\r\n"
                            "%s\r\n"   
                            "Call-ID: %s\r\n"
                            "CSeq: %d ACK\r\n"
                            "User-Agent: TinySIP\r\n"
                            "Max-Forwards: %d\r\n"
                            "Content-Length: 0\r\n\r\n",
                            call->callee, call->b_leg_ip_str, call->b_leg_port,
                            SIP_SERVER_IP_ADDRESS, SIP_PORT,
                            (unsigned long)time(NULL),
                            call->b_leg_header.from, call->b_leg_header.to, call->b_leg_uuid, cseq_value,
                            max_forwards
                        );


                        //printf("\r\n===========================================================\r\n");
                        printf("Tx SIP message ACK to B-leg:\r\n%s\r\n", ack_b);
                        //printf("==============================================================\r\n");

                        sip_message_t response_ack;
                        memset(&response_ack, 0, sizeof(response_ack));
                        strncpy(response_ack.buffer, ack_b, BUFFER_SIZE - 1);
                        send_sip_message(&response_ack, call->b_leg_ip_str, call->b_leg_port);
                    }

                    call->call_state = CALL_STATE_CONNECTED;
                    printf("  Call %d state transitioned to CALL_STATE_CONNECTED.\r\n", call->index);
                    break;
                }

                // event CANCEL from a-leg:
                // This occurs when the calling party initiates a cancellation *while* the 200 OK for INVITE is *in transmission* and hasn't reached the calling party yet.
                else if (message_type == REQUEST_METHOD && strcmp(method_or_code, "CANCEL") == 0 && leg_type == A_LEG) {
                    printf("  !!! WARNING !!! received CANCEL from A leg in CALL_STATE_ANSWERED (TODO: release both legs)\r\n");
                    // TODO: Release both legs properly.
                    break;
                }
                // event BYE from b-leg:
                // This is a special case where B leg hangs up right after being connected and before A leg's ACK is received by SIP Server.
                // This could be caused by network issues. A strict approach is to release both legs.
                // This part is TODO.
                else if (message_type == REQUEST_METHOD && strcmp(method_or_code, "BYE") == 0 && leg_type == B_LEG) {
                    printf("  !!! WARNING !!! received BYE from B leg in CALL_STATE_ANSWERED (TODO: release both legs)\r\n");
                    // TODO: Release both legs properly.
                    break;
                } else {
                    printf("  !!! WARNING !!! Unexpected message type [%d] and status code/method [%s] in CALL_STATE_ANSWERED\r\n", message_type, method_or_code);
                }
                break;

            case CALL_STATE_CONNECTED:
                printf("  Current call state: CALL_STATE_CONNECTED\r\n");
                if (message_type == REQUEST_METHOD && strcmp(method_or_code, "BYE") == 0 ) {
                // event BYE from a-leg or b-leg when the call state is  CALL_STATE_CONNECTED
                // Action 5
                // Send 200 OK to the sender of BYE
                // Construct and send BYE to the other leg
                // Set state to CALL_DISCONNECTING
                    printf("  Processing BYE from %s leg\r\n", leg_type == A_LEG ? "A":"B");
                    char ok_200_bye[BUFFER_SIZE] = {0};
                    snprintf(ok_200_bye, BUFFER_SIZE,
                         "SIP/2.0 200 OK\r\n"
                         "%s\r\n"
                         "%s\r\n"
                         "%s\r\n"
                         "%s\r\n"
                         "%s\r\n"
                         "Content-Length: 0\r\n\r\n",
                         via_header,
                         from_header,
                         to_header,
                         call_id_header,
                         cseq_header
                    );

                    //printf("\r\n===========================================================\r\n");
                    printf("Tx SIP message 200 OK(response to BYE) to Sender leg:\r\n%s\r\n", ok_200_bye);
                    //printf("==============================================================\r\n");
                    sip_message_t response_200ok;
                    memset(&response_200ok, 0, sizeof(response_200ok));
                    strncpy(response_200ok.buffer, ok_200_bye, BUFFER_SIZE - 1);
                    send_sip_message(&response_200ok, leg_type == A_LEG ? call->a_leg_ip_str: call->b_leg_ip_str, leg_type == A_LEG ? call->a_leg_port: call->b_leg_port);
                    
                    char bye_other_leg[BUFFER_SIZE] = {0};
                    if (leg_type == A_LEG) {
                        // Generate Via header for b-leg
                        snprintf(call->b_leg_header.via, HEADER_SIZE, "Via: SIP/2.0/UDP %s:%d;branch=z9hG4bK%lx\r\n",
                            SIP_SERVER_IP_ADDRESS,
                            SIP_PORT,
                            (unsigned long)time(NULL)
                        );

                        snprintf(bye_other_leg, BUFFER_SIZE,
                            "BYE sip:%s@%s:%d SIP/2.0\r\n"
                            "%s"
                            "%s\r\n"
                            "%s\r\n"
                            "Call-ID: %s\r\n"
                            "CSeq: %d BYE\r\n"
                            "User-Agent: TinySIP\r\n"
                            "Content-Length: 0\r\n\r\n",
                            call->callee, call->b_leg_ip_str, call->b_leg_port,
                            call->b_leg_header.via,
                            call->b_leg_header.from,
                            call->b_leg_header.to,
                            call->b_leg_uuid,
                            cseq_number++
                        );
                        

                        //printf("\r\n===========================================================\r\n");
                        printf("Tx SIP message BYE to B leg:\r\n%s\r\n", bye_other_leg);
                        //printf("==============================================================\r\n");
                        sip_message_t response_bye_other_leg;
                        memset(&response_bye_other_leg, 0, sizeof(response_bye_other_leg));
                        strncpy(response_bye_other_leg.buffer, bye_other_leg, BUFFER_SIZE - 1);
                        send_sip_message(&response_bye_other_leg, call->b_leg_ip_str, call->b_leg_port);
                    } else {
                        // Generate Via header for a-leg
                        snprintf(call->a_leg_header.via, HEADER_SIZE, "Via: SIP/2.0/UDP %s:%d;branch=z9hG4bK%lx\r\n",
                            SIP_SERVER_IP_ADDRESS,
                            SIP_PORT,
                            (unsigned long)time(NULL)
                        );
                        
                        char new_from_header[HEADER_SIZE+10] = {0};
                        char new_to_header[HEADER_SIZE+10] = {0};
                        
                        char temp_from_data[HEADER_SIZE] = {0};
                        char temp_to_data[HEADER_SIZE] = {0};

                        char *from_start = strstr(call->a_leg_header.from, "From: ");
                        if (from_start != NULL) {
                            from_start += strlen("From: "); 
                            size_t from_len = strlen(from_start);
                             if (from_len < HEADER_SIZE -1) {
                                strncpy(temp_from_data, from_start, HEADER_SIZE - 1);
                                temp_from_data[HEADER_SIZE - 1] = '\0';
                            }
                        
                        }

                         char *to_start = strstr(call->a_leg_header.to, "To: ");
                        if (to_start != NULL) {
                            to_start += strlen("To: "); 
                            size_t to_len = strlen(to_start);
                            if (to_len < HEADER_SIZE -1) {
                            strncpy(temp_to_data, to_start, HEADER_SIZE - 1);
                            temp_to_data[HEADER_SIZE - 1] = '\0';
                            }
                          
                        }
                        

                        snprintf(new_from_header, HEADER_SIZE+10, "From: %s", temp_to_data);
                        snprintf(new_to_header, HEADER_SIZE+10, "To: %s", temp_from_data);
                        
                        snprintf(bye_other_leg, BUFFER_SIZE,
                            "BYE %s SIP/2.0\r\n"
                            "%s"
                            "%s\r\n"
                            "%s\r\n"
                            "Call-ID: %s\r\n"
                            "CSeq: %d BYE\r\n"
                            "User-Agent: TinySIP\r\n"
                            "Content-Length: 0\r\n\r\n",
                            call->a_leg_contact,
                            call->a_leg_header.via,
                            new_from_header,
                            new_to_header,
                            call->a_leg_uuid,
                            cseq_number++
                        );

                        //printf("\r\n===========================================================\r\n");
                        printf("Tx SIP message BYE to A leg:\r\n%s\r\n", bye_other_leg);
                        //printf("==============================================================\r\n");
                        sip_message_t response_bye_other_leg;
                        memset(&response_bye_other_leg, 0, sizeof(response_bye_other_leg));
                        strncpy(response_bye_other_leg.buffer, bye_other_leg, BUFFER_SIZE - 1);
                        send_sip_message(&response_bye_other_leg, call->a_leg_ip_str, call->a_leg_port);
                    }

                    call->call_state = CALL_DISCONNECTING;
                    printf("  Call %d state transitioned to CALL_DISCONNECTING.\r\n", call->index);
                } else {
                    printf("  !!! WARNING !!! Unexpected message type [%d] and status code/method [%s] in CALL_STATE_CONNECTED\r\n", message_type, method_or_code);
                }
                break;

            case CALL_DISCONNECTING:
                printf("  Current call state: CALL_DISCONNECTING\r\n");
                if (message_type == STATUS_CODE && strcmp(method_or_code, "200") == 0) {
                    // event 200 OK of BYE from a-leg or b-leg when the call state is  CALL_DISCONNECTING
                    // Action 8
                    // Set state to CALL_STATE_IDLE
                    if (strstr(cseq_header, "BYE") != NULL || strstr(cseq_header, "CANCEL") != NULL) {
                        printf("Received 200 OK (response to BYE/CANCEL) for call [%d]. Releasing call data.\r\n", call->index);
                        init_call(call, call->index);
                        call->call_state = CALL_STATE_IDLE;
                        printf("  Call %d state transitioned to CALL_STATE_IDLE.\r\n", call->index);
                    } else {
                        printf("  !!! WARNING !!! received 200 OK without BYE/CANCEL in CALL_DISCONNECTING\r\n");
                    }
                } else {
                    printf("  !!! WARNING !!! Unexpected message type [%d] and status code/method [%s] in CALL_DISCONNECTING\r\n", message_type, method_or_code);
                }

                break;

            default:
                printf("  Current call state: Unknown state\r\n");
        }        
        
    }
}

/**
 * @brief Worker thread function to process SIP messages.
 * @param arg Pointer to the worker thread's message queue.
 * @return NULL
 */
void* process_sip_messages(void* arg) {
    message_queue_t *queue = (message_queue_t *)arg;
    sip_message_t *message;
    char method[20] = {0};
    char call_id[MAX_UUID_LENGTH] = {0};
    const char *ptr;
    char *content_type_start;
    char content_type[50] = {0};
    int response_code = 0;
    char cseq_header[HEADER_SIZE] = {0};
    bool has_sdp = false;
    call_t *call = NULL;
    int leg_type = 0;
    char source_ip_str[INET_ADDRSTRLEN] = {0};

    while (1) {
        if (dequeue_message(queue, &message)) {
            // Process the SIP message here
            inet_ntop(AF_INET, &(message->client_addr.sin_addr), source_ip_str, INET_ADDRSTRLEN);

            char *first_line_end = strstr(message->buffer, "\r\n");
            if (first_line_end == NULL) {
                free(message);
                continue; // invalid sip message
            }
            size_t first_line_len = first_line_end - message->buffer;
            char first_line[first_line_len + 1];
            strncpy(first_line, message->buffer, first_line_len);
            first_line[first_line_len] = '\0';
            
            if (0 == first_line_len)
            {
                free(message);
                continue; // invalid sip message                
            }
             
            printf("\r\n===========================================================\r\n");
            printf("Rx SIP message from Source: %s:%d\r\n", source_ip_str, ntohs(message->client_addr.sin_port));
            printf("received SIP message:\r\n%s\r\n", message->buffer);
            //printf("==============================================================\r\n");

            // Simulate processing and response.
            // This study case simulates minimal SIP decoding by extracting and logging the Request Method/Status Code, Call-ID, and Content-Type (if application/sdp).
            // Only Status Codes related to INVITE, CANCEL, or BYE requests are reported to the state machine. Other Status Codes would be handled by a full SIP stack.
            // A more complete SIP stack will manage decoding exceptions, SIP transaction processing, as well as redundant string matching and performance optimizations during SIP decoding.
            // This SIP server operates in media bypass mode, so no media processing is performed here; media information is only recorded and forwarded to the other leg.
            // This SIP server operates in B2BUA mode, not as a proxy. The registration server functionality is not implemented.

            // The SIP_SERVER_IP_ADDRESS macro must be set to the interface address used by the SIP server
            // so that the SIP server can correctly populate its Via: and Contact: headers. **Note:** This needs to be done before compiling!

            // The location_entries should be filled with each softphone/UE's SIP port, address, and phone number
            // so that they can be correctly reached as the called party by the SIP server. **Note:** This needs to be done before compiling!
            // The SIP port and address are no longer required, but the phone number must still be set. 
            // Parse SIP message inline

            // The location_entries should be filled with each softphone/UE's phone number
            // so that they can be correctly reached by the SIP server. 
            // The SIP address and port are no longer required due to the simplified registration and location server functionality. 
            // However, the phone number must still be set to identify the user.
            // **Note:** This needs to be done before compiling!
            
            ptr = message->buffer;
            response_code = 0;
            memset(cseq_header, 0, sizeof(cseq_header));    
            has_sdp = false;

            // 0. REGISTER
            if (strncmp(first_line, "REGISTER ", strlen("REGISTER ")) == 0) {

                // It's a REGISTER request, call handle_register
                printf("Handling REGISTER request.\n");
                int ret = handle_register(message);
                if (ret == -1) {
                    printf("Handling REGISTER request failure.\n");
                }
                free(message);
                continue; // Continue to next message
            }

            // 1. Parse Call-ID
            // Locate "Call-ID:"
            char *call_id_start = strstr(message->buffer, "Call-ID:");
            if (call_id_start != NULL) {
                // Skip "Call-ID:" and spaces
                call_id_start += strlen("Call-ID:");
                while (*call_id_start == ' ') {
                    call_id_start++;
                }
                ptr = call_id_start;

                // Find the newline character, get the Call-ID
                while (*ptr != '\r' && *ptr != '\n' && *ptr != '\0') {
                    ptr++;
                }

                if (ptr - call_id_start > 0) {
                    strncpy(call_id, call_id_start, ptr - call_id_start);
                    call_id[ptr - call_id_start] = '\0'; 
                    printf("  Call-ID:       [%s]\r\n", call_id);
                } else {
                    printf("  Failed to parse Call-ID\r\n");
                }
            } else {
                //printf("  Call-ID not found\r\n");
            }

            // 2. Parse Content-Type, only check for application/sdp
            content_type_start = strstr(message->buffer, "Content-Type: ");
            if (content_type_start != NULL) {
                content_type_start += strlen("Content-Type: ");
                ptr = content_type_start;
                while (*ptr != '\r' && *ptr != '\n' && *ptr != '\0') {
                    ptr++;
                }
                if (ptr - content_type_start > 0) {
                    strncpy(content_type, content_type_start, ptr - content_type_start);
                    content_type[ptr - content_type_start] = '\0';
                    if (strstr(content_type, "application/sdp")) {
                        printf("  Content-Type:  [%s]\r\n", content_type);
                        has_sdp = true;
                    }
                }
            }

            // 3. Parse Request Method (INVITE, ACK, BYE, etc.) or Status Code
            // Find the first space
            ptr = message->buffer; // reset ptr to the beginning of buffer
            while (*ptr != ' ' && *ptr != '\0' && *ptr != '\r' && *ptr != '\n') {
                ptr++;
            }

            if (*ptr == ' ') {
                // Check if it is a response line (starts with "SIP/2.0")
                if (strncmp(message->buffer, "SIP/2.0", 7) == 0) {
                    // If it is a response line, parse the status code
                    while (*ptr == ' ') {
                        ptr++; // Skip any leading spaces
                    }

                    const char *code_start = ptr;
                    while (isdigit(*ptr)) {
                        ptr++;  // Move ptr to the first non-digit character
                    }

                    // Check if we correctly found a number
                    if (ptr > code_start) {
                        // Copy the response code string and convert it to an integer
                        strncpy(method, code_start, ptr - code_start);
                        method[ptr - code_start] = '\0'; // Null-terminate the string
                        response_code = atoi(method); // Convert to integer

                        // Print the parsed response code
                        printf("  Response Code: [%d] (parsed)\r\n", response_code);
                    } else {
                        // Print the position of code_start and ptr to help debug why parsing failed
                        printf("  Failed to parse response code, ptr: [%p], code_start: [%p]\r\n", (void*)ptr, (void*)code_start);
                        free(message); //Free the message before continue
                        continue; // Skip processing if no response code
                    }

                    // Check if the response is for an INVITE
                    const char *cseq_start = strstr(message->buffer, "CSeq:");
                    if (cseq_start != NULL) {
                        ptr = cseq_start;
                        while (*ptr != '\r' && *ptr != '\n' && *ptr != '\0') {
                            ptr++;
                        }
                        if (ptr - cseq_start > 0) {
                            strncpy(cseq_header, cseq_start, ptr - cseq_start);
                            cseq_header[ptr - cseq_start] = '\0';
                            if (strstr(cseq_header, "INVITE") || strstr(cseq_header, "CANCEL") || strstr(cseq_header, "BYE")) { 
                                // Only INVITE / CANCEL /BYE responses be handled by state machine for the minimal sip server
                                printf("  Response Code: [%d] (for %s).\r\n", response_code, cseq_header);
                                call = find_call_by_callid(&call_map, call_id, &leg_type);
                                handle_state_machine(call, STATUS_CODE, method, has_sdp, message, message->buffer, leg_type);
                            } else {
                                printf("  Response Code: [%d] (for %s), discarded.\r\n", response_code, cseq_header);
                                free(message); //Free the message before continue
                                continue;  
                            }
                        } else {
                            printf("  empty CSeq:, discard response\r\n");
                            free(message); //Free the message before continue
                            continue; 
                        }
                    } else {
                        printf("  No CSeq:, discard response\r\n");
                        free(message); //Free the message before continue
                        continue; 
                    }


                } else {
                    strncpy(method, message->buffer, ptr - message->buffer);
                    method[ptr - message->buffer] = '\0'; 
                    printf("  Method:        [%s]\r\n", method);
                    call = find_call_by_callid(&call_map, call_id, &leg_type);
                    handle_state_machine(call, REQUEST_METHOD, method, has_sdp, message, message->buffer, leg_type);
                }
            } else {
                printf("  Failed to parse Method or Response Code\r\n");
            }

            free(message);
        }
    }
    return NULL;
}

/**
 * @brief Destroys a call map
 */
void destroy_call_map() {
    pthread_mutex_destroy(&call_map.mutex);
}

/**
 * @brief Initializes a single call struct with default values.
 * @param call A pointer to the call struct to be initialized.
 * @param index The index of the call struct in the call map array.
 */
void init_call(call_t *call, int index) {
    pthread_mutex_init(&call->mutex, NULL);
    memset(call->a_leg_uuid, 0, sizeof(call->a_leg_uuid));
    memset(call->b_leg_uuid, 0, sizeof(call->b_leg_uuid));
    call->call_state = CALL_STATE_IDLE;
    call->a_leg_media.local_media = false;
    call->a_leg_media.remote_media = false;
    call->b_leg_media.local_media = false;
    call->b_leg_media.remote_media = false;
    memset(&call->a_leg_ip_str, 0, sizeof(call->a_leg_ip_str));
    memset(&call->b_leg_ip_str, 0, sizeof(call->b_leg_ip_str));
    call->a_leg_port = 0;
    call->b_leg_port = 0;
    call->index = index;
    memset(&call->a_leg_header, 0, sizeof(call->a_leg_header));
    memset(&call->b_leg_header, 0, sizeof(call->b_leg_header));
    memset(call->caller, 0, sizeof(call->caller));   // Initialize caller
    memset(call->callee, 0, sizeof(call->callee));   // Initialize callee
    call->is_active = false;
}
/**
 * @brief Initialize a call map.
 */
void init_call_map() {
    call_map.size = 0;
    pthread_mutex_init(&call_map.mutex, NULL);
    
    for (int i = 0; i < MAX_CALLS; i++) {
        init_call(&call_map.calls[i], i);
    }
}

/**
 * @brief Find a call by sip Call-ID.
 * @param call_map A pointer to the call map.
 * @param call_id The call_id to search for.
 * @param leg_type A pointer to store the leg type where the call_id was found.
 * @return A pointer to the call struct if found, otherwise NULL.
 * 
 * @To simplify demo implementation, leg_uuid in the call control module temporarily reuses the Call-Id from SIP messages.
 * @However, in a commercial system, the switch and the SIP protocol stack are usually two modules, and these values are usually different with a mapping and exchange process.
 */
call_t* find_call_by_callid(call_map_t *call_map, const char *call_id, int *leg_type) {
    if (call_map == NULL || call_id == NULL) {
        return NULL;
    }

    pthread_mutex_lock(&call_map->mutex);
    for (int i = 0; i < MAX_CALLS; i++) {
        if (call_map->calls[i].is_active) {
            if (strcmp(call_map->calls[i].a_leg_uuid, call_id) == 0) {
                *leg_type = A_LEG;
                pthread_mutex_unlock(&call_map->mutex);
                return &call_map->calls[i];
            }
            if (strcmp(call_map->calls[i].b_leg_uuid, call_id) == 0) {
                *leg_type = B_LEG;
                pthread_mutex_unlock(&call_map->mutex);
                return &call_map->calls[i];
            }
        }
    }
    pthread_mutex_unlock(&call_map->mutex);
    return NULL;
}

/**
 * @brief Allocates a new call from the call map.
 * @param call_map A pointer to the call map.
 * @return A pointer to a newly allocated call struct, or NULL if the call map is full.
 */
call_t* allocate_new_call(call_map_t *call_map) {

    if (call_map == NULL) {
        return NULL;
    }
    pthread_mutex_lock(&call_map->mutex);
    if(call_map->size >= MAX_CALLS){
        pthread_mutex_unlock(&call_map->mutex);
        return NULL;
    }
    for (int i = 0; i < MAX_CALLS; i++) {
        if (!call_map->calls[i].is_active) {
            call_map->calls[i].is_active = true;
            call_map->size++;
            pthread_mutex_unlock(&call_map->mutex);
            return &call_map->calls[i];
        }
    }
    pthread_mutex_unlock(&call_map->mutex);
    return NULL;
}

/**
 * @brief Find a location entry by its uri
 * @param location_map A pointer to the location map
 * @param uri The uri of the location to search for
 * @return A pointer to the location entry if found, otherwise NULL
 */
location_entry_t* find_location_entry_by_userid(const char *uri) {

    if (uri == NULL) {
        return NULL;
    }
    for (int i = 0; i < location_size; i++) {
        if (strcmp(location_entries[i].username, uri) == 0) {
           return &location_entries[i];
        }
    }
    return NULL;
}
