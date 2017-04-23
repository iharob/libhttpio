#include <http-websockets.h>

#include <http-util.h>

#include <openssl/sha.h>

#include <ctype.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <errno.h>

#define SecretSource "abcdefghijklmnopqrstuvwxyz0123456789"
#define WebSocketMagic "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WebSocketMagicLength (sizeof(WebSocketMagic) - 1)

struct httpio_websocket_frame
{
    uint8_t *data;
    enum httpio_websocket_frame_type type;
    int64_t length;
};

bool
httpio_websocket_send_string(struct httpio *link, const char *const message)
{
    ssize_t length;
    uint8_t header[2];
    int32_t mask;
    uint8_t *masked;
    int result;

    mask = httpio_safe_random();
    if ((link == NULL) || (message == NULL))
        return false;
    length = strlen(message);
    if (length == 0)
        return false;
    header[0] = 0x81;
    if (length < 126) {
        header[1] = (uint8_t) (length & 0xFF);
    } else if (length < 127) {
        header[1] = 126;
    } else {
        header[1] = 127;
    }
    header[1] |= 0x80;
    if (httpio_write(link, header, sizeof(header)) != sizeof(header))
        return false;
    if (httpio_write(link, (uint8_t *) &mask, sizeof(mask)) != sizeof(mask))
        return false;
    masked = malloc(length);
    for (size_t index = 0; message[index] != '\0'; ++index)
        masked[index] = message[index] ^ ((uint8_t *) &mask)[index % 4];
    result = httpio_write(link, masked, length);

    free(masked);
    if (result < 0)
        return false;
    return true;
}

void
httpio_websocket_dump_frame(struct httpio_websocket_frame *frame)
{
    if (frame == NULL)
        return;
    fprintf(stderr, "WebSocket Frame: ");
    for (ssize_t i = 0; i < frame->length; ++i)
    {
        if (isprint((unsigned char) frame->data[i]) != 0)
            fputc(frame->data[i], stderr);
        else if (isspace((unsigned char) frame->data[i]) != 0)
            fputc(frame->data[i], stderr);
        else
            fprintf(stderr, "\\%03x", frame->data[i]);
    }
    fputc('\n', stderr);
}

struct httpio_websocket_frame *
httpio_websocket_get_frame(struct httpio *link)
{
    struct httpio_websocket_frame *frame;
    struct httpio_bstream stream;
    uint8_t zero;
    int8_t final;
    int8_t type;

    httpio_byte_stream_start(&stream);

    frame = malloc(sizeof(*frame));
    if (frame == NULL)
        return NULL;
    memset(frame, 0, sizeof(*frame));

    zero = 0;
    do {
        int64_t length;
        uint8_t buffer[4];
        int8_t masked;
        if (httpio_read(link, buffer, 2, DEFAULT_TIMEOUT) <= 0)
            goto error;
        final = ((buffer[0] & 0x80) == 0x80);
        type = buffer[0] & 0x0F;
        masked = ((buffer[1] & 0x80) == 0x80);
        length = (buffer[1] & 0x7F);
        switch (length) {
        case 126:
            if (httpio_read(link, buffer, 2, DEFAULT_TIMEOUT) <= 0)
                goto error;
            length = (buffer[0] << 8) | buffer[1];
            break;
        case 127:
            if (httpio_read(link, buffer, 4, DEFAULT_TIMEOUT) <= 0)
                goto error;
            length = buffer[3];
            length |= (buffer[1] << 16);
            length |= (buffer[2] << 8);
            length |= (buffer[0] << 24);
            break;
        }

        if (masked == 0) {
            uint8_t buffer[0x4000];
            ssize_t received;
            size_t expect;

            received = 0;
            while (received < length) {
                ssize_t result;

                expect = length - received;
                if (expect > sizeof(buffer))
                    expect = sizeof(buffer);
                result = httpio_read(link, buffer,
                                                       expect, DEFAULT_TIMEOUT);
                if (result <= 0)
                    goto error;
                httpio_byte_stream_append(&stream, buffer, result);

                received += result;
            }
        } else {
            goto error;
        }
    } while (final == 0);
    if (stream.length == 0)
        goto error;
    httpio_byte_stream_append(&stream, (uint8_t *) &zero, 1);

    frame->type = type;
    frame->length = stream.length - 1;
    frame->data = stream.data;

    return frame;
error:
    httpio_byte_stream_free(&stream);
    free(frame);
    return NULL;
}

void
httpio_websocket_frame_free(struct httpio_websocket_frame *frame)
{
    if (frame == NULL)
        return;
    free(frame->data);
    free(frame);
}

char *
httpio_websocket_secret()
{
    char secret[] = SecretSource;
    size_t length;

    length = 16;
    for (size_t index = 0; index < length; ++index)
    {
        int position;
        char stored;

        position = httpio_safe_random() % (sizeof(SecretSource) - 1);
        stored = secret[index];
        secret[index] = secret[position];
        secret[position] = stored;
    }
    secret[length] = '\0';

    return (char *) httpio_base64_encode((uint8_t *) secret, length);
}

char *
httpio_websocket_key_accept(const char *const source)
{
    uint8_t *string;
    uint8_t *sha;
    size_t length;
    char *result;
    sha = malloc(SHA_DIGEST_LENGTH);
    if (sha == NULL)
        return NULL;
    length = strlen(source);
    string = malloc(1 + length + WebSocketMagicLength);
    if (string == NULL)
        goto failed;
    memcpy(string, source, length);
    memcpy(string + length, WebSocketMagic, 1 + WebSocketMagicLength);
    SHA1(string, length + WebSocketMagicLength, sha);
    free(string);
    result = httpio_base64_encode(sha, SHA_DIGEST_LENGTH);
    free(sha);
    return result;
failed:
    free(string);
    free(sha);

    return NULL;
}

bool
httpio_websocket_check_key(const char *const key, const char *const secret)
{
    char *accept;
    int result;

    accept = httpio_websocket_key_accept(secret);
    if (accept == NULL)
        return 0;
    result = (strcmp(accept, key) == 0);

    free(accept);

    return result;
}

enum httpio_websocket_frame_type
httpio_websocket_frame_type(struct httpio_websocket_frame *frame)
{
    if (frame == NULL)
        return WebSocketInvalidFrame;
    return frame->type;
}

uint8_t *
httpio_websocket_frame_data(const struct httpio_websocket_frame *const frame)
{
    if (frame == NULL)
        return NULL;
    return frame->data;
}


size_t
httpio_websocket_frame_length(const struct httpio_websocket_frame *const frame)
{
    if (frame == NULL)
        return 0;
    return frame->length;
}

void
httpio_websocket_set_onclose_handler(struct httpio *const websocket,
                             httpio_websocket_onclose_handler handler, void *data)
{
    httpio_connection_set_websocket_onclose_handler(websocket, handler, data);
}

void
httpio_websocket_set_onerror_handler(struct httpio *const websocket,
                             httpio_websocket_onerror_handler handler, void *data)
{
    httpio_connection_set_websocket_onerror_handler(websocket, handler, data);
}
