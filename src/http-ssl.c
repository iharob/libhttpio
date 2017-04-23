#include <openssl/ssl.h>
#include <openssl/err.h>

#include <http-util.h>
#include <http-ssl.h>

#include <pthread.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

struct httpio_ssl {
    SSL *ssl;
};

static SSL *
httpio_create_openssl_object(int socket)
{
    SSL_CTX *context;
    SSL *ssl;

    ssl = NULL;
    context = SSL_CTX_new(SSLv23_client_method());
    if (context == NULL)
        goto failed;
    ssl = SSL_new(context);
    if (ssl == NULL)
        goto failed;
    if (SSL_set_fd(ssl, socket) == 0)
        goto failed;
    SSL_set_connect_state(ssl);
    if (SSL_do_handshake(ssl) <= 0)
        goto failed;
    return ssl;

failed:
    ERR_print_errors_fp(stderr);
    if (ssl == NULL)
        return NULL;
    SSL_shutdown(ssl);
    SSL_free(ssl);

    return NULL;
}

static void
httpio_openssl_free(SSL *ssl)
{
    SSL_CTX *ctx;
    if (ssl != NULL) {
        ctx = SSL_get_SSL_CTX(ssl);
        SSL_shutdown(ssl);
        SSL_free(ssl);

        // ERR_remove_thread_state(0);
        if (ctx == NULL)
            return;
        SSL_CTX_free(ctx);
    }
}

void
httpio_openssl_finalize()
{
    SSL_COMP_free_compression_methods();
    FIPS_mode_set(0);

    ERR_free_strings();
    EVP_cleanup();

    COMP_zlib_cleanup();
    CRYPTO_cleanup_all_ex_data();
}

void
httpio_openssl_initialize()
{
    SSL_library_init();
}

static ssize_t
httpio_sendopenssl(SSL *ssl, const uint8_t *const buffer, size_t size)
{
    ssize_t result;
    ssize_t sent;
    if (ssl == NULL)
        return -1;
    sent = 0;
    while (sent < (ssize_t) size) {
        result = SSL_write(ssl, buffer, size);
        if (result < 0) {
            int error;
            error = SSL_get_error(ssl, result);
            switch (error) {
            case SSL_ERROR_NONE:
                return sent;
            case SSL_ERROR_WANT_WRITE:
            case SSL_ERROR_WANT_READ:
                break;
            default:
                return -1;
            }
        }
        else if (result > 0)
            sent += result;
        else if (result == 0)
            return -1;
    }
    return sent;
}

bool
httpio_openssl_has_data(SSL *ssl, int64_t nanoseconds)
{
    if (SSL_pending(ssl) > 0)
        return true;
    return httpio_socket_has_data(SSL_get_fd(ssl), nanoseconds);
}

static ssize_t
httpio_recvopenssl(SSL *ssl, uint8_t *buffer, size_t size, int64_t nanoseconds)
{
    ssize_t result;
    ssize_t received;
    received = 0;
    while ((received < (ssize_t) size) &&
                            (httpio_openssl_has_data(ssl, nanoseconds) == true)) {
        result = SSL_read(ssl, buffer, size - received);
        if (result <= 0) {
            int error;
            error = SSL_get_error(ssl, result);
            switch (error) {
            case SSL_ERROR_NONE:
                return received;
            case SSL_ERROR_WANT_WRITE:
            case SSL_ERROR_WANT_READ:
                break;
            case SSL_ERROR_ZERO_RETURN:
                fprintf(stderr, "connection shutdown\n");
            default:
                return -1;
            }
        } else if (result > 0) {
            received += result;
            buffer += result;
        }
    }
    return received;
}

struct httpio_ssl *
httpio_ssl_create_with_socket(int sock)
{
    struct httpio_ssl *ssl;
    ssl = malloc(sizeof(*ssl));
    if (ssl == NULL)
        return NULL;
    ssl->ssl = httpio_create_openssl_object(sock);
    return ssl;
}

bool
httpio_ssl_has_data(struct httpio_ssl *ssl, int64_t nanoseconds)
{
    return httpio_openssl_has_data(ssl->ssl, nanoseconds);
}

ssize_t
httpio_sendssl(struct httpio_ssl *ssl, const uint8_t *const buffer, size_t size)
{
    return httpio_sendopenssl(ssl->ssl, buffer, size);
}

ssize_t
httpio_recvssl(struct httpio_ssl *ssl,
                        uint8_t *const buffer, size_t size, int64_t nanoseconds)
{
    return httpio_recvopenssl(ssl->ssl, buffer, size, nanoseconds);
}

void
httpio_ssl_finalize()
{
    httpio_openssl_finalize();
}

void
httpio_ssl_initialize()
{
    httpio_openssl_initialize();
}

void
httpio_ssl_free(struct httpio_ssl *ssl)
{
    if (ssl == NULL)
        return;
    httpio_openssl_free(ssl->ssl);
    free(ssl);
}
