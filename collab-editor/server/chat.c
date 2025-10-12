#include "mongoose.h"
#include <string.h>
#include <stdio.h>

static const char *s_web_root = "./chat";  // Serve static files from collab-editor/server/chat

static void ev_handler(struct mg_connection *c, int ev, void *ev_data) {
  switch (ev) {
    case MG_EV_HTTP_REQUEST: {
      // Serve static files (index.html, etc.)
      struct http_message *hm = (struct http_message *) ev_data;
      struct mg_serve_http_opts opts;
      memset(&opts, 0, sizeof(opts));
      opts.document_root = s_web_root;
      opts.enable_directory_listing = "no";
      mg_serve_http(c, hm, opts);
      break;
    }

    case MG_EV_WEBSOCKET_HANDSHAKE_DONE: {
      // New WS client connected: notify others
      const char *hello = "hello";
      struct mg_connection *nc;
      for (nc = c->mgr->active_connections; nc != NULL; nc = nc->next) {
        if (nc != c && (nc->flags & MG_F_IS_WEBSOCKET)) {
          mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, hello, (int) strlen(hello));
        }
      }
      break;
    }

    case MG_EV_WEBSOCKET_FRAME: {
  struct websocket_message *wm = (struct websocket_message *) ev_data;
  struct mg_connection *nc;
  for (nc = c->mgr->active_connections; nc != NULL; nc = nc->next) {
    if (nc->flags & MG_F_IS_WEBSOCKET) {
      mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, wm->data, (int) wm->size);
    }
  }
  break;
}
    case MG_EV_CLOSE: {
      // Notify others when a WS client disconnects
      if (c->flags & MG_F_IS_WEBSOCKET) {
        const char *bye = "bye";
        struct mg_connection *nc;
        for (nc = c->mgr->active_connections; nc != NULL; nc = nc->next) {
          if (nc != c && (nc->flags & MG_F_IS_WEBSOCKET)) {
            mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, bye, (int) strlen(bye));
          }
        }
      }
      break;
    }
  }
}

int main(void) {
  struct mg_mgr mgr;
  struct mg_connection *listener;

  mg_mgr_init(&mgr, NULL);
  printf("Starting chat server on http://localhost:9091\n");

  listener = mg_bind(&mgr, "9091", ev_handler);
  if (listener == NULL) {
    fprintf(stderr, "Failed to bind to port 9091\n");
    return 1;
  }

  // Enable HTTP + WebSocket protocol handling (WS handshake is automatic in v6)
  mg_set_protocol_http_websocket(listener);

  for (;;) {
    mg_mgr_poll(&mgr, 1000);
  }

  mg_mgr_free(&mgr);
  return 0;
}
