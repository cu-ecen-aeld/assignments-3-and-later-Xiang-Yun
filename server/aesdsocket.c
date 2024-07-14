#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h> // Include this for umask

#define PORT 9000
#define BACKLOG 10
#define FILENAME "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

int sockfd;
int filefd;

void signal_handler(int sig) {
    (void)sig; // Mark sig as unused to avoid the warning
    syslog(LOG_INFO, "Caught signal, exiting");
    close(sockfd);
    close(filefd);
    remove(FILENAME);
    closelog();
    exit(0);
}

void setup_signal_handler() {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) {
        exit(EXIT_FAILURE);
    }

    pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    umask(0);

    if (chdir("/") < 0) {
        exit(EXIT_FAILURE);
    }

    for (int fd = sysconf(_SC_OPEN_MAX); fd > 0; fd--) {
        close(fd);
    }

    openlog("aesdsocket", LOG_PID, LOG_DAEMON);
}

int main(int argc, char *argv[]) {
    int opt;
    int daemon_mode = 0;

    while ((opt = getopt(argc, argv, "d")) != -1) {
        switch (opt) {
            case 'd':
                daemon_mode = 1;
                break;
            default:
                fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (daemon_mode) {
        daemonize();
    } else {
        openlog("aesdsocket", LOG_PID, LOG_USER);
    }

    setup_signal_handler();

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        syslog(LOG_ERR, "Error creating socket: %s", strerror(errno));
        return -1;
    }

    // Bind socket
    struct sockaddr_in server_addr, client_addr;
    socklen_t sin_size;
    char buffer[BUFFER_SIZE];
    ssize_t num_bytes;
    int new_fd; // Declare new_fd here

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    memset(&(server_addr.sin_zero), '\0', 8);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1) {
        syslog(LOG_ERR, "Error binding socket: %s", strerror(errno));
        close(sockfd);
        return -1;
    }

    // Listen on socket
    if (listen(sockfd, BACKLOG) == -1) {
        syslog(LOG_ERR, "Error listening on socket: %s", strerror(errno));
        close(sockfd);
        return -1;
    }

    while (1) {
        sin_size = sizeof(struct sockaddr_in);
        if ((new_fd = accept(sockfd, (struct sockaddr *)&client_addr, &sin_size)) == -1) {
            syslog(LOG_ERR, "Error accepting connection: %s", strerror(errno));
            continue;
        }

        syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(client_addr.sin_addr));

        // Open the file for appending data
        if ((filefd = open(FILENAME, O_RDWR | O_CREAT | O_APPEND, 0644)) == -1) {
            syslog(LOG_ERR, "Error opening file: %s", strerror(errno));
            close(new_fd);
            continue;
        }

        while ((num_bytes = recv(new_fd, buffer, BUFFER_SIZE, 0)) > 0) {
            buffer[num_bytes] = '\0'; // Null-terminate the buffer
            if (write(filefd, buffer, num_bytes) != num_bytes) {
                syslog(LOG_ERR, "Error writing to file: %s", strerror(errno));
                break;
            }

            // If a newline is received, send the file contents back to the client
            if (strchr(buffer, '\n')) {
                lseek(filefd, 0, SEEK_SET); // Rewind the file
                while ((num_bytes = read(filefd, buffer, BUFFER_SIZE)) > 0) {
                    if (send(new_fd, buffer, num_bytes, 0) != num_bytes) {
                        syslog(LOG_ERR, "Error sending data to client: %s", strerror(errno));
                        break;
                    }
                }
            }
        }

        syslog(LOG_INFO, "Closed connection from %s", inet_ntoa(client_addr.sin_addr));
        close(new_fd);
        close(filefd);
    }

    close(sockfd);
    return 0;
}
