/*
 * client.c - the "receiver" half of the TCP Stream Lab.
 *
 * What it does:
 *   Connects to the server on 127.0.0.1:9999 and calls recv() exactly ONCE
 *   with a big (4096-byte) buffer, then prints whatever it got.
 *
 * The server sent 10 x 5 bytes. Naive expectation: "I'll get 5 bytes."
 * Reality: you get whatever has already arrived in the socket's receive buffer
 * at the moment recv() returns - anywhere from 5 to 50 bytes.
 *
 * Build:  gcc client.c -o client
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 9999

int main(void) {
    // 1. socket(): create a TCP socket. No bind() needed on the client side -
    //    connect() will make the kernel pick a random free "ephemeral" local port.
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) {
        perror("socket");
        return 1;
    }
    printf("File descriptor was created successfully\n");

    // This is the address of the SERVER we want to reach (not our own address).
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT); // host -> network byte order

    // inet_pton ("presentation to network") converts the text "127.0.0.1"
    // into the 4 raw bytes [127, 0, 0, 1] that the kernel expects.
    if (inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) != 1) {
        fprintf(stderr, "inet_pton: invalid address\n");
        close(client_fd);
        return 1;
    }

    // 2. connect(): start the TCP 3-way handshake (SYN -> SYN/ACK -> ACK).
    //    The client always initiates; the server just accept()s.
    if (connect(client_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("connect");
        close(client_fd);
        return 1;
    }
    printf("Connected to server\n");

    // Zero the buffer so that printf("%s") later stops right after the data.
    // (A stack buffer is full of leftover garbage until we clear it.)
    // We pass sizeof(buffer) - 1 to recv() below so there is always room
    // for a terminating '\0'.
    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));

    // 3. The star of the show: ONE recv() call.
    //    It blocks until at least 1 byte is available, then returns immediately
    //    with whatever is in the receive buffer (up to the size we asked for).
    //    It does NOT wait for "all 50 bytes" or "one full message".
    ssize_t bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if (bytes < 0) {
        perror("recv");
        close(client_fd);
        return 1;
    }
    if (bytes == 0) {
        // 0 means the server closed the connection before sending anything.
        printf("Server closed the connection without sending data\n");
        close(client_fd);
        return 0;
    }

    printf("Got data. Printing...\n");
    // %zd is the printf format for ssize_t.
    printf("Read %zd bytes: %s\n", bytes, buffer);

    // Whatever is still in flight / buffered is simply thrown away when we close.
    // To read the rest, we'd have to call recv() again (in a loop).
    close(client_fd);
    return 0;
}