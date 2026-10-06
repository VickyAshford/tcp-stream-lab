/*
 * server.c - the "sender" half of the TCP Stream Lab.
 *
 * What it does:
 *   Waits for ONE client, then calls send() 10 times, 5 bytes ("HELLO") each.
 *   Total: 50 bytes, delivered as 10 separate application-level "messages".
 *
 * The point of the lab:
 *   The client will call recv() ONCE. How many bytes does it get back?
 *   Spoiler: the answer depends on timing, not on how many times we called send().
 *
 * Optional compile-time knobs (see README for experiments):
 *   -DSEND_DELAY_US=100000   sleep 100 ms between send() calls
 *   -DUSE_NODELAY            disable Nagle's algorithm (TCP_NODELAY)
 *
 * Build:  gcc server.c -o server
 */

#include <stdio.h>       // printf, perror
#include <stdlib.h>      // exit
#include <string.h>      // memset
#include <unistd.h>      // close, sleep, usleep
#include <arpa/inet.h>   // htons
#include <sys/socket.h>  // socket, bind, listen, accept, send, setsockopt
#include <netinet/in.h>  // struct sockaddr_in, INADDR_ANY, IPPROTO_TCP
#include <netinet/tcp.h> // TCP_NODELAY

#define PORT 9999
#define NUM_SENDS 10

// Pause between send() calls, in microseconds. 0 = fire as fast as possible.
#ifndef SEND_DELAY_US
#define SEND_DELAY_US 0
#endif

int main(void) {
    // 1. socket(): ask the kernel for a TCP socket (SOCK_STREAM = byte stream).
    //    At this point it has no address and no port - it's just a handle (fd).
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }
    printf("Socket created: fd = %d\n", server_fd);

    // The address we want to claim: "any local IP, port 9999".
    // Zero the struct first so padding/unused fields don't contain stack garbage.
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // 0.0.0.0 = listen on every local interface
    address.sin_port = htons(PORT);       // htons: host byte order -> network (big-endian)

    // SO_REUSEADDR lets us restart the server right away after Ctrl+C.
    // Without it, bind() can fail with "Address already in use" while the old
    // connection lingers in the TIME_WAIT state (up to about a minute).
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 2. bind(): attach the socket to 0.0.0.0:9999 so the kernel knows
    //    which socket should receive packets addressed to that port.
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    // 3. listen(): switch the socket to passive mode. The kernel now completes
    //    TCP handshakes for us and queues finished connections (backlog = 1).
    if (listen(server_fd, 1) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }
    printf("Listening on port %d...\n", PORT);

    // 4. accept(): block until a client connects, and get a NEW socket (client_fd)
    //    dedicated to that one connection. server_fd keeps listening.
    //    NULL, NULL = "I don't care who the client is".
    int client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) {
        perror("accept");
        close(server_fd);
        return 1;
    }
    printf("Connection with client fd (%d) was successful\n", client_fd);

#ifdef USE_NODELAY
    // Disable Nagle's algorithm: push every send() to the network immediately
    // instead of waiting for the previous data to be ACKed.
    int one = 1;
    if (setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) < 0) {
        perror("setsockopt TCP_NODELAY");
    }
    printf("TCP_NODELAY enabled\n");
#endif

    // Give the client a moment to reach its recv() call and start waiting.
    sleep(1);

    const char message[] = "HELLO"; // 5 characters (+ a '\0' we deliberately don't send)
    for (int i = 0; i < NUM_SENDS; i++) {
        // send() does NOT put a packet on the wire. It copies 5 bytes from our
        // memory into the kernel's send buffer and returns. The kernel decides
        // later when, and in how many pieces, to actually transmit them.
        if (send(client_fd, message, 5, 0) < 0) {
            perror("send");
            close(client_fd);
            close(server_fd);
            return 1;
        }

        if (SEND_DELAY_US > 0) {
            usleep(SEND_DELAY_US);
        }
    }

    // Closing the connection sends a FIN: the client's recv() will eventually
    // return 0 ("end of stream") once all the data has been read.
    close(client_fd);
    close(server_fd);

    return 0;
}