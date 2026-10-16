#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define DEFAULT_PORT 18080

static void handle_client(int client_fd) {
    char req[4096];
    ssize_t n = recv(client_fd, req, sizeof(req) - 1, 0);
    if (n <= 0) {
        close(client_fd);
        return;
    }
    req[n] = '\0';

    if (strncmp(req, "GET /api/tags", 13) == 0) {
        const char *body =
            "{\"models\":["
            "{\"name\":\"mock-qwen:7b\"},"
            "{\"name\":\"mock-llama3:8b\"}"
            "]}";
        char resp[1024];
        int len = snprintf(resp, sizeof(resp),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %zu\r\n"
            "Connection: close\r\n\r\n"
            "%s",
            strlen(body), body);
        (void)send(client_fd, resp, (size_t)len, 0);
    } else if (strncmp(req, "POST /v1/chat/completions", 25) == 0) {
        const char *header =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/event-stream\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: close\r\n\r\n";
        (void)send(client_fd, header, strlen(header), 0);

        const char *chunks[] = {
            "data: {\"choices\":[{\"delta\":{\"content\":\"Hello \"}}]}\n\n",
            "data: {\"choices\":[{\"delta\":{\"content\":\"from \"}}]}\n\n",
            "data: {\"choices\":[{\"delta\":{\"content\":\"SKEET \"}}]}\n\n",
            "data: {\"choices\":[{\"delta\":{\"content\":\"mock!\"}}]}\n\n",
            "data: [DONE]\n\n"
        };

        for (size_t i = 0; i < sizeof(chunks) / sizeof(chunks[0]); i++) {
            usleep(25000); /* 25ms delay to simulate real streaming */
            (void)send(client_fd, chunks[i], strlen(chunks[i]), 0);
        }
    } else {
        const char *not_found =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Length: 0\r\n"
            "Connection: close\r\n\r\n";
        (void)send(client_fd, not_found, strlen(not_found), 0);
    }

    close(client_fd);
}

int main(int argc, char **argv) {
    int port = DEFAULT_PORT;
    if (argc > 1) {
        port = atoi(argv[1]);
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("Mock server listening on http://127.0.0.1:%d\n", port);
    fflush(stdout);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            break;
        }
        handle_client(client_fd);
    }

    close(server_fd);
    return 0;
}
