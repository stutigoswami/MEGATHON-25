#include "mongoose.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_SESSIONS 50
#define MAX_CLIENTS 50
#define MAX_DOC_SIZE 1048576
#define SESSION_EXPIRY 3600

// ---------------- Hardcoded Users ----------------
typedef struct { char username[32]; char password[32]; } User;
static User users[] = { {"alice","alice123"}, {"bob","bob123"}, {"charlie","charlie123"} };
static int user_count = 3;

// ---------------- Sessions ----------------
typedef struct { char session_id[64]; char username[32]; time_t expires; int active; } Session;
static Session sessions[MAX_SESSIONS];

// ---------------- Clients ----------------
typedef struct {
    struct mg_connection *conn;
    char username[32];
    char session_id[64];
    char color[8];
} Client;
static Client clients[MAX_CLIENTS];

// ---------------- Document ----------------
typedef struct { char content[MAX_DOC_SIZE]; int length; } Document;
static Document document;

// ---------------- User Colors ----------------
static const char *user_colors[] = { "#FF6B6B","#4ECDC4","#45B7D1","#FFA07A","#98D8C8","#F7DC6F","#BB8FCE","#85C1E2" };

// ---------------- Helper Functions ----------------
void generate_session_id(char *out) { sprintf(out,"sess_%ld_%d",time(NULL),rand()%10000); }

User* authenticate_user(const char *u,const char *p) {
    for(int i=0;i<user_count;i++) if(strcmp(users[i].username,u)==0 && strcmp(users[i].password,p)==0) return &users[i];
    return NULL;
}

Session* find_session(const char *sid) {
    for(int i=0;i<MAX_SESSIONS;i++){
        if(sessions[i].active && strcmp(sessions[i].session_id,sid)==0){
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
            strncpy(sessions[i].username,username,31);
            sessions[i].username[31]='\0';
            sessions[i].expires=time(NULL)+SESSION_EXPIRY;
            sessions[i].active=1;
            return &sessions[i];
        }
    }
    return NULL;
}

void delete_session(const char *sid){
    for(int i=0;i<MAX_SESSIONS;i++) if(sessions[i].active && strcmp(sessions[i].session_id,sid)==0) sessions[i].active=0;
}

Client* add_client(struct mg_connection *c,const char *username,const char *sid){
    for(int i=0;i<MAX_CLIENTS;i++){
        if(clients[i].conn==NULL){
            clients[i].conn=c;
            strncpy(clients[i].username,username,31); clients[i].username[31]='\0';
            strncpy(clients[i].session_id,sid,63); clients[i].session_id[63]='\0';
            strncpy(clients[i].color,user_colors[i%8],7); clients[i].color[7]='\0';
            return &clients[i];
        }
    }
    return NULL;
}

void remove_client(struct mg_connection *c){
    for(int i=0;i<MAX_CLIENTS;i++) if(clients[i].conn==c) clients[i].conn=NULL;
}

Client* find_client(struct mg_connection *c){
    for(int i=0;i<MAX_CLIENTS;i++) if(clients[i].conn==c) return &clients[i];
    return NULL;
}

// Escape/unescape JSON strings
void escape_json(const char *input,char *out,int max_len){
    int j=0;
    for(int i=0;input[i]!='\0' && j<max_len-2;i++){
        if(input[i]=='"'||input[i]=='\\'){ out[j++]='\\'; out[j++]=input[i]; continue;}
        if(input[i]=='\n'){ out[j++]='\\'; out[j++]='n'; continue;}
        out[j]=input[i]; j++;
    } out[j]='\0';
}

// ---------------- Broadcast ----------------
void broadcast_message(struct mg_connection *exclude,const char *msg,int len){
    for(int i=0;i<MAX_CLIENTS;i++){
        if(clients[i].conn && clients[i].conn!=exclude){
            mg_send_websocket_frame(clients[i].conn,WEBSOCKET_OP_TEXT,msg,len);
        }
    }
}

void send_user_list_to_all(){
    char buf[4096]; int offset=sprintf(buf,"{\"type\":\"users\",\"users\":[");
    int first=1;
    for(int i=0;i<MAX_CLIENTS;i++){
        if(clients[i].conn){
            if(!first) offset+=sprintf(buf+offset,",");
            offset+=sprintf(buf+offset,"{\"username\":\"%s\",\"color\":\"%s\"}",clients[i].username,clients[i].color);
            first=0;
        }
    }
    offset+=sprintf(buf+offset,"]}");
    for(int i=0;i<MAX_CLIENTS;i++) if(clients[i].conn) mg_send_websocket_frame(clients[i].conn,WEBSOCKET_OP_TEXT,buf,offset);
}

// ---------------- JSON Parser ----------------
int parse_json_string(const char *json,const char *key,char *out,int max_len){
    char search[64]; snprintf(search,sizeof(search),"\"%s\":\"",key);
    const char *s=strstr(json,search); if(!s) return -1;
    s+=strlen(search); const char *e=strchr(s,'"'); if(!e) return -1;
    int l=e-s; if(l>=max_len) l=max_len-1; strncpy(out,s,l); out[l]='\0'; return l;
}

int parse_json_int(const char *json,const char *key,int *val){
    char search[64]; snprintf(search,sizeof(search),"\"%s\":",key);
    const char *s=strstr(json,search); if(!s) return -1;
    s=strchr(s,':'); if(!s) return -1; s++;
    while(*s==' '||*s=='\t') s++;
    *val=atoi(s); return 0;
}

// ---------------- Handlers ----------------
static void handle_login(struct mg_connection *c, struct http_message *hm){
    char body[512],username[32],password[32]; snprintf(body,sizeof(body),"%.*s",(int)hm->body.len,hm->body.p);
    if(parse_json_string(body,"username",username,sizeof(username))<0 || parse_json_string(body,"password",password,sizeof(password))<0){
        mg_printf(c,"HTTP/1.1 400\r\nContent-Type: application/json\r\n\r\n{\"success\":false,\"error\":\"Invalid request\"}");
        c->flags|=MG_F_SEND_AND_CLOSE; return;
    }
    User *u=authenticate_user(username,password);
    if(u){
        Session *sess=create_session(u->username);
        mg_printf(c,"HTTP/1.1 200\r\nContent-Type: application/json\r\n\r\n{\"success\":true,\"session_id\":\"%s\",\"username\":\"%s\"}",sess->session_id,sess->username);
    } else mg_printf(c,"HTTP/1.1 401\r\nContent-Type: application/json\r\n\r\n{\"success\":false,\"error\":\"Invalid credentials\"}");
    c->flags|=MG_F_SEND_AND_CLOSE;
}

static void handle_logout(struct mg_connection *c,struct http_message *hm){
    char body[128],sid[64]; snprintf(body,sizeof(body),"%.*s",(int)hm->body.len,hm->body.p);
    parse_json_string(body,"session_id",sid,sizeof(sid));
    if(strlen(sid)>0) delete_session(sid);
    mg_printf(c,"HTTP/1.1 200\r\nContent-Type: application/json\r\n\r\n{\"success\":true}");
    c->flags|=MG_F_SEND_AND_CLOSE;
}

static void handle_verify(struct mg_connection *c,struct http_message *hm){
    char sid[64]={0}; char query[128]; snprintf(query,sizeof(query),"%.*s",(int)hm->query_string.len,hm->query_string.p);
    const char *start=strstr(query,"session_id="); if(start){ start+=11; const char *end=strchr(start,'&'); int l=end?end-start:(int)strlen(start); if(l>63) l=63; strncpy(sid,start,l); sid[l]='\0'; }
    Session *s=find_session(sid);
    if(s) mg_printf(c,"HTTP/1.1 200\r\nContent-Type: application/json\r\n\r\n{\"valid\":true,\"username\":\"%s\"}",s->username);
    else mg_printf(c,"HTTP/1.1 200\r\nContent-Type: application/json\r\n\r\n{\"valid\":false}");
    c->flags|=MG_F_SEND_AND_CLOSE;
}

// ---------------- WebSocket ----------------
static void handle_ws(struct mg_connection *c,struct websocket_message *wm){
    char msg[MAX_DOC_SIZE]; snprintf(msg,sizeof(msg),"%.*s",(int)wm->size,(char*)wm->data);
    Client *client=find_client(c); char type[32]={0};
    if(parse_json_string(msg,"type",type,sizeof(type))<0) return;
    
    if(strcmp(type,"auth")==0){
        char sid[64]={0}; parse_json_string(msg,"session_id",sid,sizeof(sid));
        Session *s=find_session(sid);
        if(s){ add_client(c,s->username,s->session_id);
            char buf[MAX_DOC_SIZE+128],escaped[MAX_DOC_SIZE]; escape_json(document.content,escaped,sizeof(escaped));
            int len=sprintf(buf,"{\"type\":\"init\",\"content\":\"%s\"}",escaped);
            mg_send_websocket_frame(c,WEBSOCKET_OP_TEXT,buf,len);
            send_user_list_to_all();
        }
    }
    else if(client){
        if(strcmp(type,"update")==0){
            const char *start=strstr(msg,"\"content\":\""); if(start){ start+=11;
                char temp[MAX_DOC_SIZE]; int pos=0; for(int i=0;start[i]!='\0' && pos<MAX_DOC_SIZE-1;i++){ if(start[i]=='"' && (i==0 || start[i-1]!='\\')) break; temp[pos++]=start[i];} temp[pos]='\0';
                escape_json(temp,document.content,sizeof(document.content)); document.length=strlen(document.content);
            }
            broadcast_message(c,msg,strlen(msg));
        }
        else if(strcmp(type,"cursor")==0){
            int offset=0; if(parse_json_int(msg,"offset",&offset)==0){
                char buf[256]; int len=sprintf(buf,"{\"type\":\"cursor\",\"username\":\"%s\",\"color\":\"%s\",\"offset\":%d}",client->username,client->color,offset);
                broadcast_message(c,buf,len);
            }
        }
        else if(strcmp(type,"selection")==0){
            int start=0,end=0; if(parse_json_int(msg,"start",&start)==0 && parse_json_int(msg,"end",&end)==0){
                char buf[256]; int len=sprintf(buf,"{\"type\":\"selection\",\"username\":\"%s\",\"color\":\"%s\",\"start\":%d,\"end\":%d}",client->username,client->color,start,end);
                broadcast_message(c,buf,len);
            }
        }
        else if(strcmp(type,"chat")==0){
            char chat_msg[512]={0}; parse_json_string(msg,"message",chat_msg,sizeof(chat_msg));
            char buf[1024]; int len=sprintf(buf,"{\"type\":\"chat\",\"username\":\"%s\",\"message\":\"%s\"}",client->username,chat_msg);
            broadcast_message(NULL,buf,len);
        }
    }
}

// ---------------- Event Handler ----------------
static void ev_handler(struct mg_connection *c,int ev,void *ev_data){
    if(ev==MG_EV_HTTP_REQUEST){
        struct http_message *hm=(struct http_message*)ev_data;
        if(mg_vcmp(&hm->uri,"/api/login")==0) handle_login(c,hm);
        else if(mg_vcmp(&hm->uri,"/api/logout")==0) handle_logout(c,hm);
        else if(mg_vcmp(&hm->uri,"/api/verify")==0) handle_verify(c,hm);
        else mg_serve_http(c,hm,(struct mg_serve_http_opts){.document_root="."});
    }
    else if(ev==MG_EV_WEBSOCKET_FRAME) handle_ws(c,(struct websocket_message*)ev_data);
    else if(ev==MG_EV_CLOSE) { if(c->flags&MG_F_IS_WEBSOCKET){ remove_client(c); send_user_list_to_all();} }
}

// ---------------- Main ----------------
int main(void){
    struct mg_mgr mgr; struct mg_connection *nc; srand(time(NULL));
    memset(sessions,0,sizeof(sessions));
    memset(clients,0,sizeof(clients));
    memset(&document,0,sizeof(document)); strcpy(document.content,"Welcome to Collaborative Editor! Start typing..."); document.length=strlen(document.content);

    mg_mgr_init(&mgr,NULL);
    nc=mg_bind(&mgr,"8000",ev_handler);
    mg_set_protocol_http_websocket(nc);
    struct mg_serve_http_opts opts; memset(&opts,0,sizeof(opts)); opts.document_root=".";
    
    printf("Collaborative Editor running on http://localhost:8000\n");
    for(;;) mg_mgr_poll(&mgr,1000);
    mg_mgr_free(&mgr); return 0;
}
