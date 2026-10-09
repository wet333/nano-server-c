#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include "server.h"
#include "file_handler.h"
#include "constants.h"
#include "http_request.h"

// Set to 0 by Ctrl+C (SIGINT) or SIGTERM to stop the main loop
static volatile sig_atomic_t keep_running = 1;

static void handle_stop_signal(int signal_number) {
    (void)signal_number;
    keep_running = 0;
}

static void setup_signals(void) {
    // No SA_RESTART: the signal must interrupt a blocked accept() so the loop can end
    struct sigaction stop_action = {0};
    stop_action.sa_handler = handle_stop_signal;
    sigemptyset(&stop_action.sa_mask);
    sigaction(SIGINT, &stop_action, NULL);
    sigaction(SIGTERM, &stop_action, NULL);

    // A client that disconnects mid-response makes send() fail with EPIPE
    // instead of killing the whole server
    signal(SIGPIPE, SIG_IGN);
}

int main(int argc, char *argv[]) {

    // // Argument checks
    // if (argc < 2) {
    //     fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
    //     return 1;
    // }

    // char *response_file = argv[1]; // Configure file tu return

    setup_signals();

    // Init Server Socket
    HttpServer server = init_server(8080);

    int new_socket;
    int addrlen = sizeof(server.address);

    // Main Loop
    while (keep_running) {

        printf("Listening for connections...\n");

        // Client socket connection
        new_socket = accept(
            server.socket_fd,
            (struct sockaddr *)&server.address,
            (socklen_t*)&addrlen
        );

        if (new_socket < 0) {
            // Interrupted by Ctrl+C: keep_running is now 0 and the loop ends
            if (errno != EINTR) {
                perror("Accept failed");
            }
            continue;
        }

        // Read and parse the HTTP request
        char request_buffer[BUFFER_SIZE];
        HttpRequest request;
        
        int bytes_read = read_request(new_socket, &request, request_buffer, sizeof(request_buffer));
        
        if (bytes_read > 0) {
            // Print parsed request information
            const char *method_str = "UNKNOWN";
            switch (request.method) {
                case HTTP_METHOD_GET: method_str = "GET"; break;
                case HTTP_METHOD_POST: method_str = "POST"; break;
                case HTTP_METHOD_PUT: method_str = "PUT"; break;
                case HTTP_METHOD_DELETE: method_str = "DELETE"; break;
                case HTTP_METHOD_PATCH: method_str = "PATCH"; break;
                case HTTP_METHOD_HEAD: method_str = "HEAD"; break;
                case HTTP_METHOD_OPTIONS: method_str = "OPTIONS"; break;
                default: method_str = "UNKNOWN"; break;
            }
            
            // Log HTTP method
            printf("Request Method: %s\n", method_str);

            // Log Request PATH
            if (request.path != NULL) {
                printf("Request Path: %s\n", request.path);
            }

            // Send requested file
            send_file(new_socket, request.path);
            
            // Clean up request memory
            free_request(&request);

        } else if (bytes_read == 0) {
            printf("Client closed connection\n");
            close(new_socket);
            continue;
        } else {
            printf("Error reading request\n");
            close(new_socket);
            continue;
        }

        close(new_socket);
    }

    // Shutdown
    printf("\nShutting down...\n");
    close(server.socket_fd);
    printf("Server socket closed.\n");

    return 0;
}