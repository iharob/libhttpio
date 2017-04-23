#ifndef __HTTP_CONNECTION_H__
#define __HTTP_CONNECTION_H__

#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>

#include <http-util.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct httpio httpio;
typedef struct httpio_proxy httpio_proxy;
/* Normal Connection Handler: advanced error handling */
typedef int (*httpio_connection_error_handler)(struct httpio *const,int,void *);
/* WebSocket handlers */
typedef int (*httpio_websocket_onerror_handler)(struct httpio *const,int,void *);
typedef int (*httpio_websocket_onclose_handler)(struct httpio *const,int,void *);

bool httpio_has_data(struct httpio *link, int64_t nanoseconds);
bool httpio_wants_data(struct httpio *link, int64_t nanoseconds);
struct httpio *httpio_connect(const char *const host, const char *const service);
void httpio_disconnect(struct httpio *link);
ssize_t httpio_get_chunk(struct httpio *link, uint8_t *buffer, int size, int64_t nanoseconds);
ssize_t httpio_write(struct httpio *link, const uint8_t *const data, size_t size);
ssize_t httpio_read(struct httpio *link, uint8_t *const buffer, int size, int64_t nanoseconds);
ssize_t httpio_write_line(struct httpio *link, const char *format, ...)  __attribute__((format(printf, 2, 3)));
ssize_t httpio_vwrite_line(struct httpio *link, const char *format, va_list args);
ssize_t httpio_write_newline(struct httpio *link);

const char *httpio_host(struct httpio *link);
int httpio_connection_reconnect(struct httpio *link);

void httpio_set_error_handler(struct httpio *const link, httpio_connection_error_handler handler, void *data);
void httpio_connection_set_websocket_onclose_handler(struct httpio *const websocket, httpio_websocket_onclose_handler handler, void *data);
void httpio_connection_set_websocket_onerror_handler(struct httpio *const websocket, httpio_websocket_onerror_handler handler, void *data);
struct httpio *httpio_connection_open_socks5(const char *const host, const char * const service, const struct httpio_proxy *const proxy);
const struct httpio_proxy *httpio_socks5_tor_proxy(void);

#ifdef __cplusplus
}
#endif

#endif /* __HTTP_CONNECTION_H__ */
