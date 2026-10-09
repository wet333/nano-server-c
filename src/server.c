#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

HttpServer init_server(int port) {

    HttpServer server;
    server.port = port;
    int opt = 1;

    // Create Server Socket (Waiting for connections)
    if ((server.socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("An error occurred while creating the server socket");
        exit(EXIT_FAILURE);
    }

    // Allow binding the port again right after a restart, while the
    // connections of the previous run still wait in the TIME_WAIT state
    if (setsockopt(server.socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Error while setting SO_REUSEADDR");
        exit(EXIT_FAILURE);
    }

    // Configure Server Socket
    server.address.sin_family = AF_INET;
    server.address.sin_addr.s_addr = INADDR_ANY;
    server.address.sin_port = htons(port);

    // Bind socket to Port
    if (bind(server.socket_fd, (struct sockaddr *)&server.address, sizeof(server.address)) < 0) {
        perror("Error while trying to bind socket");
        exit(EXIT_FAILURE);
    }

    // Start listening
    if (listen(server.socket_fd, 3) < 0) {
        perror("Error when trying to start listening");
        exit(EXIT_FAILURE);
    }

    printf("Server initialized on port %d\n\n", port);
    return server;
}