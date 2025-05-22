#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8080
#define MAX_EVENTS 10
#define BUFFER_SIZE 512

// Set a file descriptor to non-blocking mode
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
    int server_fd, epoll_fd;
    struct sockaddr_in addr;
    struct epoll_event ev, events[MAX_EVENTS];

    // Create listening socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Allow quick reuse of the address
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Bind to 127.0.0.1:8080
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Start listening
    if (listen(server_fd, SOMAXCONN) < 0) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    printf("Server listening on 127.0.0.1:%d\n", PORT);

    // Create epoll instance
    if ((epoll_fd = epoll_create1(0)) < 0) {
        perror("epoll_create1");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Add the listening socket to epoll
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) < 0) {
        perror("epoll_ctl: server_fd");
        close(server_fd);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    // Event loop
    while (1) {
        int n_fds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (n_fds < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n_fds; i++) {
            int fd = events[i].data.fd;

            // New connection
            if (fd == server_fd) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int conn_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                if (conn_fd < 0) {
                    perror("accept");
                    continue;
                }
                // Optional: make client socket non-blocking
                if (set_nonblocking(conn_fd) < 0) {
                    perror("set_nonblocking");
                    close(conn_fd);
                    continue;
                }
                ev.events = EPOLLIN;
                ev.data.fd = conn_fd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_fd, &ev) < 0) {
                    perror("epoll_ctl: conn_fd");
                    close(conn_fd);
                    continue;
                }
                printf("New client connected: %d\n", conn_fd);

            } else {
                // Data from existing client
                char buffer[BUFFER_SIZE];
                ssize_t count = read(fd, buffer, sizeof(buffer) - 1);
                if (count < 0) {
                    perror("read");
                    continue;
                } else if (count == 0) {
                    // Client disconnected
                    printf("Client %d disconnected\n", fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                } else {
                    buffer[count] = '\0';
                    printf("Client %d sent: %s\n", fd, buffer);
                    // Echo back
                    ssize_t sent = 0;
                    while (sent < count) {
                        ssize_t wrote = write(fd, buffer + sent, count - sent);
                        if (wrote < 0) {
                            perror("write");
                            break;
                        }
                        sent += wrote;
                    }
                }
            }
        }
    }

    close(server_fd);
    close(epoll_fd);
    return 0;
}
