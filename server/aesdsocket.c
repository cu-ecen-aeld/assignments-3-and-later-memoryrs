/***************************************************************
* FILENAME: aesdsocket.c
* DESCRIPTION: This aesdsocket implements a native TCP/IP stream socket server using `getaddrinfo`, which is a newer approach
* and more flexible as it works for both IPv4 and IPv6, does DNS and service name lookups, and fills out the structs you need
* DATE: 08/06/2025
* REFERENCE: https://beej.us/guide/bgnet/html/
****************************************************************/

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

#define PORT "9000"  // the port users will be connecting to
#define BACKLOG 10	 // how many pending connections queue will hold
#define BUFFER_SIZE 512
#define DATA_FILE "/var/tmp/aesdsocketdata"


int server_fd = -1, client_fd = -1;

void signal_handler(int s)
{
    syslog(LOG_INFO, "Caught signal, exiting");

    if (client_fd != -1) {
        close(client_fd);
    }
    if (server_fd != -1) {
        close(server_fd);
    }

    remove(DATA_FILE);
    closelog();
    exit(EXIT_SUCCESS);
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


int main(int argc, char *argv[]) 
{
	int status;
	struct addrinfo hints;
	struct addrinfo *servinfo;  // will point to the results
    struct sockaddr_storage client_addr;	// connector's address information
    socklen_t addr_size = sizeof(client_addr);
	struct sockaddr_in *client_in = (struct sockaddr_in *)&client_addr;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

	// parse arguments to check for -d option
    int daemon_mode = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            daemon_mode = 1;
        }
    }

    // set up signal handling
    struct sigaction sa;
    sa.sa_handler = signal_handler;	 // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    openlog("aesdsocket", LOG_PID | LOG_PERROR, LOG_USER);

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

    // checking for Daemon after binding if -d option was specified
    if (daemon_mode) {
        if (daemonize() != 0) {
            close(server_fd);
            return -1;
        }
        syslog(LOG_INFO, "Running in daemon mode");
    }
    
    // listen for connections
    if (listen(server_fd, BACKLOG) == -1) {
		perror("listen");
        close(server_fd);
        return -1;
    }

	printf("Server: waiting for connections...\n");
    
    // main server loop to handle incoming connections
    while (1) { // main accept() loop
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_size);
        if (client_fd == -1) {
			perror("accept");
            continue;
        }
		// ready to communicate on socket descriptor client_fd

        syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(client_in->sin_addr));

        // open file for appending
        int file_fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (file_fd == -1) {
            syslog(LOG_ERR, "Failed to open file");
            close(client_fd);
            continue;
        }

		// receives data from client and appends to file
        while ((bytes_received = recv(client_fd, buffer, BUFFER_SIZE, 0)) > 0) {
            if (write(file_fd, buffer, bytes_received) == -1) {
                syslog(LOG_ERR, "Failed to write to file %s", DATA_FILE);
                break;
            }

            // check for end of packet based on '\n' as the delimiter
            if (buffer[bytes_received - 1] == '\n') {
                break;
            }
        }
        
        close(file_fd);	// close file after writing

        // open the file for reading
        file_fd = open(DATA_FILE, O_RDONLY);
        if (file_fd == -1) {
            syslog(LOG_ERR, "Failed to open file for reading");
            close(client_fd);
            continue;
        }

		// read the full content of the file and send it back to the client
        while ((bytes_received = read(file_fd, buffer, BUFFER_SIZE)) > 0) {
            send(client_fd, buffer, bytes_received, 0);
        }

        close(file_fd);	// close file after reading
        close(client_fd);	// close client connection after getting line
		syslog(LOG_INFO, "Closed connection from %s", inet_ntoa(client_in->sin_addr));
    }
    
    closelog();	// close server sockets
    close(server_fd);
    return 0;
}
