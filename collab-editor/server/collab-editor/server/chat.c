// Standalone chat server using Mongoose (no changes to existing code)
// Serves static chat UI and provides a WebSocket endpoint that broadcasts
// messages to all connected clients.

#include "mongoose.h"

static const char *s_listen_addr = "http://0.0.0.0:9091";   // Chat server port
static const char *s_web_root    = "../client/chat";        // Serve chat UI

static void handle(struct mg_connection *c, int ev, void *ev_data, void *fn_data) {
  if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *) ev_data;
    if (mg_http_match_uri(hm, "/ws")) {
      mg_ws_upgrade(c, hm, NULL);  // Upgrade to WebSocket
    } else {
      // Serve static chat UI
      struct mg_http_serve_opts opts = {.root = s_web_root};
      mg_http_serve_dir(c, hm, &opts);
    }
  } else if (ev == MG_EV_WS_OPEN) {
    // Optionally announce join
    const char *hello = "{\"type\":\"system\",\"text\":\"A user joined the chat\"}";
    for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
      if (t->is_websocket) mg_ws_send(t, hello, strlen(hello), WEBSOCKET_OP_TEXT);
    }
  } else if (ev == MG_EV_WS_MSG) {
    // Broadcast incoming message to all connected WebSocket clients
    struct mg_ws_message *wm = (struct mg_ws_message *) ev_data;
    for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
      if (t->is_websocket) mg_ws_send(t, wm->data.ptr, wm->data.len, WEBSOCKET_OP_TEXT);
    }
  } else if (ev == MG_EV_CLOSE && c->is_websocket) {
    // Optionally announce leave
    const char *bye = "{\"type\":\"system\",\"text\":\"A user left the chat\"}";
    for (struct mg_connection *t = c->mgr->conns; t != NULL; t = t->next) {
      if (t->is_websocket) mg_ws_send(t, bye, strlen(bye), WEBSOCKET_OP_TEXT);
    }
  }
  (void) fn_data;
}

int main(void) {
  struct mg_mgr mgr;
  mg_mgr_init(&mgr);
  mg_log_set("2");  // Info level
  if (mg_http_listen(&mgr, s_listen_addr, handle, NULL) == NULL) {
    MG_ERROR(("Cannot listen on %s", s_listen_addr));
    return 1;
  }
  MG_INFO(("Chat server listening on %s, serving %s", s_listen_addr, s_web_root));
  for (;;) mg_mgr_poll(&mgr, 100);
  mg_mgr_free(&mgr);
  return 0;
}
