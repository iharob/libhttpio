#include <http-post-parameters.h>
#include <http-util.h>

#include <stdio.h>
#include <string.h>

struct httpio_postdata
{
    size_t count;
    size_t capacity;
    char **names;
    char **values;
    int sorted;
};

httpio_postdata *
httpio_post_parameters_create(size_t capacity)
{
    httpio_postdata *result;
    result = malloc(sizeof(*result));
    if (result == NULL)
        return NULL;
    result->sorted = 0;
    result->count = 0;
    result->capacity = capacity;
    result->names = malloc(capacity * sizeof(*result->names));
    result->values = malloc(capacity * sizeof(*result->values));

    if ((result->names != NULL) && (result->values != NULL))
        return result;

    free(result->names);
    free(result->values);
    free(result);

    return NULL;
}

void
httpio_post_parameters_append(httpio_postdata *parameters, const char *const name, const char *const value)
{
    if ((parameters == NULL) || (name == NULL))
        return;

    if (parameters->count + 1 >= parameters->capacity)
    {
        void *pointer;
        size_t capacity;

        capacity = parameters->count + parameters->capacity + 1;
        pointer = realloc(parameters->names, capacity * sizeof(*parameters->names));
        if (pointer == NULL)
            return;
        parameters->names = pointer;

        pointer = realloc(parameters->values, capacity * sizeof(*parameters->values));
        if (pointer == NULL)
            return;
        parameters->values = pointer;
        parameters->capacity = capacity;
    }

    parameters->names[parameters->count] = strdup(name);
    if (value == NULL)
        parameters->values[parameters->count] = NULL;
    else
        parameters->values[parameters->count] = strdup(value);
    ++parameters->count;
}

void
httpio_post_parameters_free(httpio_postdata *parameters)
{
    if (parameters == NULL)
        return;
    for (size_t i = 0; i < parameters->count; ++i)
    {
        free(parameters->names[i]);
        free(parameters->values[i]);
    }
    free(parameters->names);
    free(parameters->values);
    free(parameters);
}

// TODO: make this function more generic
static void
httpio_post_parameters_urlencode_value(const char *const value, httpio_bstream *stream)
{
    uint8_t plus;
    if (value == NULL)
        return;
    plus = '+';
    for (size_t i = 0; value[i] != '\0'; ++i) {
        char encoded[4];
        ssize_t length;
        switch (value[i]) {
        case ' ':
            httpio_byte_stream_append(stream, &plus, 1);
            break;
        case '!':
        case '*':
        case '\'':
        case '(':
        case ')':
        case ';':
        case ':':
        case '@':
        case '&':
        case '=':
        case '$':
        case ',':
        case '/':
        case '?':
        case '#':
        case '[':
        case ']':
        case '{':
        case '}':
        case '~':
        case '`':
        case '^':
        case '>':
        case '<':
        case '%':
        case '"':
        case '\r':
        case '\n':
        case '\\':
            length = snprintf(encoded, sizeof(encoded), "%%%02X", value[i]);
            if ((length < 0) || ((size_t) length >= sizeof(encoded)))
                return;
            httpio_byte_stream_append(stream, (uint8_t *) encoded, length);
            break;
        default:
            httpio_byte_stream_append(stream, (uint8_t *) &value[i], 1);
            break;
        }
    }
}

char *
httpio_post_parameters_urlencoded(const httpio_postdata *const parameters)
{
    httpio_bstream stream;

    uint8_t equal;
    uint8_t ampersand;
    uint8_t nul;
    if ((parameters == NULL) || (parameters->count == 0))
        return NULL;
    equal = '=';
    ampersand = '&';
    nul = '\0';

    httpio_byte_stream_start(&stream);
    httpio_post_parameters_urlencode_value(parameters->names[0], &stream);
    httpio_byte_stream_append(&stream, &equal, 1);
    httpio_post_parameters_urlencode_value(parameters->values[0], &stream);
    for (size_t i = 1; i < parameters->count; ++i) {
        httpio_byte_stream_append(&stream, &ampersand, 1);
        httpio_post_parameters_urlencode_value(parameters->names[i], &stream);
        httpio_byte_stream_append(&stream, &equal, 1);
        httpio_post_parameters_urlencode_value(parameters->values[i], &stream);
    }
    httpio_byte_stream_append(&stream, &nul, 1);
    return (char *) stream.data;
}

void
httpio_post_parameters_set(httpio_postdata *const parameters, const char *const name, const char *const value)
{
    for (size_t i = 0; i < parameters->count; ++i) {
        if (strcmp(parameters->names[i], name) != 0)
            continue;
        free(parameters->values[i]);
        if (value == NULL)
            parameters->values[i] = NULL;
        else
            parameters->values[i] = strdup(value);
        return;
    }
    httpio_post_parameters_append(parameters, name, value);
}
