#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>
#include <fcntl.h>

#define PORT 9000
#define BUFFER_SIZE 1024
#define FILE_PATH "/var/tmp/aesdsocketdata"

static volatile sig_atomic_t running = 1;
static int server_fd = -1;

void cleanup() {
    if (server_fd != -1) {
        close(server_fd);
    }
    unlink(FILE_PATH);
    closelog();
}

void signal_handler(int signo) {
    running = 0;
}

int main(int argc, char *argv[]) {
    struct sockaddr_in address = {0};
    socklen_t addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};
    int client_socket = -1, fd = -1;
    int daemon_mode = 0;
    ssize_t bytes_read, bytes_written;
    
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);
    
    struct sigaction sa = {0};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL) == -1) {
        syslog(LOG_ERR, "Error setting up signal handler: %s", strerror(errno));
        cleanup();
        return -1;
    }

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        syslog(LOG_ERR, "Socket creation failed: %s", strerror(errno));
        cleanup();
        return -1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        syslog(LOG_ERR, "setsockopt failed: %s", strerror(errno));
        cleanup();
        return -1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
        syslog(LOG_ERR, "Bind failed: %s", strerror(errno));
        cleanup();
        return -1;
    }

    if (listen(server_fd, 3) == -1) {
        syslog(LOG_ERR, "Listen failed: %s", strerror(errno));
        cleanup();
        return -1;
    }

    if (daemon_mode) {
        pid_t pid = fork();
        if (pid == -1) {
            syslog(LOG_ERR, "Fork failed: %s", strerror(errno));
            cleanup();
            return -1;
        }
        if (pid > 0) exit(EXIT_SUCCESS);
        if (setsid() == -1) {
            syslog(LOG_ERR, "Setsid failed: %s", strerror(errno));
            cleanup();
            return -1;
        }
    }

    while (running) {
        client_socket = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (client_socket == -1) {
            if (errno == EINTR) continue;  // Interrupted system call
            syslog(LOG_ERR, "Accept failed: %s", strerror(errno));
            continue;
        }

        char client_ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &(address.sin_addr), client_ip, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        fd = open(FILE_PATH, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (fd == -1) {
            syslog(LOG_ERR, "Failed to open file: %s", strerror(errno));
            close(client_socket);
            continue;
        }

        int complete = 0;
        while (!complete && (bytes_read = recv(client_socket, buffer, BUFFER_SIZE - 1, 0)) > 0) {
            buffer[bytes_read] = '\0';  // Null-terminate the received data
            if (write(fd, buffer, bytes_read) == -1) {
                syslog(LOG_ERR, "Failed to write to file: %s", strerror(errno));
                break;
            }
            if (strchr(buffer, '\n') != NULL) {
                complete = 1;
            }
        }

        if (bytes_read == -1 && errno != EINTR) {
            syslog(LOG_ERR, "Error receiving data: %s", strerror(errno));
        }

        close(fd);

        fd = open(FILE_PATH, O_RDONLY);
        if (fd == -1) {
            syslog(LOG_ERR, "Failed to open file for reading: %s", strerror(errno));
            close(client_socket);
            continue;
        }

        while ((bytes_read = read(fd, buffer, BUFFER_SIZE)) > 0) {
            bytes_written = send(client_socket, buffer, bytes_read, 0);
            if (bytes_written == -1) {
                if (errno == EINTR) continue;
                syslog(LOG_ERR, "Error sending data: %s", strerror(errno));
                break;
            }
        }

        if (bytes_read == -1) {
            syslog(LOG_ERR, "Error reading file: %s", strerror(errno));
        }

        close(fd);
        close(client_socket);
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
    }

    syslog(LOG_INFO, "Caught signal, exiting");
    cleanup();
    return 0;
}