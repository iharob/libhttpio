#ifndef __HTTP_PROTOCOL_H__
#define __HTTP_PROTOCOL_H__

#include <http-connection.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct httpio_response httpio_response;
typedef struct httpio_header_list httpio_header_list;
typedef struct httpio_header httpio_header;
typedef struct httpio_body httpio_body;

enum httpio_code
{
    HTTP_OK = 200,
    HTTP_BAD_REQUEST = 400,
    HTTP_FORBIDDEN = 401,
    HTTP_NOT_FOUND = 404,
    HTTP_RESOURCE_UNAVAILABLE = 503,
    HTTP_REDIRECT = 301,
    HTTP_OBJECT_MOVED = 302,
    HTTP_INVALID_CODE = -1
};

httpio_response *httpio_read_response(httpio *link);
const char *httpio_header_list_get(const httpio_header_list *list, const char *const key);
void httpio_response_free(httpio_response *response);
const httpio_header_list *httpio_response_get_headers(httpio_response *response);
httpio_body *httpio_response_get_body(httpio_response *response);
const uint8_t *httpio_response_body_get_data(httpio_body *body);
size_t httpio_response_body_length(httpio_body *body);
enum httpio_code httpio_response_get_code(httpio_response *response);
uint8_t *httpio_response_body_take_data(httpio_body *body);
void httpio_response_update_cookie(char **cookie, const httpio_header_list *const list);
#ifdef __cplusplus
}
#endif

#endif /* __HTTP_PROTOCOL_H__ */
