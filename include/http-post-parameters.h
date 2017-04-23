#ifndef __HTI_HTTP_POST_PARAMETERS_H__
#define __HTI_HTTP_POST_PARAMETERS_H__

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct httpio_postdata httpio_postdata;
httpio_postdata *httpio_post_parameters_create(size_t count);
void httpio_post_parameters_append(httpio_postdata *parameters, const char *const name, const char *const value);
void httpio_post_parameters_free(httpio_postdata *parameters);
char *httpio_post_parameters_urlencoded(const httpio_postdata *const parameters);
void httpio_post_parameters_set(httpio_postdata *const parameters, const char *const name, const char *const value);

#ifdef __cplusplus
}
#endif

#endif // __HTI_HTTP_POST_PARAMETERS_H__
