#include "mongoose.h"

static const char *s_web_root = "./chat"; // Serve files from this directory

static void ev_handler(struct mg_connection *c, int ev, void *ev_data) {
  struct mg_connection *nc;
  struct http_message *hm;
  struct websocket_message *wm;

  switch (ev) {
    case MG_EV_HTTP_REQUEST:
      hm = (struct http_message *)ev_data;
      // Serve static files and handle /ws websocket upgrade
      if (mg_vcmp(&hm->uri, "/ws") == 0) {
        // Upgrade to WebSocket
        mg_ws_upgrade(c, hm, NULL);
      } else {
        mg_serve_http(c, hm, s_web_root);
      }
      break;

    case MG_EV_WEBSOCKET_HANDSHAKE_DONE:
      // Broadcast "hello" to all connected websockets
      for (nc = c->mgr->active_connections; nc != NULL; nc = nc->next) {
        if (nc != c && (nc->flags & MG_F_IS_WEBSOCKET)) {
          mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, "hello", 5);
        }
      }
      break;

    case MG_EV_WEBSOCKET_FRAME:
      wm = (struct websocket_message *)ev_data;
      // Broadcast received message to all websockets
      for (nc = c->mgr->active_connections; nc != NULL; nc = nc->next) {
        if (nc->flags & MG_F_IS_WEBSOCKET) {
          mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, wm->data, wm->size);
        }
      }
      break;

    case MG_EV_CLOSE:
      if (c->flags & MG_F_IS_WEBSOCKET) {
        // Notify others that someone left
        for (nc = c->mgr->active_connections; nc != NULL; nc = nc->next) {
          if (nc != c && (nc->flags & MG_F_IS_WEBSOCKET)) {
            mg_send_websocket_frame(nc, WEBSOCKET_OP_TEXT, "bye", 3);
          }
        }
      }
      break;
  }
}

int main(void) {
  struct mg_mgr mgr;
  struct mg_connection *c;

  mg_mgr_init(&mgr, NULL);

  printf("Starting chat server on http://localhost:9091\n");
  c = mg_bind(&mgr, "9091", ev_handler);
  mg_set_protocol_http_websocket(c);

  for (;;) {
    mg_mgr_poll(&mgr, 1000);
  }

  mg_mgr_free(&mgr);
  return 0;
}
