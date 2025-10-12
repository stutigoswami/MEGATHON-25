#include "mongoose.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define PORT "8080"
#define MAX_DOC_SIZE 50000

static char document[MAX_DOC_SIZE] = "";
static int next_user_id = 1;

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

static void ev_handler(struct mg_connection *nc, int ev, void *ev_data){
    if(ev == MG_EV_HTTP_REQUEST){
        struct http_message *hm = (struct http_message *)ev_data;
        struct mg_serve_http_opts opts;
        memset(&opts, 0, sizeof(opts));
        opts.document_root = "../client";
        mg_serve_http(nc, hm, opts);
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
            time_t now = time(NULL);
            
            // Broadcast to all users
            char broadcast[MAX_DOC_SIZE + 100];
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
