#include <http-ssl.h>
#include <http-connection.h>
#include <http-protocol.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include <netdb.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <string.h>
#include <stdio.h>

#include <unistd.h>
#include <pthread.h>

#include <signal.h>
#include <ctype.h>

#include <errno.h>

#define SOCKS5_IPv4_ADDRESS_TYPE 0x01
#define SOCKS5_DOMAIN_NAME_ADDRESS_TYPE 0x03
#define SOCKS5_IPv6_ADDRESS_TYPE 0x04

#define httpio_read_dt(link, data, size) httpio_read(link, data, size, DEFAULT_TIMEOUT)
struct httpio_proxy {
    const char *host;
    short int port;
};

struct httpio
{
    // The address we connected to!
    struct sockaddr_in address;
    // Socket for IO
    int socket;
    // Host to connect to
    char *host;
    // Service, to determine PORT
    char *service;
    // SSL context (it's abstracted from SSL_ctx
    struct httpio_ssl *ssl;
    // Error handler callback and data
    httpio_connection_error_handler error_handler;
    void *error_handler_data;
    // Websocket error handler callback and data
    httpio_websocket_onerror_handler websocket_onerror;
    void *websocket_on_error_data;
    // Websocket onclose handler and data
    httpio_websocket_onclose_handler websocket_onclose;
    void *websocket_on_close_data;
};

// Internal variables
static int httpio_get_address_list(struct sockaddr_in *list, int maximum,
                                 const char *const url, const char *service);
// Library initialization
static void httpio_initialize(void) __attribute__((constructor));
static void httpio_finalize(void) __attribute__((destructor));
// Global mutex to allow multithreading
static pthread_mutex_t GLOBAL_MUTEX = PTHREAD_MUTEX_INITIALIZER;

static void
httpio_initialize(void)
{
    httpio_ssl_initialize();
}

static void
httpio_finalize(void)
{
    httpio_ssl_finalize();
}

static int
httpio_get_address_list(struct sockaddr_in *list,
                        int maximum, const char *const url, const char *service)
{
    struct addrinfo hints;
    struct addrinfo *information;
    struct addrinfo *next;
    int result;
    int index;

    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    // This is mandatory because `getaddrinfo()' uses
    // getenv() which is not thread safe
    pthread_mutex_lock(&GLOBAL_MUTEX);
    // Resolve the IP
    result = getaddrinfo(url, service, &hints, &information);
    // Release the mutex
    pthread_mutex_unlock(&GLOBAL_MUTEX);
    if (result != 0)
        return -1;
    index = 0;
    for (next = information; ((next != NULL) && (index < maximum)); next = next->ai_next) {
        struct sockaddr_in *address;
        address = (struct sockaddr_in *) next->ai_addr;
        if (address == NULL)
            continue;
        memcpy(&list[index++], address, sizeof(*address));
    }
    freeaddrinfo(information);

    return index;
}

int
httpio_set_keep_alive(int sock)
{
    socklen_t length;
    int alive;
    length = sizeof(alive);
    alive = 0;
    if (getsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &alive, &length) == -1)
        return -1;
    if (alive != 0)
        return 0;
    alive = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &alive, length) == -1)
        return -1;
    return 0;
}

static int
httpio_create_socket(struct httpio *const link)
{
    struct sockaddr_in address_list[32];
    int count;
    int address_count;

    link->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (link->socket == -1)
        return -1;
    if (httpio_set_keep_alive(link->socket) == -1)
        goto error;
    count = countof(address_list);
    address_count = httpio_get_address_list(address_list,
                                              count, link->host, link->service);
    for (int index = 0; index < address_count; ++index) {
        struct sockaddr_in *address;
        socklen_t length;
        address = &address_list[index];
        length = sizeof(*address);
        if (connect(link->socket, (struct sockaddr *) address, length) != 0)
            continue;
        memcpy(&link->address, address, sizeof(*address));
        switch (htons(address->sin_port)) {
            case 993:
            case 6984:
            case 443:
                // Grab a lock to do this safely
                pthread_mutex_lock(&GLOBAL_MUTEX);
                // Make an SSL object to securely communicate
                link->ssl = httpio_ssl_create_with_socket(link->socket);
                // Release the lock
                pthread_mutex_unlock(&GLOBAL_MUTEX);
                if (link->ssl == NULL)
                    goto error;
                break;
            default:
                link->ssl = NULL;
                break;
        }
        return link->socket;
    }
error:
    if (link->socket != -1)
        close(link->socket);
    return -1;
}

struct httpio *
httpio_connect(const char *const host, const char *const service)
{
    struct httpio *link;

    link = malloc(sizeof(*link));
    if (link == NULL)
        return NULL;
    memset(&link->address, 0, sizeof(link->address));

    link->service = strdup(service);
    link->host = strdup(host);
    link->websocket_onclose = NULL;
    link->websocket_onerror = NULL;
    link->error_handler = NULL;
    // Create the socket and connect to it
    link->socket = httpio_create_socket(link);
    if (link->socket != -1)
        return link;
    free(link->service);
    free(link->host);
    free(link);

    return NULL;
}

void
httpio_disconnect(struct httpio *link)
{
    if (link == NULL)
        return;
    if (link->socket != -1) {
        shutdown(link->socket, SHUT_RDWR);
        close(link->socket);
    }
    httpio_ssl_free(link->ssl);

    free(link->service);
    free(link->host);
    free(link);
}


bool
httpio_wants_data(struct httpio *link, int64_t nanoseconds)
{
    if (link == NULL)
        return false;
    if (link->ssl == NULL)
        return httpio_socket_wants_data(link->socket, nanoseconds);
    return true;
}

bool
httpio_has_data(struct httpio *link, int64_t nanoseconds)
{
    if (link == NULL)
        return false;
    if (link->ssl == NULL)
        return httpio_socket_has_data(link->socket, nanoseconds);
    return httpio_ssl_has_data(link->ssl, nanoseconds);
}

#undef _DEBUG
#ifdef _DEBUG
#define printable(x) ((isprint((x)) != 0) || (isspace((x)) != 0))
static void
httpio_dump_buffer(const uint8_t *const data, ssize_t length)
{
    if (length == -1)
        return;
    for (ssize_t idx = 0; idx < length; ++idx) {
        unsigned char chr;
        chr = (unsigned char) data[idx];
        if (printable(chr) != 0) {
            fputc(chr, stderr);
        } else {
            fprintf(stderr, "\\%03X", chr);
        }
    }
}
#endif

ssize_t
httpio_write(struct httpio *link, const uint8_t *const data, size_t size)
{
    ssize_t result;
    if (size == 0)
        return 0;
    errno = 0;
    if ((link == NULL) || (data == NULL))
        return -1;
    if (httpio_wants_data(link, DEFAULT_TIMEOUT) == false)
        return -1;
    if (link->ssl == NULL) {
        result = send(link->socket, data, size, MSG_NOSIGNAL);
    } else {
        result = httpio_sendssl(link->ssl, data, size);
    }
    // What happened?
    if (result == 0) {
        // FIXME: this is done deliberately, no reason whatsoever
        //        this should be fixed ASAP.
        result = -1;
    } else if (errno != 0) {
        if (link->error_handler != NULL) {
            link->error_handler(link, errno, link->error_handler_data);
        }
    }
#ifdef _DEBUG
    fprintf(stderr, "\033[34m");
    httpio_dump_buffer(data, size);
    fprintf(stderr, "\033[0m");
#endif
    return result;
}

ssize_t
httpio_read(struct httpio *link, uint8_t *const data, int size, int64_t nanoseconds)
{
    ssize_t result;
    if (size == 0)
        return 0;
    errno = 0;
    if ((link == NULL) || (data == NULL))
        return -1;
    if (httpio_has_data(link, nanoseconds) == false)
        return -1;
    if (link->ssl == NULL) {
        result = recv(link->socket, data, size, MSG_NOSIGNAL);
    } else {
        result = httpio_recvssl(link->ssl, data, size, nanoseconds);
    }
    // What happened?
    if (result == 0) {
        // FIXME: this is done deliberately, no reason whatsoever
        //        this should be fixed ASAP.
        result = -1;
    } else if (errno != 0) {
        if (link->error_handler != NULL) {
            link->error_handler(link, errno, link->error_handler_data);
        }
    }
#ifdef _DEBUG
    httpio_dump_buffer(data, size);
#endif
    return result;
}

ssize_t
httpio_vwrite_line(struct httpio *link, const char *format, va_list args)
{
    char *buffer;
    ssize_t length;
    va_list copy;

    if (httpio_wants_data(link, DEFAULT_TIMEOUT) == false)
        return -1;
    va_copy(copy, args);
    length = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    buffer = malloc(length + 1);
    if (buffer != NULL) {
        vsnprintf(buffer, length + 1, format, args);
        length = httpio_write(link, (uint8_t *) buffer, length);
        free(buffer);
    }
    httpio_write(link, (uint8_t *) "\r\n", 2);

    return length;
}

ssize_t
httpio_write_newline(struct httpio *link)
{
    return httpio_write(link, (uint8_t *) "\r\n", 2);
}

ssize_t
httpio_write_line(struct httpio *link, const char *format, ...)
{
    ssize_t result;
    va_list args;

    va_start(args, format);
    result = httpio_vwrite_line(link, format, args);
    va_end(args);

    return result;
}

struct httpio *
httpio_connection_open_socks5(const char *const host,
                const char *const service, const struct httpio_proxy *const proxy)
{
    uint8_t response[1024];
    struct httpio *link;
    // Static variables, they are REALLY CONST
    static const uint8_t command[] = {0x05, 0x01, 0x00, SOCKS5_DOMAIN_NAME_ADDRESS_TYPE};
    static const uint8_t version[] = {0x05, 0x01, 0x00};
    // Other parameters and variables
    short int connection_port;
    ssize_t result;
    ssize_t length;
    char proxy_port[16];
    // Make the proxy port string to pass to httpio_connect
    result = snprintf(proxy_port, sizeof(proxy_port), "%d", proxy->port);
    if ((result >= (ssize_t) sizeof(proxy_port)) || (result < 0))
        return NULL;
    // Check which HTTP port to use
    if (strcmp(service, "http") == 0) {
        connection_port = htons(80);
    } else if (strcmp(service, "https") == 0) {
        connection_port = htons(443);
    } else {// This means, that we don't know what to do with this request
        return NULL;
    }
    // Store the host length, and check that it fits 1 Byte
    length = strlen(host);
    if (length > 0xFFU)
        return NULL;    
    // Connect to the proxy
    link = httpio_connect(proxy->host, proxy_port);
    if (link == NULL)
        return NULL;
    // Send the version information
    httpio_write(link, version, 3);
    // Get the response and check it's validity
    if (httpio_read_dt(link, response, 2) == 2) {
        // It must be the same version we sent
        if (response[0] != 0x05)
            goto failed;
        // It should not request any authentication
        // because it's not currently supported
        if (response[1] != 0x00)
            goto failed;
    }
    // Send the command (a connect request)
    httpio_write(link, command, 4);
    httpio_write(link, (uint8_t *) &length, 1);
    httpio_write(link, (uint8_t *) host, length);
    httpio_write(link, (uint8_t *) &connection_port, 2);
    // Check the proxy's response
    if (httpio_read_dt(link, response, 4) == 4) {
        // This must be the same SOCKS version we're using
        if (response[0] != 0x05)
            goto failed;
        // This is the status code
        switch (response[1]) {
        case 0x00:
            break;
        case 0x01:
            fprintf(stderr, "socks5: General server failure\n");
            goto failed;
        case 0x02:
            fprintf(stderr, "socks5: Connection not allowed\n");
            goto failed;
        case 0x03:
            fprintf(stderr, "socks5: Network unreachable\n");
            goto failed;
        case 0x04:
            fprintf(stderr, "socks5: Host unreacahble\n");
            goto failed;
        case 0x05:
            fprintf(stderr, "socks5: Connection refused\n");
            goto failed;
        case 0x06:
            fprintf(stderr, "socks5: TTL expired\n");
            goto failed;
        case 0x07:
            fprintf(stderr, "socks5: Command not supported\n");
            goto failed;
        case 0x08:
            fprintf(stderr, "socks5: Address type not supported\n");
            goto failed;
        }
        // The address type (response[2]) is reserved
        switch (response[3]) {
        case SOCKS5_IPv4_ADDRESS_TYPE:
            // Read the IPv4 address from the connection
            if (httpio_read_dt(link, response, 4) != 4)
                goto failed;
            break;
        case SOCKS5_DOMAIN_NAME_ADDRESS_TYPE:
            // First read the length of the address
            if (httpio_read_dt(link, response, 1) != 1)
                goto failed;
            // Store the length somewhere
            length = response[0];
            // How read the domain name with length `length'
            if (httpio_read_dt(link, response, length) != length)
               goto failed;
            break;
        case SOCKS5_IPv6_ADDRESS_TYPE:
            // Read the IPv6 address from the connection
            if (httpio_read_dt(link, response, 16) != 16)
                goto failed;
            break;
        default: // This should not be reached if everything is good
            goto failed;
        }
        // Read the port
        if (httpio_read_dt(link, response, 2) != 2)
            goto failed;
    }
    // The `host' should be the real one
    free(link->host);
    // Copy the real host
    link->host = strdup(host);
    // From now, it's a regular `httpio' object
    return link;
failed:
    // On failure, you have to close the connection
    httpio_disconnect(link);
    return NULL;
}

const char *
httpio_host(struct httpio *link)
{
    if (link == NULL)
        return NULL;
    return link->host;
}

int
httpio_connection_reconnect(struct httpio *link)
{
    if (link == NULL)
        return -1;
    return (httpio_create_socket(link) != -1);
}

void httpio_set_error_handler(struct httpio *const link,
                             httpio_connection_error_handler handler, void *data)
{
    link->error_handler = handler;
    link->error_handler_data = data;
}

void
httpio_connection_set_websocket_onclose_handler(
                             struct httpio *const websocket,
                             httpio_websocket_onclose_handler handler, void *data)
{
    websocket->websocket_onclose = handler;
    websocket->websocket_on_close_data = data;
}

void
httpio_connection_set_websocket_onerror_handler(
                             struct httpio *const websocket,
                             httpio_websocket_onerror_handler handler, void *data)
{
    websocket->websocket_onerror = handler;
    websocket->websocket_on_error_data = data;
}

const struct httpio_proxy *
httpio_socks5_tor_proxy()
{
    static const struct httpio_proxy proxy = {"127.0.0.1", 9050};
    return &proxy;
}
