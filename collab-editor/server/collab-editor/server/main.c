#include "mongoose.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_SESSIONS 100
#define MAX_CLIENTS 50
#define MAX_DOCUMENT_SIZE 1048576
#define SESSION_EXPIRY 3600

// Hardcoded users
typedef struct { char username[64]; char password[64]; } User;
static User users[] = { {"alice","alice123"}, {"bob","bob123"}, {"charlie","charlie123"} };
static int user_count = 3;

// Sessions
typedef struct {
    char session_id[64];
    char username[64];
    time_t expires;
    int active;
} Session;

static Session sessions[MAX_SESSIONS];

// Document
typedef struct {
    char content[MAX_DOCUMENT_SIZE];
    int length;
} Document;

static Document document;

// Clients
typedef struct {
    struct mg_connection *conn;
    char session_id[64];
    char username[64];
    char color[8];
    int cursor_offset;
    int selection_start;
    int selection_end;
} Client;

static Client clients[MAX_CLIENTS];
static const char *user_colors[] = { "#FF6B6B","#4ECDC4","#45B7D1","#FFA07A","#98D8C8","#F7DC6F","#BB8FCE","#85C1E2","#F8B739","#52B788" };

// -------------------- UTILITIES --------------------
void generate_session_id(char *out) {
    sprintf(out,"sess_%ld_%d",time(NULL),rand()%10000);
}

User* authenticate_user(const char *username,const char *password){
    for(int i=0;i<user_count;i++)
        if(strcmp(users[i].username,username)==0 && strcmp(users[i].password,password)==0)
            return &users[i];
    return NULL;
}

Session* find_session(const char *session_id){
    for(int i=0;i<MAX_SESSIONS;i++){
        if(sessions[i].active && strcmp(sessions[i].session_id,session_id)==0){
            if(time(NULL)<sessions[i].expires) return &sessions[i];
            else sessions[i].active=0;
        }
    }
    return NULL;
}

Session* create_session(const char *username){
    for(int i=0;i<MAX_SESSIONS;i++){
        if(!sessions[i].active){
            generate_session_id(sessions[i].session_id);
            strncpy(sessions[i].username,username,63);
            sessions[i].expires = time(NULL)+SESSION_EXPIRY;
            sessions[i].active=1;
            return &sessions[i];
        }
    }
    return NULL;
}

void delete_session(const char *session_id){
    for(int i=0;i<MAX_SESSIONS;i++)
        if(sessions[i].active && strcmp(sessions[i].session_id,session_id)==0)
            sessions[i].active=0;
}

// -------------------- CLIENT HANDLING --------------------
Client* add_client(struct mg_connection *conn,const char *session_id,const char *username){
    for(int i=0;i<MAX_CLIENTS;i++){
        if(clients[i].conn==NULL){
            clients[i].conn=conn;
            strncpy(clients[i].session_id,session_id,63);
            strncpy(clients[i].username,username,63);
            strncpy(clients[i].color,user_colors[i%10],7);
            clients[i].cursor_offset=0;
            clients[i].selection_start=0;
            clients[i].selection_end=0;
            return &clients[i];
        }
    }
    return NULL;
}

void remove_client(struct mg_connection *conn){
    for(int i=0;i<MAX_CLIENTS;i++)
        if(clients[i].conn==conn) clients[i].conn=NULL;
}

Client* find_client(struct mg_connection *conn){
    for(int i=0;i<MAX_CLIENTS;i++)
        if(clients[i].conn==conn) return &clients[i];
    return NULL;
}

// -------------------- JSON ESCAPING --------------------
void escape_json_string(const char *input,char *output,int max_len){
    int j=0;
    for(int i=0;input[i]!='\0' && j<max_len-2;i++){
        if(input[i]=='"'||input[i]=='\\') output[j++]='\\';
        if(input[i]=='\n'){output[j++]='\\'; output[j++]='n';}
        else if(input[i]=='\r'){output[j++]='\\'; output[j++]='r';}
        else if(input[i]=='\t'){output[j++]='\\'; output[j++]='t';}
        else output[j++]=input[i];
    }
    output[j]='\0';
}

void unescape_json_string(const char *input,char *output,int max_len){
    int j=0;
    for(int i=0;input[i]!='\0' && j<max_len-1;i++){
        if(input[i]=='\\' && input[i+1]!='\0'){
            i++;
            if(input[i]=='n') output[j++]='\n';
            else if(input[i]=='r') output[j++]='\r';
            else if(input[i]=='t') output[j++]='\t';
            else if(input[i]=='"') output[j++]='"';
            else if(input[i]=='\\') output[j++]='\\';
            else output[j++]=input[i];
        }else output[j++]=input[i];
    }
    output[j]='\0';
}

// -------------------- SIMPLE JSON PARSERS --------------------
int parse_json_string(const char *json,const char *key,char *buf,int buf_len){
    char search[128]; snprintf(search,sizeof(search),"\"%s\":",key);
    const char *start = strstr(json,search);
    if(!start) return -1;
    start = strchr(start,'"'); if(!start) return -1; start++;
    const char *end = strchr(start,'"'); if(!end) return -1;
    int len = end-start; if(len>=buf_len) len=buf_len-1;
    strncpy(buf,start,len); buf[len]='\0';
    return len;
}

int parse_json_int(const char *json,const char *key,int *val){
    char search[128]; snprintf(search,sizeof(search),"\"%s\":",key);
    const char *start=strstr(json,search); if(!start) return -1;
    start=strchr(start,':'); if(!start) return -1; start++;
    while(*start==' '||*start=='\t') start++;
    *val=atoi(start); return 0;
}

// -------------------- BROADCAST --------------------
void broadcast_message(struct mg_connection *exclude,const char *msg,size_t len){
    for(int i=0;i<MAX_CLIENTS;i++)
        if(clients[i].conn!=NULL && clients[i].conn!=exclude)
            mg_send_websocket_frame(clients[i].conn,WEBSOCKET_OP_TEXT,msg,len);
}

void send_user_list_to_all(){
    char buf[4096]; int offset=sprintf(buf,"{\"type\":\"users\",\"users\":[");
    int first=1;
    for(int i=0;i<MAX_CLIENTS;i++){
        if(clients[i].conn!=NULL){
            if(!first) offset+=sprintf(buf+offset,",");
            offset+=sprintf(buf+offset,"{\"username\":\"%s\",\"color\":\"%s\"}",clients[i].username,clients[i].color);
            first=0;
        }
    }
    offset+=sprintf(buf+offset,"]}");
    for(int i=0;i<MAX_CLIENTS;i++)
        if(clients[i].conn!=NULL)
            mg_send_websocket_frame(clients[i].conn,WEBSOCKET_OP_TEXT,buf,offset);
}

// -------------------- HANDLERS --------------------
static void handle_login(struct mg_connection *c, struct http_message *hm){
    char username[64]={0},password[64]={0};
    char body[4096]; snprintf(body,sizeof(body),"%.*s",(int)hm->body.len,hm->body.p);
    if(parse_json_string(body,"username",username,sizeof(username))<0 ||
       parse_json_string(body,"password",password,sizeof(password))<0){
        mg_printf(c,"HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n"
                     "{\"success\":false,\"error\":\"Invalid request\"}");
        c->flags|=MG_F_SEND_AND_CLOSE; return;
    }
    User *user=authenticate_user(username,password);
    if(user){
        Session *sess=create_session(user->username);
        if(sess)
            mg_printf(c,"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
                     "{\"success\":true,\"session_id\":\"%s\",\"username\":\"%s\"}",
                     sess->session_id,sess->username);
        else
            mg_printf(c,"HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\n\r\n"
                     "{\"success\":false,\"error\":\"Server full\"}");
    }else{
        mg_printf(c,"HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\n\r\n"
                     "{\"success\":false,\"error\":\"Invalid credentials\"}");
    }
    c->flags|=MG_F_SEND_AND_CLOSE;
}

static void handle_logout(struct mg_connection *c, struct http_message *hm){
    char session_id[64]={0},body[1024];
    snprintf(body,sizeof(body),"%.*s",(int)hm->body.len,hm->body.p);
    parse_json_string(body,"session_id",session_id,sizeof(session_id));
    if(strlen(session_id)>0) delete_session(session_id);
    mg_printf(c,"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"success\":true}");
    c->flags|=MG_F_SEND_AND_CLOSE;
}

static void handle_verify(struct mg_connection *c, struct http_message *hm){
    char session_id[64]={0}; char query[256];
    snprintf(query,sizeof(query),"%.*s",(int)hm->query_string.len,hm->query_string.p);
    const char *sid_start=strstr(query,"session_id=");
    if(sid_start){ sid_start+=11; const char *sid_end=strchr(sid_start,'&'); int len=sid_end?(sid_end-sid_start):strlen(sid_start);
        if(len>63) len=63; strncpy(session_id,sid_start,len); session_id[len]='\0'; }
    Session *sess=find_session(session_id);
    if(sess)
        mg_printf(c,"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"valid\":true,\"username\":\"%s\"}",sess->username);
    else
        mg_printf(c,"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n{\"valid\":false}");
    c->flags|=MG_F_SEND_AND_CLOSE;
}

// -------------------- WEBSOCKET --------------------
static void handle_ws(struct mg_connection *c, struct websocket_message *wm){
    Client *client=find_client(c);
    char msg[MAX_DOCUMENT_SIZE]; snprintf(msg,sizeof(msg),"%.*s",(int)wm->size,(char*)wm->data);
    char type[32]; parse_json_string(msg,"type",type,sizeof(type));

    if(strcmp(type,"auth")==0){
        char session_id[64]; parse_json_string(msg,"session_id",session_id,sizeof(session_id));
        Session *sess=find_session(session_id);
        if(sess){
            Client *new_client=add_client(c,sess->session_id,sess->username);
            if(new_client){
                char buf[MAX_DOCUMENT_SIZE+256],escaped[MAX_DOCUMENT_SIZE];
                escape_json_string(document.content,escaped,sizeof(escaped));
                int len=sprintf(buf,"{\"type\":\"init\",\"content\":\"%s\",\"color\":\"%s\"}",escaped,new_client->color);
                mg_send_websocket_frame(c,WEBSOCKET_OP_TEXT,buf,len);
                send_user_list_to_all();
            }
        }
    }
    else if(client){
        if(strcmp(type,"update")==0){
            const char *content_start=strstr(msg,"\"content\":\"");
            if(content_start){
                content_start+=11; char temp[MAX_DOCUMENT_SIZE]; int pos=0;
                for(int i=0;content_start[i]!='\0' && pos<MAX_DOCUMENT_SIZE-1;i++){
                    if(content_start[i]=='"' && content_start[i-1]!='\\') break;
                    temp[pos++]=content_start[i];
                }
                temp[pos]='\0';
                unescape_json_string(temp,document.content,sizeof(document.content));
                document.length=strlen(document.content);
            }
            broadcast_message(c,msg,strlen(msg));
        }
        else if(strcmp(type,"cursor")==0){
            int offset=0; parse_json_int(msg,"offset",&offset);
            client->cursor_offset=offset;
            char buf[512]; int len=sprintf(buf,
                "{\"type\":\"cursor\",\"username\":\"%s\",\"color\":\"%s\",\"offset\":%d}",
                client->username,client->color,offset);
            broadcast_message(c,buf,len);
        }
        else if(strcmp(type,"selection")==0){
            int start=0,end=0; parse_json_int(msg,"start",&start); parse_json_int(msg,"end",&end);
            client->selection_start=start; client->selection_end=end;
            char buf[512]; int len=sprintf(buf,
                "{\"type\":\"selection\",\"username\":\"%s\",\"color\":\"%s\",\"start\":%d,\"end\":%d}",
                client->username,client->color,start,end);
            broadcast_message(c,buf,len);
        }
        else if(strcmp(type,"chat")==0){
            char message[1024]; parse_json_string(msg,"message",message,sizeof(message));
            char buf[1152]; int len=sprintf(buf,
                "{\"type\":\"chat\",\"username\":\"%s\",\"color\":\"%s\",\"message\":\"%s\"}",
                client->username,client->color,message);
            broadcast_message(NULL,buf,len);
        }
    }
}

// -------------------- EVENT HANDLER --------------------
static void ev_handler(struct mg_connection *c,int ev,void *ev_data){
    if(ev==MG_EV_HTTP_REQUEST){
        struct http_message *hm=(struct http_message*)ev_data;
        if(mg_vcmp(&hm->uri,"/api/login")==0) handle_login(c,hm);
        else if(mg_vcmp(&hm->uri,"/api/logout")==0) handle_logout(c,hm);
        else if(mg_vcmp(&hm->uri,"/api/verify")==0) handle_verify(c,hm);
        else mg_serve_http(c,hm,(struct mg_serve_http_opts){.document_root="."});
    }
    else if(ev==MG_EV_WEBSOCKET_FRAME) handle_ws(c,(struct websocket_message*)ev_data);
    else if(ev==MG_EV_CLOSE){ if(c->flags & MG_F_IS_WEBSOCKET){ remove_client(c); send_user_list_to_all(); }}
}

// -------------------- MAIN --------------------
int main(void){
    struct mg_mgr mgr; struct mg_connection *nc;
    srand(time(NULL));
    memset(sessions,0,sizeof(sessions)); memset(clients,0,sizeof(clients));
    memset(&document,0,sizeof(document)); strcpy(document.content,"Welcome to Collaborative Editor!"); document.length=strlen(document.content);

    mg_mgr_init(&mgr,NULL);
    nc=mg_bind(&mgr,"8000",ev_handler);
    mg_set_protocol_http_websocket(nc);

    printf("Collaborative Editor Server started at http://localhost:8000\nDemo accounts: alice/alice123 bob/bob123 charlie/charlie123\n");

    for(;;) mg_mgr_poll(&mgr,1000);
    mg_mgr_free(&mgr);
    return 0;
}
