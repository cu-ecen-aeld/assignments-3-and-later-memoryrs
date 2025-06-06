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

#define PORT 9000	// the port users will be connecting to
#define BACKLOG 10 	// how many pending connections queue will hold
#define BUFFER_SIZE 512
#define DATA_FILE "/var/tmp/aesdsocketdata"

int server_fd = -1, client_fd = -1;

void signal_handler(int s) {
	if (s == SIGINT || s == SIGTERM) {
		syslog(LOG_INFO, "Caught signal, exiting");
		syslog(LOG_INFO, "Closed connection from %d\n", PORT);

		if (client_fd != -1) {
			shutdown(client_fd, SHUT_RDWR);
			close(client_fd);
		}
		if (server_fd != -1) {
			shutdown(server_fd, SHUT_RDWR);
			close(server_fd);
		}
	
		remove(DATA_FILE);
		closelog();
		exit(EXIT_SUCCESS);
	}
}

int daemonize() {
	pid_t pid = fork();
	if (pid < 0) {
		syslog(LOG_ERR, "Fork failed: %s", strerror(errno));
		exit(EXIT_FAILURE);
	} else if (pid > 0) {
		exit(EXIT_SUCCESS);	// Parent exiting
	}

	// Child process
	if (setsid() == -1) {
		syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
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

int main(int argc, char *argv[]) {
	struct sockaddr_in server_addr, client_addr;
	socklen_t client_addr_len = sizeof(client_addr);
	char buffer[BUFFER_SIZE];
	ssize_t bytes_received, bytes_read;

	int daemon_mode = 0;
	if (argc == 2 && strcmp(argv[1], "-d") == 0) {
		daemon_mode = 1;
	}

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	openlog("aesdsocket", LOG_PID, LOG_USER);

	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		syslog(LOG_ERR, "Failed to create socket: %s", strerror(errno));
		return -1;
	}

	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(PORT);

	int opt = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
		syslog(LOG_ERR, "Failed to set options on socket: %s", strerror(errno));
		close(server_fd);
		return -1;
	}

	if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
		syslog(LOG_ERR, "Failed to bind socket: %s", strerror(errno));
		close(server_fd);
		return -1;
	}

	if (daemon_mode) {
        if (daemonize() < 0) {
            close(server_fd);
            return -1;
        }
        syslog(LOG_INFO, "Running in daemon mode");
    }

	if (listen(server_fd, BACKLOG) == -1) {
		syslog(LOG_ERR, "Failed to listen on socket: %s", strerror(errno));
		close(server_fd);
		return -1;
	}

	syslog(LOG_INFO, "Server start listening on port %d", PORT);

	while(1) {
		client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
		if (client_fd == -1) {
			syslog(LOG_ERR, "Failed to accept connection: %s", strerror(errno));
			close(server_fd);
			return -1;
		}

		char client_ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
		syslog(LOG_INFO, "Accepted connection from %s", client_ip);

		int fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
		if (fd == -1) {
			syslog(LOG_ERR, "Failed to open file: %s", strerror(errno));
			close(client_fd);
			close(server_fd);
			return -1;
		}

		while ((bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0)) > 0) {
			buffer[bytes_received] = '\0';
			if (write(fd, buffer, bytes_received) == -1) {
				syslog(LOG_ERR, "Failed to write to file: %s", strerror(errno));
				break;
			}

			if (strchr(buffer, '\n') != NULL) {
				break;
			}
		}

		if (bytes_received == -1) {
			syslog(LOG_ERR, "Failed to receive: %s", strerror(errno));
		}

		close(fd);
	
		fd = open(DATA_FILE, O_RDONLY);
		if (fd == -1) {
			syslog(LOG_ERR, "Failed to open data file: %s", strerror(errno));
			close(client_fd);
			close(server_fd);
			return -1;
		}

		while((bytes_read = read(fd, buffer, BUFFER_SIZE)) > 0) {
			if (send(client_fd, buffer, bytes_read, 0) == -1) {
				syslog(LOG_ERR, "Failed to send: %s", strerror(errno));
				break;
			}
		}
		close(fd);
		close(client_fd);
	}

	close(server_fd);
	closelog();
	return 0;
}