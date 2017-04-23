#include <http-util.h>

#include <stdarg.h>

#include <string.h>
#include <ctype.h>
#include <stddef.h>
#include <math.h>
#include <stdio.h>

#include <unistd.h>
#include <fcntl.h>

#include <signal.h>
#include <errno.h>

#define time timespec
static inline struct time *
httpio_get_timeout(struct time *source, int64_t value)
{
    if (value < 0)
        return NULL;
    source->tv_sec = ((time_t) ((double) value / 1.0E9));
    source->tv_nsec = ((time_t) ((double) value - 1.0E9 * source->tv_sec));
    return source;
}

static ssize_t util_string_list_append(char ***list, const char *const string, size_t length, size_t count);
static inline void base64_encode_chunk(const uint8_t *const chunk, uint8_t destination[4], size_t count);
static inline void base64_decode_chunk(const uint8_t *const data, uint8_t result[3]);

static int
httpio_select(int nfds, fd_set *readfds, fd_set *writefds,
                              fd_set *exceptfds, const struct timespec *timeout)
{
    sigset_t sigset;
    int result;

    sigemptyset(&sigset);

    result = pselect(nfds, readfds, writefds, exceptfds, timeout, &sigset);
    switch (result) {
    case -1:
        if (errno == EINTR)
            fprintf(stderr, "pselect was, interrupted by a signal\n");
        break;
    case 0:
        break;
    }
    return result;
}

char **
util_string_splitstr(const char *const string, const char *const delimiter)
{
    size_t count;
    const char *head;
    const char *tail;
    char **list;
    size_t size;

    head = string;
    list = NULL;
    count = 0;
    tail = head;
    size = strlen(delimiter);
    while (*tail != '\0')
    {
        ptrdiff_t length;
        ssize_t result;
        tail = strstr(head, delimiter);
        if (tail == NULL)
            tail = strchr(head, '\0');
        length = (ptrdiff_t) (tail - head);
        result = util_string_list_append(&list, head, length, count);
        if (result < 0)
            goto error;
        count = result;
        head = tail + size;
    }
    util_string_list_append(&list, NULL, 0, count);
    return list;

error:
    for (size_t index = 0; index < count; ++index)
        free(list[index]);
    free(list);

    return NULL;
}

char **
util_string_splitchr(const char *const string, char character)
{
    char delimiter[2] = {character, '\0'};
    return util_string_splitstr(string, delimiter);
}

static inline void
base64_encode_chunk(const uint8_t *const chunk, uint8_t dst[4], size_t count)
{
    if (count > 0) {
        const char *chr = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                          "abcdefghijklmnopqrstuvwxyz"
                          "0123456789+/";
        const uint8_t x[3] = {
                          chunk[0]    ,
            (count > 1) ? chunk[1] : 0,
            (count > 2) ? chunk[2] : 0
        };
        dst[0] = chr[(x[0] >> 2) & 0x3F];
        dst[1] = chr[(x[1] >> 4) | ((x[0] & 0x03) << 4)];
        if (count > 1)
            dst[2] = chr[((x[2] & 0xFC) >> 6) | ((x[1] & 0x0F) << 2)];
        else
            dst[2] = '=';
        if (count > 2)
            dst[3] = chr[x[2] & 0x3F];
        else
            dst[3] = '=';
    }
}

static inline void
base64_decode_chunk(const uint8_t *const src, uint8_t result[3])
{
    const uint8_t chr[255] = {
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 62,  0,  0,  0, 63,
       52, 53, 54, 55, 56, 57, 58, 59, 60, 61,  0,  0,  0,  0,  0,  0,
        0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
       15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,  0,  0,  0,  0,  0,
        0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
       41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0
    };
    result[0] = ((chr[src[0]] << 2) & 0xFC) | ((chr[src[1]] >> 4) & 0x3F);
    result[1] = ((chr[src[1]] & 0x0F) << 4) | ((chr[src[2]] >> 2) & 0x0F);
    result[2] = ((chr[src[2]] & 0x03) << 6) | (chr[src[3]] & 0x3F);
}


char *
httpio_base64_encode(const uint8_t *const data, size_t input_size)
{
    uint8_t *result;
    uint8_t *pointer;
    size_t counter;
    size_t size;
    uint8_t remainder;
    remainder = input_size % 3;
    size = input_size - remainder;
    result = malloc(1 + 4 * (size / 3 + ((remainder != 0) ? 1 : 0)));
    if (result == NULL)
        return NULL;
    pointer = result;
    for (counter = 0; counter < size; counter += 3, pointer += 4)
        base64_encode_chunk(data + counter, pointer, 3);
    base64_encode_chunk(data + counter, pointer, remainder);
    *(pointer + ((remainder > 0) ? 4 : 0)) = '\0';
    return (char *) result;
}

uint8_t *
httpio_base64_decode(const char *const data, size_t *length)
{
    uint8_t *result;
    size_t input_size;
    input_size = strlen(data);
    if (input_size % 4 != 0)
        return NULL;
    *length = 3 * input_size / 4;
    while (data[--input_size] == '=')
        *length -= 1;
    result = malloc(*length + 1);
    if (result == NULL)
        return NULL;
    for (size_t i = 0; i < input_size; i += 4)
        base64_decode_chunk((uint8_t *) data + i, result + 3 * i / 4);
    result[*length] = '\0';

    return result;
}

static ssize_t
util_string_list_append(char ***list, const char *const string, size_t length, size_t count)
{
    char **pointer;
    pointer = realloc(*list, (count + 1) * sizeof(*pointer));
    if (pointer == NULL)
        return -1;
    *list = pointer;
    if (string != NULL)
    {
        pointer[count] = malloc(length + 1);
        if (pointer[count] == NULL)
            return -1;
        memcpy(pointer[count], string, length);
        pointer[count][length] = '\0';
    } else
        pointer[count] = NULL;
    return count + 1;
}

char *
httpio_stripdup(const char *string)
{
    size_t length;
    char *copy;
    if (string == NULL)
        return NULL;
    while ((string[0] != '\0') && (isspace((unsigned char) string[0]) != 0))
        string++;
    length = strlen(string);
    if (length == 0)
        return strdup(string);
    while ((string[length - 1] != '\0') && (isspace((unsigned char) string[length - 1]) != 0))
        --length;
    copy = malloc(length + 1);
    if (copy == NULL)
        return NULL;
    memcpy(copy, string, length);

    copy[length] = '\0';
    return copy;
}

void
httpio_string_list_free(char **list)
{
    if (list == NULL)
        return;
    for (size_t index = 0; list[index] != NULL; ++index)
        free(list[index]);
    free(list);
}

void
httpio_byte_stream_start(struct httpio_bstream *buffer)
{
    buffer->data = malloc(BYTE_STREAM_DEFAULT_SIZE);
    buffer->capacity = BYTE_STREAM_DEFAULT_SIZE;
    buffer->length = 0;
}

void
httpio_byte_stream_append(struct httpio_bstream *buffer, const uint8_t *const data, size_t size)
{
    size_t length;

    length = buffer->length;
    if (length + size >= buffer->capacity) {
        uint8_t *resized;
        size_t capacity;
        capacity = BYTE_STREAM_DEFAULT_SIZE + length + size;
        resized = realloc(buffer->data, capacity);
        if (resized == NULL)
            return;
        buffer->data = resized;
        buffer->capacity = capacity;
    }
    memcpy(&buffer->data[length], data, size);

    buffer->length += size;
}

bool
httpio_byte_stream_ends_with(const struct httpio_bstream *const buffer, const char *const tail, size_t length)
{
    size_t far;
    uint8_t * start;

    far = buffer->length;
    if (far <= length)
        return false;
    start = &buffer->data[far - length];
    if (memcmp(tail, start, length) == 0)
        return true;

    return false;
}

void
httpio_byte_stream_free(struct httpio_bstream *buffer)
{
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}

int32_t
httpio_safe_random(void)
{
    struct timeval timeout;
    int devrandom;
    int value;
    fd_set set;

    memset(&timeout, 0, sizeof(timeout));

    devrandom = open("/dev/random", O_RDONLY);
    if (devrandom == -1)
        return rand();
    FD_ZERO(&set);
    FD_SET(devrandom, &set);

    if (select(devrandom + 1, &set, NULL, NULL, &timeout) == 1)
    {
        if (read(devrandom, &value, sizeof(value)) == (ssize_t) sizeof(value))
        {
            close(devrandom);
            return value;
        }
    }
    close(devrandom);

    return rand();
}

char *
httpio_concatenate(const char *const first, ...)
{
    const char *next;
    char *result;
    va_list args;
    size_t length;

    result = NULL;
    length = 0;

    va_start(args, first);
    next = first;
    while (next != NULL)
    {
        char *buffer;
        size_t count;

        count = strlen(next);
        buffer = realloc(result, count + length + 1);
        if (buffer == NULL)
            continue;
        result = buffer;

        memcpy(result + length, next, count);

        length += count;
        next = va_arg(args, const char *);
    }

    if (result != NULL)
        result[length] = '\0';
    va_end(args);
    return result;
}

static size_t
util_countsubstr(const char *string, const char *const needle)
{
    size_t count;
    char *next;
    size_t step;
    step = strlen(needle);
    for (count = 0; (next = strstr(string, needle)) != NULL; ++count)
        string = next + step;
    return count;
}

size_t
httpio_strreplace(char **output, const char *const needle, const char *const replacement)
{
    size_t count;
    size_t add;
    size_t remove;
    size_t length;
    ptrdiff_t change;
    char *result;
    char *pointer;
    char *string;
    char *next;

    if ((output == NULL) || (needle == NULL) || (replacement == NULL))
        return 0;
    length = strlen(*output);
    count = util_countsubstr(*output, needle);
    remove = strlen(needle);
    add = strlen(replacement);
    result = malloc(length + (add - remove) * count + 1);
    if (result == NULL)
        return 0;
    pointer = result;
    string = *output;
    for (count = 0; (next = strstr(string, needle)) != NULL; ++count)
    {
        change = next - string;

        memcpy(pointer, string, change);
        pointer += change;
        memcpy(pointer, replacement, add);
        string = next + remove;
        pointer += add;
    }
    change = (*output + length) - string;

    memcpy(pointer, string, change);
    pointer[change] = '\0';

    free(*output);
    *output = result;

    return length + (add - remove) * count;
}

bool
httpio_socket_has_data(int sock, int64_t nanoseconds)
{
    struct time _timeout;
    struct time *timeout;
    fd_set set;
    FD_ZERO(&set);
    FD_SET(sock, &set);
    timeout = httpio_get_timeout(&_timeout, nanoseconds);
    if (httpio_select(sock + 1, &set, NULL, NULL, timeout) != 1)
        return false;
    return FD_ISSET(sock, &set);
}

bool
httpio_socket_wants_data(int sock, int64_t nanoseconds)
{
    struct time _timeout;
    struct time *timeout;
    fd_set set;
    FD_ZERO(&set);
    FD_SET(sock, &set);
    timeout = httpio_get_timeout(&_timeout, nanoseconds);
    if (httpio_select(sock + 1, NULL, &set, NULL, timeout) != 1)
        return false;
    return FD_ISSET(sock, &set);
}
