#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/queue.h>
#include <pthread.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include "../aesd-char-driver/aesd_ioctl.h"


#define PORT "9000"  // the port users will be connecting to
#define BACKLOG 10   // how many pending connections queue will hold
#define BUFFER_SIZE 512
#define TS_BUFFER_SIZE 128
#define TS_INTERVAL_IN_S 10 // the interval in seconds for the timer to append timestamps to the file

#define USE_AESD_CHAR_DEVICE 1
#ifdef USE_AESD_CHAR_DEVICE
    #define DATA_FILE "/dev/aesdchar"
#else
    #define DATA_FILE "/var/tmp/aesdsocketdata"
#endif


struct list_data_s {
    pthread_t thread_connection;
    int client_fd;
    LIST_ENTRY(list_data_s) entries;
};

LIST_HEAD(listhead, list_data_s) head;

pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

#if !USE_AESD_CHAR_DEVICE
pthread_t thread_timer;
#endif

int server_fd = -1;
int running = 1;


void signal_handler(int signo) {
    syslog(LOG_INFO, "Caught signal, exiting");

    running = 0;
    if (server_fd != -1) {
        close(server_fd);
        server_fd = -1;
    }
}


int daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "Fork failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        exit(EXIT_SUCCESS); // parent exiting
    }

    // child process
    if (setsid() < 0) {
        syslog(LOG_ERR, "Failed to create a new session: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    int dev_null = open("/dev/null", O_RDWR);
    if (dev_null != -1) {
        dup2(dev_null, STDIN_FILENO);
        dup2(dev_null, STDOUT_FILENO);
        dup2(dev_null, STDERR_FILENO);
        if (dev_null > 2) close(dev_null);
    }

    return 0;
}


void cleanup_handler(void *arg) {
    struct list_data_s *datap = (struct list_data_s *)arg;
    if (datap->client_fd != -1) close(datap->client_fd);

    pthread_mutex_lock(&list_mutex);
    LIST_REMOVE(datap, entries);
    pthread_mutex_unlock(&list_mutex);
    free(datap);
}


void *handle_connection(void *arg) {
    struct list_data_s *datap = (struct list_data_s *)arg;
    int client_fd = datap->client_fd;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    pthread_cleanup_push(cleanup_handler, datap);

    int file_fd = open(DATA_FILE, O_CREAT | O_APPEND | O_RDWR, S_IRWXU | S_IRGRP | S_IROTH);
    if (file_fd == -1) {
        syslog(LOG_ERR, "Failed to open device file %s: %s", DATA_FILE, strerror(errno));
    }

    //receive data
    while (1) {
        bytes_received = recv(client_fd, buffer, BUFFER_SIZE, 0);
        if (bytes_received <= 0) {
            if (bytes_received < 0) {
                syslog(LOG_ERR, "Failed to receive data: %s", strerror(errno));
            } else {
                syslog(LOG_INFO, "Client disconnected");
            }
            break;
        }

        buffer[bytes_received] = '\0';

        // Check for AESDCHAR_IOCSEEKTO:X,Y pattern
        if (strncmp(buffer, "AESDCHAR_IOCSEEKTO:", 19) == 0) {
            unsigned int write_cmd, write_cmd_offset;
            if (sscanf(buffer + 19, "%u,%u", &write_cmd, &write_cmd_offset) == 2) {
                if (file_fd < 0) {
                    syslog(LOG_ERR, "Failed to open file for ioctl: %s", strerror(errno));
                } else {
                    struct aesd_seekto seekto;
                    seekto.write_cmd = write_cmd;
                    seekto.write_cmd_offset = write_cmd_offset;
                    if (ioctl(file_fd, AESDCHAR_IOCSEEKTO, &seekto) < 0) {
                        syslog(LOG_ERR, "Failed to perform ioctl: %s", strerror(errno));
                    }

                    char read_buf[BUFFER_SIZE];
                    ssize_t n;
                    while ((n = read(file_fd, read_buf, BUFFER_SIZE)) > 0) {
                        send(tinfo->client_fd, read_buf, n, 0);
                    }
                    close(file_fd);
                }
            } else {
                syslog(LOG_ERR, "Invalid ioctl command format from client");
            }
            continue;
        }

        pthread_mutex_lock(&file_mutex);

        if (file_fd == -1) {
            syslog(LOG_ERR, "Failed to open file for writing: %s", strerror(errno));
            pthread_mutex_unlock(&file_mutex);
            break;
        }
        if (write(file_fd, buffer, bytes_received) == -1) {
            syslog(LOG_ERR, "Failed to write to file: %s", strerror(errno));
            close(file_fd);
            pthread_mutex_unlock(&file_mutex);
            break;
        }
        close(file_fd);
        pthread_mutex_unlock(&file_mutex);

        if (buffer[bytes_received - 1] == '\n') {
            int file_fd_read = open(DATA_FILE, O_RDONLY);
            if (file_fd_read == -1) {
                syslog(LOG_ERR, "Failed to open file for reading: %s", strerror(errno));
                break;
            }
            ssize_t read_bytes;
            while ((read_bytes = read(file_fd_read, buffer, BUFFER_SIZE)) > 0) {
                if (send(client_fd, buffer, read_bytes, 0) == -1) {
                    perror("send");
                    break;
                }
            }
            close(file_fd_read);
        }
    }

    pthread_cleanup_pop(1);
    return NULL;
}


#if !USE_AESD_CHAR_DEVICE
void *append_timestamp(void *arg) {
    while (running) {

        sleep(TS_INTERVAL_IN_S);

        if (!running) break;

        char timestamp[TS_BUFFER_SIZE];
        time_t now;
        time(&now);
        struct tm *tm_info = localtime(&now);
        strftime(timestamp, TS_BUFFER_SIZE, "timestamp:%a, %d %b %Y %T %z\n", tm_info);
        
        pthread_mutex_lock(&file_mutex);

        int file_fd = open(DATA_FILE, O_WRONLY | O_APPEND | O_CREAT, 0644);
        if (file_fd == -1) {
            syslog(LOG_ERR, "Failed to open file to append timestamp: %s", strerror(errno));
            pthread_mutex_unlock(&file_mutex);
            continue;
        }

        ssize_t r = write(file_fd, timestamp, strlen(timestamp));
        if(r < 0) syslog(LOG_ERR, "Failed to append timestamp: %s", strerror(errno));

        close(file_fd);

        pthread_mutex_unlock(&file_mutex);
    }

    return NULL;
}
#endif


int main(int argc, char *argv[]) {
    openlog("aesdsocket", LOG_PID | LOG_PERROR, LOG_USER);

    int daemon_mode = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            daemon_mode = 1;
        }
    }
    
    int status;
    struct addrinfo hints;
    struct addrinfo *servinfo;  // will point to the results
    struct sockaddr_storage client_addr;    // connector's address information
    socklen_t addr_size = sizeof(client_addr);
    struct sockaddr_in *client_in = (struct sockaddr_in *)&client_addr;

    // prepare socket address structures: load up address structs with getaddrinfo():
    memset(&hints, 0, sizeof(hints));	// make sure the struct is empty
    hints.ai_family = AF_INET;			// set to AF_INET to use IPv4
    hints.ai_socktype = SOCK_STREAM;	// TCP Stream Socket
    hints.ai_flags = AI_PASSIVE;		// assign the IP address of my local host

    if ((status = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        syslog(LOG_ERR, "getaddrinfo error: %s", gai_strerror(status));
        return -1;
    }
	// servinfo now points to a linked list of 1 or more struct addrinfos
    
    // make a socket - get the file descriptor
    server_fd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    if (server_fd == -1) {
        perror("socket");
        freeaddrinfo(servinfo);
        return -1;
    }

    // lose the pesky "Address already in use" error message
    // set SO_REUSEADDR on a socket to true (1) to allow reuse of address and port
    int optval = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        perror("setsockopt");
        close(server_fd);
        freeaddrinfo(servinfo);
        return -1;
    }
    
    // bind the socket to the IP address and the port we passed into getaddrinfo()
    if (bind(server_fd, servinfo->ai_addr, servinfo->ai_addrlen) == -1) {
        perror("bind");
        close(server_fd);
        freeaddrinfo(servinfo);
        return -1;
    }
    
    freeaddrinfo(servinfo);	// all done with this structure, free the linked-list

    if (daemon_mode) {
        if (daemonize() != 0) {
            close(server_fd);
            return -1;
        }
        syslog(LOG_INFO, "Running in daemon mode");
    }
    
    if (listen(server_fd, BACKLOG) == -1) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    struct sigaction sa;
    sa.sa_handler = signal_handler;   // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

	printf("Server: waiting for connections...\n");

    LIST_INIT(&head);

#if !USE_AESD_CHAR_DEVICE
    if (pthread_create(&thread_timer, NULL, append_timestamp, NULL) != 0) {
        syslog(LOG_ERR, "Failed to create thread for timer");
    }
#endif

    while (running) { // main accept() loop
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_size);
        if (client_fd == -1) {
            if (!running) break;
            perror("accept");
            continue;
        }
        // ready to communicate on socket descriptor client_fd

        syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(client_in->sin_addr));

        struct list_data_s *datap = malloc(sizeof(struct list_data_s));
        if (!datap) {
            syslog(LOG_ERR, "Failed to allocate memory for connection data");
            close(client_fd);
            continue;
        }
        datap->client_fd = client_fd;

        pthread_mutex_lock(&list_mutex);
        LIST_INSERT_HEAD(&head, datap, entries);
        pthread_mutex_unlock(&list_mutex);

        if (pthread_create(&datap->thread_connection, NULL, handle_connection, datap) != 0) {
            syslog(LOG_ERR, "Failed to create thread for handling connection");
            close(client_fd);

            pthread_mutex_lock(&list_mutex);
            LIST_REMOVE(datap, entries);
            pthread_mutex_unlock(&list_mutex);

            free(datap);
            continue;
        }
    }

    if (server_fd != -1) close(server_fd);

#if !USE_AESD_CHAR_DEVICE
    pthread_cancel(thread_timer);
    pthread_join(thread_timer, NULL);
#endif

    pthread_mutex_lock(&list_mutex);

    struct list_data_s *entry;
    while (!LIST_EMPTY(&head)) {
        entry = LIST_FIRST(&head);
        struct list_data_s *next_entry = LIST_NEXT(entry, entries);
        pthread_cancel(entry->thread_connection);
        pthread_mutex_unlock(&list_mutex);
        pthread_join(entry->thread_connection, NULL);
        pthread_mutex_lock(&list_mutex);
        LIST_REMOVE(entry, entries);
        free(entry);
        entry = next_entry;
    }

    pthread_mutex_unlock(&list_mutex);

#if !USE_AESD_CHAR_DEVICE
    remove(DATA_FILE);
#endif

    closelog();
    return 0;
}
