#ifndef __HTTP_UTIL_H__
#define __HTTP_UTIL_H__

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct httpio_bstream;
typedef uint8_t uint8_t;

typedef struct httpio_bstream
{
    uint8_t *data;
    size_t capacity;
    size_t length;
} httpio_bstream;

#define BYTE_STREAM_DEFAULT_SIZE 0x4000

uint8_t *httpio_base64_decode(const char *const data, size_t *length);
char *httpio_base64_encode(const uint8_t *const data, size_t length);
char *httpio_stripdup(const char *string);
void httpio_string_list_free(char **list);
char **httpio_string_splitchr(const char *const string, char delimiter);
char **bt_util_string_splitstr(const char *const string, const char *const delimiter);
void httpio_byte_stream_start(struct httpio_bstream *buffer);
void httpio_byte_stream_append(struct httpio_bstream *buffer, const uint8_t *const data, size_t size);
bool httpio_byte_stream_ends_with(const struct httpio_bstream *const buffer, const char *const tail, size_t length);
void httpio_byte_stream_free(struct httpio_bstream *buffer);
char *httpio_concatenate(const char *const first, ...);
int32_t httpio_safe_random(void);
size_t httpio_strreplace(char **string, const char *const needle, const char *const replacement);

bool httpio_socket_has_data(int sock, int64_t nanoseconds);
bool httpio_socket_wants_data(int sock, int64_t nanoseconds);

#define countof(list) sizeof list / sizeof *list
#define DEFAULT_TIMEOUT 1000000000000LL

#ifdef __cplusplus
}
#endif

#endif /* __HTTP_UTIL_H__ */
