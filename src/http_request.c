#include "http_request.h"
#include "constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Returns the request_size and stores HTTP request data on req
int read_request(int client_socket, HttpRequest *req, char *buffer, size_t buffer_size) {
    if (req == NULL || buffer == NULL) {
        return -1;
    }

    // Initialize request structure
    req->method = HTTP_METHOD_UNKNOWN;
    req->path = NULL;
    req->raw_request = NULL;
    req->raw_request_size = 0;

    // Read data from socket
    ssize_t bytes_read = recv(client_socket, buffer, buffer_size - 1, 0);
    
    if (bytes_read < 0) {
        perror("Error reading from socket");
        return -1;
    }
    
    if (bytes_read == 0) {
        return 0;
    }

    // Null-terminate the buffer
    buffer[bytes_read] = '\0';
    
    // Store raw request
    req->raw_request = buffer;
    req->raw_request_size = bytes_read;

    // Request parsing starts

    // Find the first line (up to \r\n or \n)
    char *request_line = buffer;
    char *line_end = strstr(request_line, "\r\n");
    size_t line_len;
    
    if (line_end != NULL) {
        line_len = line_end - request_line;
    } else {
        line_end = strchr(request_line, '\n');
        if (line_end != NULL) {
            line_len = line_end - request_line;
        } else {
            line_len = strlen(request_line);
        }
    }
    
    // Create a null-terminated copy of the request line for parsing
    char request_line_copy[BUFFER_SIZE];
    if (line_len < sizeof(request_line_copy)) {
        strncpy(request_line_copy, request_line, line_len);
        request_line_copy[line_len] = '\0';
    } else {
        strncpy(request_line_copy, request_line, sizeof(request_line_copy) - 1);
        request_line_copy[sizeof(request_line_copy) - 1] = '\0';
    }

    // Parse method
    req->method = parse_method(request_line_copy);
    
    // Parse path
    char path_buffer[BUFFER_SIZE];
    if (parse_path(request_line_copy, path_buffer, sizeof(path_buffer)) == 0) {
        // Allocate memory for path
        req->path = malloc(strlen(path_buffer) + 1);
        if (req->path != NULL) {
            strcpy(req->path, path_buffer);
        }
    }

    return bytes_read;
}

HttpMethod parse_method(const char *request_line) {
    if (request_line == NULL) {
        return HTTP_METHOD_UNKNOWN;
    }

    if (strncmp(request_line, "GET ", 4) == 0) {
        return HTTP_METHOD_GET;
    } else if (strncmp(request_line, "POST ", 5) == 0) {
        return HTTP_METHOD_POST;
    } else if (strncmp(request_line, "PUT ", 4) == 0) {
        return HTTP_METHOD_PUT;
    } else if (strncmp(request_line, "DELETE ", 7) == 0) {
        return HTTP_METHOD_DELETE;
    } else if (strncmp(request_line, "PATCH ", 6) == 0) {
        return HTTP_METHOD_PATCH;
    } else if (strncmp(request_line, "HEAD ", 5) == 0) {
        return HTTP_METHOD_HEAD;
    } else if (strncmp(request_line, "OPTIONS ", 8) == 0) {
        return HTTP_METHOD_OPTIONS;
    }

    return HTTP_METHOD_UNKNOWN;
}

int parse_path(const char *request_line, char *path_buffer, size_t path_buffer_size) {
    if (request_line == NULL || path_buffer == NULL || path_buffer_size == 0) {
        return -1;
    }

    // Find the first space (after method)
    const char *path_start = strchr(request_line, ' ');
    if (path_start == NULL) {
        return -1;
    }
    path_start = path_start + 2; // Skip the space and the '/'

    // Find the second space (after path, before HTTP version)
    const char *path_end = strchr(path_start, ' ');
    if (path_end == NULL) {
        // If no second space, path goes to end of line
        path_end = path_start + strlen(path_start);
    }

    size_t path_len = path_end - path_start;
    
    // Ensure we don't overflow the buffer
    if (path_len >= path_buffer_size) {
        path_len = path_buffer_size - 1;
    }

    // Copy path to buffer
    strncpy(path_buffer, path_start, path_len);
    path_buffer[path_len] = '\0';

    return 0;
}

void free_request(HttpRequest *req) {
    if (req == NULL) {
        return;
    }

    // Note: We don't free raw_request as it points to the user's buffer
    if (req->path != NULL) {
        free(req->path);
        req->path = NULL;
    }
}
