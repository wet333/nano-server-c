#ifndef FILE_HANDLER_H
#define FILE_HANDLER_H

#include "http_response.h"

void send_file(int client_socket, char *filename);
long get_file_size(char *filename);
int stream_file_to_client(int client_socket, char *filename);
MimeType get_mimetype_for_file(char *filename);

#endif