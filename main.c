// main.c
// Mongoose 6.18 compatible collaborative server
#include "mongoose.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PORT "8080"
#define MAX_DOC 20000

static char document[MAX_DOC] = "";
static int next_user_id = 1;

struct user_data {
  int id;
  char color[16];
  int sel_start;
  int sel_end;
  struct mg_connection *nc;
  struct user_data *next;
};

static struct user_data *users_head = NULL;

void add_user_node(struct user_data *u) {
  u->next = users_head;
  users_head = u;
}

void remove_user_node(struct mg_connection *nc) {
  struct user_data **p = &users_head;
  while (*p) {
    if ((*p)->nc == nc) {
      struct user_data *tmp = *p;
      *p = tmp->next;
      free(tmp);
      return;
    }
    p = &((*p)->next);
  }
}

void broadcast_except(struct mg_connection *except, const char *buf, size_t len) {
  struct user_data *u = users_head;
  while (u) {
    if (u->nc != except) mg_send_websocket_frame(u->nc, WEBSOCKET_OP_TEXT, buf, len);
    u = u->next;
  }
}

// send users list (no document) to everyone (for UI display)
void broadcast_users_list() {
  char buf[4096];
  int off = 0;
  off += snprintf(buf + off, sizeof(buf) - off, "{\"type\":\"users\",\"list\":[");
  struct user_data *u = users_head;
  while (u) {
    off += snprintf(buf + off, sizeof(buf) - off,
                    "{\"id\":%d,\"color\":\"%s\",\"sel_start\":%d,\"sel_end\":%d}%s",
                    u->id, u->color, u->sel_start, u->sel_end,
                    u->next ? "," : "");
    u = u->next;
  }
  snprintf(buf + off, sizeof(buf) - off, "]}");

  u = users_head;
  while (u) {
    mg_send_websocket_frame(u->nc, WEBSOCKET_OP_TEXT, buf, strlen(buf));
    u = u->next;
  }
}

// Helper: safe substring copy for printing (not used for parsing)
static void safe_copy(char *dst, const char *src, size_t n) {
  if (n == 0) { dst[0] = '\0'; return; }
  memcpy(dst, src, n);
  dst[n] = '\0';
}

static void ev(struct mg_connection *nc, int ev, void *ev_data) {
  if (ev == MG_EV_HTTP_REQUEST) {
    struct mg_serve_http_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.document_root = "../client";
    mg_serve_http(nc, ev_data, opts);
  } else if (ev == MG_EV_WEBSOCKET_HANDSHAKE_DONE) {
    // allocate user node
    struct user_data *u = calloc(1, sizeof(*u));
    u->id = next_user_id++;
    snprintf(u->color, sizeof(u->color), "#%06X", rand() % 0xFFFFFF);
    u->sel_start = u->sel_end = 0;
    u->nc = nc;
    nc->user_data = u;
    add_user_node(u);

    // Send init message: metadata JSON + newline + full document
    char out[MAX_DOC + 128];
    int len = snprintf(out, sizeof(out),
                       "{\"type\":\"init\",\"id\":%d,\"color\":\"%s\"}\n%s",
                       u->id, u->color, document);
    mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, out, len);

    // broadcast user list
    broadcast_users_list();
    printf("User %d connected, color %s\n", u->id, u->color);
  } else if (ev == MG_EV_WEBSOCKET_FRAME) {
    // ev_data is struct mg_str* in mongoose 6.x; but here we cast to mg_str*
    struct mg_str *wm = (struct mg_str *) ev_data;
    if (!wm || wm->len == 0) return;

    // find first newline '\n' (metadata separator)
    void *p = memchr(wm->p, '\n', wm->len);
    if (!p) {
      // malformed frame: ignore
      return;
    }
    size_t meta_len = (char *)p - (char *)wm->p;
    size_t content_len = wm->len - meta_len - 1;
    // meta JSON is wm->p[0..meta_len-1]; content is wm->p[meta_len+1..]

    // copy meta safely
    char *meta = malloc(meta_len + 1);
    safe_copy(meta, wm->p, meta_len);

    // copy raw content
    size_t take = content_len < MAX_DOC - 1 ? content_len : MAX_DOC - 1;
    char *content = malloc(take + 1);
    memcpy(content, (char *)wm->p + meta_len + 1, take);
    content[take] = '\0';

    // update server-side document (overwrite)
    strncpy(document, content, MAX_DOC - 1);

    // update user selection info if meta includes selStart/selEnd (we'll parse simple integers)
    struct user_data *u = (struct user_data *) nc->user_data;
    if (u) {
      // try to extract selStart and selEnd from meta (simple parsing using strstr + atoi)
      char *s_pos = strstr(meta, "\"selStart\":");
      char *s_end = strstr(meta, "\"selEnd\":");
      if (s_pos) u->sel_start = atoi(s_pos + strlen("\"selStart\":"));
      if (s_end) u->sel_end = atoi(s_end + strlen("\"selEnd\":"));
    }

    // Recreate a frame to broadcast: use same two-line framing (meta + \n + content)
    // We will broadcast the exact bytes received so other clients see the same data.
    broadcast_except(nc, wm->p, wm->len);

    // broadcast updated users list
    broadcast_users_list();

    free(meta);
    free(content);
  } else if (ev == MG_EV_CLOSE) {
    if (nc->user_data) {
      struct user_data *u = (struct user_data *) nc->user_data;
      printf("User %d disconnected\n", u->id);
      remove_user_node(nc);
      free(u);
      nc->user_data = NULL;
      broadcast_users_list();
    }
  }
}

int main(void) {
  srand((unsigned) time(NULL));
  struct mg_mgr mgr;
  mg_mgr_init(&mgr, NULL);

  struct mg_connection *nc = mg_bind(&mgr, PORT, ev);
  if (!nc) {
    fprintf(stderr, "Failed to bind to port %s\n", PORT);
    return 1;
  }
  mg_set_protocol_http_websocket(nc);
  printf("Server running on http://localhost:%s\n", PORT);

  for (;;) mg_mgr_poll(&mgr, 1000);
  mg_mgr_free(&mgr);
  return 0;
}
