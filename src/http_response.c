#include "http_response.h"
#include "constants.h"

const char* get_status_phrase(HttpStatus status) {
    switch (status) {
        case HTTP_OK: return "OK";
        case HTTP_CREATED: return "Created";
        case HTTP_BAD_REQUEST: return "Bad Request";
        case HTTP_NOT_FOUND: return "Not Found";
        case HTTP_INTERNAL_ERROR: return "Internal Server Error";
        default: return "Unknown";
    }
}

const char* get_mime_string(MimeType type) {
    switch (type) {
        case MIME_TEXT_HTML: return "text/html; charset=UTF-8";
        case MIME_TEXT_CSS: return "text/css; charset=UTF-8";
        case MIME_TEXT_PLAIN: return "text/plain; charset=UTF-8";
        case MIME_APPLICATION_JSON: return "application/json";
        case MIME_IMAGE_PNG: return "image/png";
        default: return "application/octet-stream";
    }
}

void response_init(HttpResponse *res, HttpStatus status, MimeType type) {
    res->status = status;
    res->content_type = type;
    res->header_count = 0;
    res->body = NULL;
    res->body_length = 0;
}

void response_add_header(HttpResponse *res, char *key, char *value) {
    if (res->header_count < 20) {
        res->extra_headers[res->header_count].key = key;
        res->extra_headers[res->header_count].value = value;
        res->header_count++;
    }
}

void response_set_body(HttpResponse *res, char *body) {
    res->body = body;
    if (body != NULL) {
        res->body_length = strlen(body); // Doesnt work for images
    } else {
        res->body_length = 0;
    }
}

// Returns the total size of the response message
int get_response_size(HttpResponse *res, char *buffer, size_t buffer_size) {

    // HTTP Status line
    int response_size = snprintf(buffer, buffer_size, 
        "HTTP/1.1 %d %s\r\n", res->status, get_status_phrase(res->status));

    // Standard Headers
    response_size += snprintf(buffer + response_size, buffer_size - response_size,
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "Server: %s\r\n",
        get_mime_string(res->content_type), res->body_length, SERVER_NAME
    );

    // Custom Headers
    for (int i = 0; i < res->header_count; i++) {
        response_size += snprintf(buffer + response_size, buffer_size - response_size,
            "%s: %s\r\n",
            res->extra_headers[i].key,
            res->extra_headers[i].value);
    }

    // Empty line separator
    response_size += snprintf(buffer + response_size, buffer_size - response_size, "\r\n");

    // Response Body
    if (res->body != NULL && (size_t)response_size < buffer_size) {
        size_t space_left = buffer_size - response_size;
        size_t write_size = (res->body_length < space_left) ? res->body_length : space_left;
        
        memcpy(buffer + response_size, res->body, write_size);
        response_size += write_size;
    }

    return response_size;
}