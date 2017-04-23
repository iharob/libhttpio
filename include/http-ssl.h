#ifndef __HTTP_SSL_H__
#define __HTTP_SSL_H__

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

struct httpio_ssl;

void httpio_ssl_initialize();
void httpio_ssl_finalize();

struct httpio_ssl *httpio_ssl_create_with_socket(int sock);
bool httpio_ssl_has_data(struct httpio_ssl *ssl, int64_t nanoseconds);
void httpio_ssl_free(struct httpio_ssl *ssl);

ssize_t httpio_recvssl(struct httpio_ssl *ssl, uint8_t *const buffer, size_t size, int64_t nanoseconds);
ssize_t httpio_sendssl(struct httpio_ssl *ssl, const uint8_t *const buffer, size_t size);
#endif // __HTTP_SSL_H__
