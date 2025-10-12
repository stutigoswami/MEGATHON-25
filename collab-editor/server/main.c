#include "mongoose.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define PORT "8080"
#define MAX_DOC_SIZE 50000
#define MAX_CHAT_MSG_SIZE 2000
#define SESSION_TIMEOUT 3600  // 1 hour in seconds
#define MAX_SESSIONS 100

static char document[MAX_DOC_SIZE] = "";
static int next_user_id = 1;

// Hardcoded users
struct user_credential {
    const char *username;
    const char *password;
};

static struct user_credential valid_users[] = {
    {"alice", "alice123"},
    {"bob", "bob123"},
    {"charlie", "charlie123"}
};

// Session structure
struct session {
    char session_id[65];  // 64 hex chars + null terminator
    char username[32];
    time_t expiry;
    int active;
};

static struct session sessions[MAX_SESSIONS];
static int sessions_initialized = 0;

struct user_data {
    int id;
    char color[8];
    int cursor_pos;
    int selection_start;
    int selection_end;
};

struct user_list {
    struct mg_connection *conn;
    struct user_data *ud;
    struct user_list *next;
};

static struct user_list *users_head = NULL;

// Initialize sessions
void init_sessions() {
    if(!sessions_initialized) {
        memset(sessions, 0, sizeof(sessions));
        sessions_initialized = 1;
    }
}

// Generate a random session ID
void generate_session_id(char *out) {
    const char *hex = "0123456789abcdef";
    for(int i = 0; i < 64; i++) {
        out[i] = hex[rand() % 16];
    }
    out[64] = '\0';
}

// Validate username and password
int validate_credentials(const char *username, const char *password) {
    for(size_t i = 0; i < sizeof(valid_users) / sizeof(valid_users[0]); i++) {
        if(strcmp(valid_users[i].username, username) == 0 &&
           strcmp(valid_users[i].password, password) == 0) {
            return 1;
        }
    }
    return 0;
}

// Create a new session
int create_session(const char *username, char *session_id_out) {
    time_t now = time(NULL);
    
    // Clean up expired sessions
    for(int i = 0; i < MAX_SESSIONS; i++) {
        if(sessions[i].active && sessions[i].expiry < now) {
            sessions[i].active = 0;
        }
    }
    
    // Find an empty slot
    for(int i = 0; i < MAX_SESSIONS; i++) {
        if(!sessions[i].active) {
            generate_session_id(sessions[i].session_id);
            strncpy(sessions[i].username, username, sizeof(sessions[i].username) - 1);
            sessions[i].username[sizeof(sessions[i].username) - 1] = '\0';
            sessions[i].expiry = now + SESSION_TIMEOUT;
            sessions[i].active = 1;
            
            strncpy(session_id_out, sessions[i].session_id, 64);
            session_id_out[64] = '\0';
            return 1;
        }
    }
    return 0;  // No available slots
}

// Validate a session
int validate_session(const char *session_id, char *username_out) {
    time_t now = time(NULL);
    
    for(int i = 0; i < MAX_SESSIONS; i++) {
        if(sessions[i].active && strcmp(sessions[i].session_id, session_id) == 0) {
            if(sessions[i].expiry < now) {
                sessions[i].active = 0;
                return 0;  // Expired
            }
            
            // Update expiry time (session refresh)
            sessions[i].expiry = now + SESSION_TIMEOUT;
            
            if(username_out) {
                strncpy(username_out, sessions[i].username, 31);
                username_out[31] = '\0';
            }
            return 1;  // Valid
        }
    }
    return 0;  // Not found
}

// Invalidate a session
void invalidate_session(const char *session_id) {
    for(int i = 0; i < MAX_SESSIONS; i++) {
        if(sessions[i].active && strcmp(sessions[i].session_id, session_id) == 0) {
            sessions[i].active = 0;
            return;
        }
    }
}

void add_user(struct mg_connection *nc, struct user_data *ud){
    struct user_list *node = malloc(sizeof(struct user_list));
    node->conn = nc;
    node->ud = ud;
    node->next = users_head;
    users_head = node;
}

void remove_user(struct mg_connection *nc){
    struct user_list **ptr = &users_head;
    while(*ptr){
        if((*ptr)->conn == nc){
            struct user_list *tmp = *ptr;
            *ptr = (*ptr)->next;
            free(tmp->ud);
            free(tmp);
            return;
        }
        ptr = &(*ptr)->next;
    }
}

void broadcast_all(const char *msg, size_t len){
    struct user_list *u = users_head;
    while(u){
        mg_send_websocket_frame(u->conn, WEBSOCKET_OP_TEXT, msg, len);
        u = u->next;
    }
}

void broadcast_users_list(){
    char msg[4000];
    int offset = snprintf(msg, sizeof(msg), "USERS|");
    
    struct user_list *u = users_head;
    int first = 1;
    while(u){
        if(!first) offset += snprintf(msg+offset, sizeof(msg)-offset, ",");
        offset += snprintf(msg+offset, sizeof(msg)-offset, "%d:%s:%d:%d:%d",
            u->ud->id, u->ud->color, u->ud->cursor_pos, 
            u->ud->selection_start, u->ud->selection_end);
        first = 0;
        u = u->next;
    }
    
    broadcast_all(msg, strlen(msg));
}

// HTTP endpoint: POST /api/login
void handle_login(struct mg_connection *nc, struct http_message *hm) {
    char username[100] = {0};
    char password[100] = {0};
    
    // Parse POST body for username and password
    mg_get_http_var(&hm->body, "username", username, sizeof(username));
    mg_get_http_var(&hm->body, "password", password, sizeof(password));
    
    if(strlen(username) == 0 || strlen(password) == 0) {
        mg_printf(nc, "HTTP/1.1 400 Bad Request\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: 37\r\n\r\n"
                      "{\"error\":\"Missing credentials\"}");
        nc->flags |= MG_F_SEND_AND_CLOSE;
        return;
    }
    
    if(validate_credentials(username, password)) {
        char session_id[65];
        if(create_session(username, session_id)) {
            char response[256];
            int len = snprintf(response, sizeof(response),
                "{\"session_id\":\"%s\",\"username\":\"%s\"}", 
                session_id, username);
            
            mg_printf(nc, "HTTP/1.1 200 OK\r\n"
                          "Content-Type: application/json\r\n"
                          "Content-Length: %d\r\n\r\n%s", len, response);
        } else {
            mg_printf(nc, "HTTP/1.1 500 Internal Server Error\r\n"
                          "Content-Type: application/json\r\n"
                          "Content-Length: 36\r\n\r\n"
                          "{\"error\":\"Session creation failed\"}");
        }
    } else {
        mg_printf(nc, "HTTP/1.1 401 Unauthorized\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: 35\r\n\r\n"
                      "{\"error\":\"Invalid credentials\"}");
    }
    nc->flags |= MG_F_SEND_AND_CLOSE;
}

// HTTP endpoint: POST /api/logout
void handle_logout(struct mg_connection *nc, struct http_message *hm) {
    char session_id[100] = {0};
    
    mg_get_http_var(&hm->body, "session_id", session_id, sizeof(session_id));
    
    if(strlen(session_id) > 0) {
        invalidate_session(session_id);
    }
    
    mg_printf(nc, "HTTP/1.1 200 OK\r\n"
                  "Content-Type: application/json\r\n"
                  "Content-Length: 17\r\n\r\n"
                  "{\"success\":true}");
    nc->flags |= MG_F_SEND_AND_CLOSE;
}

// HTTP endpoint: GET /api/verify
void handle_verify(struct mg_connection *nc, struct http_message *hm) {
    char session_id[100] = {0};
    char username[32] = {0};
    
    // Get session_id from query parameter
    mg_get_http_var(&hm->query_string, "session_id", session_id, sizeof(session_id));
    
    if(strlen(session_id) == 0) {
        mg_printf(nc, "HTTP/1.1 400 Bad Request\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: 36\r\n\r\n"
                      "{\"error\":\"Missing session_id\"}");
        nc->flags |= MG_F_SEND_AND_CLOSE;
        return;
    }
    
    if(validate_session(session_id, username)) {
        char response[256];
        int len = snprintf(response, sizeof(response),
            "{\"valid\":true,\"username\":\"%s\"}", username);
        
        mg_printf(nc, "HTTP/1.1 200 OK\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: %d\r\n\r\n%s", len, response);
    } else {
        mg_printf(nc, "HTTP/1.1 200 OK\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: 16\r\n\r\n"
                      "{\"valid\":false}");
    }
    nc->flags |= MG_F_SEND_AND_CLOSE;
}

static void ev_handler(struct mg_connection *nc, int ev, void *ev_data){
    if(ev == MG_EV_HTTP_REQUEST){
        struct http_message *hm = (struct http_message *)ev_data;
        
        // Check for API endpoints
        if(mg_vcmp(&hm->uri, "/api/login") == 0 && mg_vcmp(&hm->method, "POST") == 0) {
            handle_login(nc, hm);
            return;
        }
        else if(mg_vcmp(&hm->uri, "/api/logout") == 0 && mg_vcmp(&hm->method, "POST") == 0) {
            handle_logout(nc, hm);
            return;
        }
        else if(mg_vcmp(&hm->uri, "/api/verify") == 0 && mg_vcmp(&hm->method, "GET") == 0) {
            handle_verify(nc, hm);
            return;
        }
        
        // Serve static files
        struct mg_serve_http_opts opts;
        memset(&opts, 0, sizeof(opts));
        opts.document_root = "../client";
        mg_serve_http(nc, hm, opts);
    }
    else if(ev == MG_EV_WEBSOCKET_HANDSHAKE_REQUEST){
        // Validate session before allowing WebSocket upgrade
        struct http_message *hm = (struct http_message *)ev_data;
        char session_id[100] = {0};
        char username[32] = {0};
        
        // Get session_id from query parameter in WebSocket URL
        mg_get_http_var(&hm->query_string, "session_id", session_id, sizeof(session_id));
        
        if(strlen(session_id) == 0 || !validate_session(session_id, username)) {
            // Reject WebSocket connection
            mg_printf(nc, "HTTP/1.1 401 Unauthorized\r\n"
                          "Content-Length: 12\r\n\r\n"
                          "Unauthorized");
            nc->flags |= MG_F_SEND_AND_CLOSE;
            return;
        }
        
        // Store username in connection for later use
        // We'll retrieve it after handshake is done
    }
    else if(ev == MG_EV_WEBSOCKET_HANDSHAKE_DONE){
        struct user_data *ud = malloc(sizeof(struct user_data));
        ud->id = next_user_id++;
        
        // Generate random bright color
        int r = 100 + rand() % 156;
        int g = 100 + rand() % 156;
        int b = 100 + rand() % 156;
        snprintf(ud->color, sizeof(ud->color), "#%02X%02X%02X", r, g, b);
        
        ud->cursor_pos = 0;
        ud->selection_start = 0;
        ud->selection_end = 0;
        nc->user_data = ud;
        add_user(nc, ud);

        // Send init message with user ID, color, and document
        char buffer[MAX_DOC_SIZE + 200];
        int len = snprintf(buffer, sizeof(buffer), "INIT|%d|%s|", ud->id, ud->color);
        
        // Escape document content
        int doc_len = strlen(document);
        for(int i = 0; i < doc_len && len < sizeof(buffer) - 10; i++){
            if(document[i] == '|') buffer[len++] = '\\';
            buffer[len++] = document[i];
        }
        buffer[len] = '\0';
        
        mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, buffer, len);
        
        // Broadcast updated user list
        broadcast_users_list();
    }
    else if(ev == MG_EV_WEBSOCKET_FRAME){
        struct websocket_message *wm = (struct websocket_message *)ev_data;
        struct user_data *ud = (struct user_data*)nc->user_data;
        if(!ud || wm->size == 0) return;

        char msg[MAX_DOC_SIZE + 200];
        int len = wm->size < sizeof(msg)-1 ? wm->size : sizeof(msg)-1;
        memcpy(msg, wm->data, len);
        msg[len] = '\0';

        // Parse message type
        if(strncmp(msg, "DOC|", 4) == 0){
            // Document update
            char *content = msg + 4;
            int content_len = strlen(content);
            if(content_len < MAX_DOC_SIZE){
                strcpy(document, content);
                
                // Broadcast to all others
                char broadcast[MAX_DOC_SIZE + 50];
                snprintf(broadcast, sizeof(broadcast), "UPDATE|%d|%s", ud->id, content);
                
                struct user_list *u = users_head;
                while(u){
                    if(u->conn != nc){
                        mg_send_websocket_frame(u->conn, WEBSOCKET_OP_TEXT, 
                                              broadcast, strlen(broadcast));
                    }
                    u = u->next;
                }
            }
        }
        else if(strncmp(msg, "CURSOR|", 7) == 0){
            // Cursor/selection update
            char *data = msg + 7;
            sscanf(data, "%d|%d|%d", &ud->cursor_pos, 
                   &ud->selection_start, &ud->selection_end);
            
            // Broadcast user list with updated cursor positions
            broadcast_users_list();
        }
        else if(strncmp(msg, "CHAT|", 5) == 0){
            // Chat message
            char *content = msg + 5;
            int content_len = strlen(content);
            
            // Validate message length
            if(content_len > MAX_CHAT_MSG_SIZE){
                return; // Ignore overly long messages
            }
            
            time_t now = time(NULL);
            
            // Broadcast to all users
            char broadcast[MAX_CHAT_MSG_SIZE + 200];
            snprintf(broadcast, sizeof(broadcast), "CHAT|%d|%s|%ld|%s", 
                     ud->id, ud->color, (long)now, content);
            
            broadcast_all(broadcast, strlen(broadcast));
        }
    }
    else if(ev == MG_EV_CLOSE){
        if(nc->user_data){
            remove_user(nc);
            nc->user_data = NULL;
            broadcast_users_list();
        }
    }
}

int main(void){
    struct mg_mgr mgr;
    struct mg_connection *nc;

    srand(time(NULL));
    init_sessions();
    
    mg_mgr_init(&mgr, NULL);
    
    nc = mg_bind(&mgr, PORT, ev_handler);
    if(!nc){
        printf("Failed to bind to port %s\n", PORT);
        return 1;
    }

    mg_set_protocol_http_websocket(nc);
    printf("Collaborative editor running on http://localhost:%s\n", PORT);

    for(;;) mg_mgr_poll(&mgr, 1000);
    mg_mgr_free(&mgr);
    return 0;
}
