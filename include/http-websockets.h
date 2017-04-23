#ifndef __httpio_websocketS_H__
#define __httpio_websocketS_H__

#include <http-connection.h>

#ifdef __cplusplus
extern "C" {
#endif

enum httpio_websocket_frame_type
{
    WebSocketContinuationFrame = 0x00,
    WebSocketTextFrame = 0x01,
    WebSocketBinaryFrame = 0x02,
    WebSocketConnectionCloseFrame = 0x08,
    WebSocketPingFrame = 0x09,
    WebSocketPongFrame = 0x0A,
    WebSocketInvalidFrame
};
struct httpio_websocket_frame;

char *httpio_websocket_key_accept(const char *const source);
char *httpio_websocket_secret();
struct httpio_websocket_frame *httpio_websocket_get_frame(struct httpio *link);
void httpio_websocket_frame_free(struct httpio_websocket_frame *frame);
enum httpio_websocket_frame_type httpio_websocket_frame_type(struct httpio_websocket_frame *frame);
uint8_t *httpio_websocket_frame_data(const struct httpio_websocket_frame *const frame);
size_t httpio_websocket_frame_length(const struct httpio_websocket_frame *const frame);
bool httpio_websocket_send_string(struct httpio *link, const char *const unmasked);
bool httpio_websocket_check_key(const char *const key, const char *const secret);

void httpio_websocket_set_onclose_handler(struct httpio *const websocket, httpio_websocket_onclose_handler handler, void *data);
void httpio_websocket_set_onerror_handler(struct httpio *const websocket, httpio_websocket_onerror_handler handler, void *data);

void httpio_websocket_dump_frame(struct httpio_websocket_frame *frame);

#ifdef __cplusplus
}
#endif

#endif /* __httpio_websocketS_H__ */
