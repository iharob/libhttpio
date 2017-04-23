#include <http-protocol.h>

#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <ctype.h>

#include <errno.h>
#include <zlib.h>

struct httpio_header
{
    char *key;
    char *value;
};

struct httpio_header_list
{
    struct httpio_header **headers;
    size_t count;
};

typedef struct httpio_status
{
    char *protocol;
    int value;
    char *message;
} httpio_status;

struct httpio_body
{
    uint8_t *data;
    size_t length;
};

struct httpio_response
{
    struct httpio_status *code;
    struct httpio_header_list *headers;
    struct httpio_body *body;
};

typedef struct httpio_content
{
    const char *type;
    const char *encoding;
    ssize_t length;
} httpio_content;

static httpio_body *httpio_response_read_chunked_transfer_encoding(httpio_content *content, httpio *link);
static httpio_body *httpio_response_read_content_length(httpio_content *content, httpio *link);
static ssize_t httpio_content_length(const char *const source);
static int httpio_header_list_append(httpio_header_list *list, httpio_header *header);
static httpio_header *httpio_response_create_header(const char *const key, const char *const value);
static int httpio_headers_compare_key(const void *const _a, const void *const _b);
static httpio_header_list *httpio_response_parse_headers(char *source);
static httpio_body *httpio_get_response_body(httpio_header_list *list, httpio *link);
static httpio_body *httpio_response_body_create(const httpio_content * const content, uint8_t *body, size_t length);
static httpio_header_list *httpio_get_response_headers(httpio *link);
static httpio_status *httpio_get_response_code(httpio *link);
static void httpio_response_headers_free(httpio_header_list *list);
static void httpio_response_code_free(httpio_status *code);
static void httpio_response_body_free(httpio_body *body);
static uint8_t *httpio_response_body_gunzip(uint8_t *data, size_t *length);

enum httpio_readline_state {
    DoneReadingLine,
    ExpectingFormFeed,
    ExpectingNewLine
};

static int
httpio_header_list_append(httpio_header_list *list, httpio_header *header)
{
    httpio_header **pointer;

    pointer = realloc(list->headers, (list->count + 1) * sizeof(*pointer));
    if (pointer == NULL)
        return -1;
    list->headers = pointer;
    list->headers[list->count] = header;
    list->count += 1;
    return 0;
}

static httpio_header *
httpio_response_create_header(const char *const key, const char *const value)
{
    httpio_header *header;

    header = malloc(sizeof(*header));
    if (header == NULL)
        return NULL;
    header->key = httpio_stripdup(key);
    header->value = httpio_stripdup(value);

    return header;
}

static int
httpio_headers_compare_key(const void *const _a, const void *const _b)
{
    httpio_header *a;
    httpio_header *b;

    a = *(httpio_header **) _a;
    b = *(httpio_header **) _b;

    return strcasecmp(a->key, b->key);
}

static httpio_header_list *
httpio_response_parse_headers(char *source)
{
    httpio_header_list *list;
    char *tail;
    char *head;
    head = source;
    list = malloc(sizeof(*list));
    if (list == NULL)
        return NULL;
    memset(list, 0, sizeof(*list));
    while ((head != NULL) && (tail = strchr(head, ':')) != NULL)
    {
        httpio_header *header;
        const char *key;
        const char *value;

        *tail = '\0';

        key = head;
        value = tail + 1;
        if (((tail = strstr(value, "\r\n")) != NULL) && ((*tail = '\0') == '\0'))
            head = tail + 1;
        else if ((tail = strchr(value, '\0')) != NULL)
            head = NULL;
        header = httpio_response_create_header(key, value);
        if (header == NULL)
            continue;
        if (httpio_header_list_append(list, header) == -1)
            free(header);
    }
    qsort(list->headers, list->count, sizeof(httpio_header *), httpio_headers_compare_key);
    return list;
}

static void
httpio_response_code_free(httpio_status *code)
{
    if (code == NULL)
        return;
    if (code->message != NULL)
        free(code->message);
    if (code->protocol != NULL)
        free(code->protocol);
    free(code);
}

static void
httpio_response_body_free(httpio_body *body)
{
    if (body == NULL)
        return;
    if (body->data != NULL)
        free(body->data);
    free(body);
}

static void
httpio_response_headers_free(httpio_header_list *list)
{
    httpio_header **headers;
    if (list == NULL)
        return;
    headers = list->headers;
    if (headers == NULL)
        return;
    for (size_t index = 0; index < list->count; ++index)
    {
        free(headers[index]->value);
        free(headers[index]->key);
        free(headers[index]);
    }
    free(headers);
    free(list);
}

httpio_status *
httpio_response_parse_response_code(char *const data)
{
    httpio_status *code;

    char *head;
    int next;
    char *tail;

    if (data == NULL)
        return NULL;
    code = malloc(sizeof(*code));
    if (code == NULL)
        return NULL;
    memset(code, 0, sizeof(*code));

    next = 0;
    head = data;
    tail = head;
    while (*tail != '\0')
    {
        char *endptr;
        char last;

        tail = strchr(head, ' ');
        if (tail == NULL)
            tail = strchr(head, '\0');
        last = *tail;

        *tail = '\0';
        switch (next)
        {
        case 0:
            code->protocol = httpio_stripdup(head);
            break;
        case 1:
            code->value = (enum httpio_code) strtol(head, &endptr, 10);
            if ((*endptr != '\0') && (*endptr != '\r'))
                code->value = HTTP_INVALID_CODE;
            break;
        case 2:
            code->message = httpio_stripdup(head);
            break;
        }
        *tail = last;

        head = tail + 1;
        next += 1;
    }
    return code;
}

static char *
httpio_connection_readline(httpio *link)
{
    httpio_bstream buffer;
    uint8_t value;
    enum httpio_readline_state state;

    state = ExpectingFormFeed;

    httpio_byte_stream_start(&buffer);
    while (state != DoneReadingLine) {
        if (httpio_read(link, &value, 1, DEFAULT_TIMEOUT) != 1)
            goto invalid;
        switch (value) {
        case '\r':
            if (state == ExpectingFormFeed)
                state = ExpectingNewLine;
            else
                goto invalid;
            break;
        case '\n':
            if (state == ExpectingNewLine)
                state = DoneReadingLine;
            else
                state = ExpectingFormFeed;
            break;
        default:
            httpio_byte_stream_append(&buffer, (uint8_t *) &value, 1);
            break;
        }
    }
    httpio_byte_stream_append(&buffer, (const uint8_t[]) {0}, 1);

    return (char *) buffer.data;

invalid:
    httpio_byte_stream_free(&buffer);
    return NULL;
}

static httpio_status *
httpio_get_response_code(httpio *link)
{
    httpio_status *code;
    char *line;
    line = httpio_connection_readline(link);
    if (line == NULL)
        return NULL;
    code = httpio_response_parse_response_code(line);
    free(line);

    return code;
}

static httpio_header_list *
httpio_get_response_headers(httpio *link)
{
    httpio_header_list *list;
    uint8_t value;
    httpio_bstream buffer;
    bool done;
    uint8_t zero;

    done = false;
    zero = 0;

    httpio_byte_stream_start(&buffer);
    while ((done == false) &&
                   (httpio_read(link, &value, 1, DEFAULT_TIMEOUT) > 0))
    {
        httpio_byte_stream_append(&buffer, (uint8_t *) &value, 1);
        if (value != '\n')
            continue;
        done = httpio_byte_stream_ends_with(&buffer, "\r\n\r\n", 4);
    }
    httpio_byte_stream_append(&buffer, (uint8_t *) &zero, 1);

    list = NULL;
    if (done == true)
        list = httpio_response_parse_headers((char *) buffer.data);
    httpio_byte_stream_free(&buffer);

    return list;
}

static bool
httpio_response_body_istext(const httpio_content *const content)
{
    bool istext;
    if (content->type == NULL)
        return true;
    istext = (strstr(content->type, "text") != NULL);
    if (istext == false)
        istext = (strstr(content->type, "html") != NULL);
    if (istext == false)
        istext = (strstr(content->type, "xml") != NULL);
    if (istext == false)
        istext = (strstr(content->type, "json") != NULL);
    return istext;
}

static httpio_body *
httpio_response_body_create(const httpio_content *const content,
                                                   uint8_t *data, size_t length)
{
    httpio_body *body;
    body = malloc(sizeof(*body));
    if (body == NULL)
        return NULL;
    if ((content->encoding != NULL) && (strstr(content->encoding, "gzip") != NULL))
        body->data = httpio_response_body_gunzip(data, &length);
    else
        body->data = data;
    body->length = length;
    if (data != body->data)
        free(data);
    if (httpio_response_body_istext(content) == true) {
        uint8_t *pointer;
        pointer = realloc(body->data, body->length + 1);
        if (pointer != NULL) {
            body->data = pointer;
            body->data[body->length] = '\0';
        } else {
            free(body->data);
            free(body);

            body = NULL;
        }
    }
    return body;
}

static uint8_t *
httpio_response_body_gunzip_failed(size_t *length)
{
    if (length != NULL)
        *length = 0;
    return NULL;
}

static uint8_t *
httpio_response_body_gunzip(uint8_t *data, size_t *length)
{
    z_stream zstream;
    httpio_bstream stream;
    size_t processed;

    if ((data == NULL) || (length == NULL) || (*length < 10) || (data[0] != 0x1F) || (data[1] != 0x8B))
        return httpio_response_body_gunzip_failed(length);

    httpio_byte_stream_start(&stream);
    memset(&zstream, 0, sizeof(zstream));

    zstream.zalloc = Z_NULL;
    zstream.zfree = Z_NULL;
    zstream.opaque = Z_NULL;
    zstream.avail_in = *length;
    zstream.next_in = data;
    if (inflateInit2(&zstream, 15 | 32) < 0)
        return NULL;
    processed = 0;
    while (zstream.avail_out == 0) {
        uint8_t out[0x4000];
        size_t inflated;

        memset(out, 0, sizeof(out));

        zstream.avail_out = sizeof(out);
        zstream.next_out = out;
        switch (inflate(&zstream, Z_NO_FLUSH)) {
        case Z_NEED_DICT:
            fprintf(stderr, "NEED DICT\n");
            goto error;
        case Z_DATA_ERROR:
            fprintf(stderr, "DATA ERROR\n");
            goto error;
        case Z_MEM_ERROR:
            fprintf(stderr, "MEM ERROR\n");
            goto error;
        }
        processed += *length - zstream.avail_in;
        *length = zstream.avail_in;
        inflated = sizeof(out) - zstream.avail_out;
        zstream.next_in = data + processed;

        httpio_byte_stream_append(&stream, out, inflated);
    }
    inflateEnd(&zstream);

    *length = stream.length;
    return stream.data;

error:
    *length = 0;

    httpio_byte_stream_free(&stream);
    inflateEnd(&zstream);

    return NULL;
}

static ssize_t
httpio_content_length(const char *const source)
{
    ssize_t length;
    char *endptr;
    if (source == NULL)
        return -1;
    length = strtol(source, &endptr, 10);
    if (*endptr != '\0')
        return 0;
    return length;
}

static httpio_body *
httpio_response_read_content_length(httpio_content *content, httpio *link)
{
    uint8_t *pointer;
    uint8_t *data;
    ssize_t remaining;
    // FIXME: we should not be usnig `calloc' something is wrong somewhere
    data = malloc(content->length);
    if (data == NULL)
        return NULL;
    remaining = content->length;
    pointer = data;
    while (remaining > 0) {
        ssize_t received;
        received = httpio_read(link, pointer, remaining, DEFAULT_TIMEOUT);
        if (received < 0)
            goto error;
        remaining -= received;
        pointer += received;
    }

    if (remaining != 0)
        goto error;

    return httpio_response_body_create(content, data, content->length);
error:
    free(data);
    return NULL;
}

static size_t
httpio_response_body_chunked_transfer_get_chunk_length(httpio *link)
{
    int index;
    size_t length;
    char string[16] = {0};
    bool eon;
    char *endptr;
    uint8_t value;

    eon = false;
    index = 0;
    while (httpio_read(link, &value, 1, DEFAULT_TIMEOUT) != -1) {
        if ((eon == false) && (value == ';'))
            eon = true;
        if ((index < 15) && (eon == false) && (value != '\r'))
            string[index++] = value;
        if (value == '\r') {
            if (httpio_read(link, &value, 1, DEFAULT_TIMEOUT) == -1)
                return -1;
            if (value != '\n')
                return -1;
            string[index] = '\0';
            length = (size_t) strtol(string, &endptr, 16);
            if (*endptr != '\0')
                return -1;
            return length;
        }
    }
    return -1;
}

static httpio_body *
httpio_response_read_chunked_transfer_encoding(httpio_content *content, httpio *link)
{
    httpio_bstream stream;
    bool finish;

    httpio_byte_stream_start(&stream);
    do
    {
        ssize_t length;
        uint8_t chunk[BYTE_STREAM_DEFAULT_SIZE];
        size_t expect;

        expect = httpio_response_body_chunked_transfer_get_chunk_length(link);
        finish = (expect == 0);
        while (expect > 0)
        {
            size_t ready;
            ready = sizeof(chunk);
            if (ready > expect)
                ready = expect;
            length = httpio_read(link, chunk, ready, DEFAULT_TIMEOUT);
            if (length < 0)
                goto error;
            httpio_byte_stream_append(&stream, chunk, length);
            expect -= length;
        }
        httpio_read(link, chunk, 2, DEFAULT_TIMEOUT);
    } while (finish == false);
    return httpio_response_body_create(content, stream.data, stream.length);
error:
    httpio_byte_stream_free(&stream);
    return NULL;
}


static httpio_body *
httpio_get_response_body(httpio_header_list *list, httpio *link)
{
    httpio_content content;
    const char *content_length;
    const char *transfer_encoding;

    transfer_encoding = httpio_header_list_get(list, "transfer-encoding");
    content_length = httpio_header_list_get(list, "content-length");
    if (transfer_encoding != NULL) {
        content.length = -1;
    } else {
        content.length = httpio_content_length(content_length);
    }
    content.type = httpio_header_list_get(list, "content-type");
    content.encoding = httpio_header_list_get(list, "content-encoding");
    if (content.length > 0) {
        return httpio_response_read_content_length(&content, link);
    } else if ((transfer_encoding != NULL) &&
                              (strcasecmp(transfer_encoding, "chunked") == 0)) {
        return httpio_response_read_chunked_transfer_encoding(&content, link);
    }

    return NULL;
}

httpio_response *
httpio_read_response(httpio *link)
{
    httpio_response *response;
    response = malloc(sizeof(*response));
    if (response == NULL)
        return NULL;
    response->code = httpio_get_response_code(link);
    response->headers = httpio_get_response_headers(link);
    response->body = httpio_get_response_body(response->headers, link);

    return response;
}

const httpio_header_list *
httpio_response_get_headers(httpio_response *response)
{
    if (response == NULL)
        return NULL;
    return response->headers;
}

const char *
httpio_header_list_get(const httpio_header_list *list, const char *const key)
{
    httpio_header **found;
    httpio_header header;
    httpio_header *pointer;

    if (list == NULL)
        return NULL;
    for (size_t i = 0; i < list->count; ++i) {
        httpio_header *self;
        self = list->headers[i];
        if (self == NULL)
            continue;
    }

    header.key = (char *) key;
    if ((list == NULL) || (list->headers == NULL))
        return NULL;
    pointer = &header;
    found = bsearch(&pointer, list->headers, list->count,
        sizeof(*found), httpio_headers_compare_key);
    if (found == NULL)
        return NULL;
    pointer = *found;

    return pointer->value;
}

void
httpio_response_free(httpio_response *response)
{
    if (response == NULL)
        return;
    httpio_response_headers_free(response->headers);
    httpio_response_code_free(response->code);
    httpio_response_body_free(response->body);

    free(response);
}

httpio_body *
httpio_response_get_body(httpio_response *response)
{
    if (response == NULL)
        return NULL;
    return response->body;
}

const uint8_t *
httpio_response_body_get_data(httpio_body *body)
{
    if (body == NULL)
        return NULL;
    return body->data;
}

uint8_t *
httpio_response_body_take_data(httpio_body *body)
{
    uint8_t *data;
    if (body == NULL)
        return NULL;
    data = body->data;
    body->data = NULL;
    body->length = 0;
    return data;
}

size_t
httpio_response_body_length(httpio_body *body)
{
    if (body == NULL)
        return 0;
    return body->length;
}

enum httpio_code
httpio_response_get_code(httpio_response *response)
{
    httpio_status *code;
    if (response == NULL)
        return HTTP_INVALID_CODE;
    code = response->code;
    if (code == NULL)
        return HTTP_INVALID_CODE;
    return code->value;
}

void
httpio_response_update_cookie(char **cookie, const httpio_header_list *const list)
{
    size_t length;
    if ((list == NULL) || (cookie == NULL))
        return;
    if (*cookie != NULL)
        length = strlen(*cookie);
    else
        length = 0;
    for (size_t i = 0; i < list->count; ++i)
    {
        char *separator;
        const httpio_header *header;
        ptrdiff_t size;
        void *pointer;
        header = list->headers[i];
        if (strcasecmp(header->key, "set-cookie") != 0)
            continue;
        separator = strchr(header->value, ';');
        if (separator == NULL)
            separator = strchr(header->value, '\0');
        size = (separator - header->value);

        pointer = realloc(*cookie, length + size + 3);
        if (pointer == NULL)
            continue;
        *cookie = pointer;

        if (length > 0)
        {
            (*cookie)[length++] = ';';
            (*cookie)[length++] = ' ';
        }
        (*cookie)[length + size] = '\0';

        memcpy(*cookie + length, header->value, size);
        length += size;
    }
}
