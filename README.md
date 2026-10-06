# 🌊 TCP Stream Lab

### *You called `send()` ten times. Why did `recv()` give you everything at once?*

A tiny C experiment that breaks one of the most common illusions in network programming:

> **"One `send()` = one `recv()`."**

It's not. **TCP is a stream of bytes, not a sequence of messages.** This lab lets you watch that happen on your own machine.

```
$ ./client
File descriptor was created successfully
Connected to server
Got data. Printing...
Read 50 bytes: HELLOHELLOHELLOHELLOHELLOHELLOHELLOHELLOHELLOHELLO     ← 10 sends, 1 recv

$ ./client
...
Read 5 bytes: HELLO                                                   ← same code, different timing
```

Same client. Same server. Different result. Let's find out why.

---

## 🧪 The experiment

| | What it does |
|---|---|
| **`server.c`** | Accepts one client, then calls `send()` **10 times**, 5 bytes (`"HELLO"`) each. 50 bytes in total. |
| **`client.c`** | Connects and calls `recv()` **exactly once** with a 4096-byte buffer. Prints how many bytes it got. |

The question: **how many bytes does that single `recv()` return?**

If TCP preserved message boundaries, the answer would always be 5. It isn't.

---

## 🚀 Run it

You need `gcc` (or `clang`) on Linux, macOS, or WSL.

```bash
gcc server.c -o server
gcc client.c -o client
```

Terminal 1:

```bash
./server
```

Terminal 2:

```bash
./client
```

The server handles one client and exits. Start it again for each run.

### Turn the knobs

The server has two compile-time switches so you can reproduce every scenario below:

```bash
# Scenario 1 (default): no delay, Nagle on
gcc server.c -o server

# Scenario 2: 100 ms pause between sends
gcc -DSEND_DELAY_US=100000 server.c -o server

# Scenario 3: Nagle's algorithm off
gcc -DUSE_NODELAY server.c -o server

# Combine them
gcc -DSEND_DELAY_US=100000 -DUSE_NODELAY server.c -o server
```

---

## 📊 What you'll see

### Scenario 1: a burst, no delay → usually **50 bytes**

The server's loop finishes in microseconds. By the time the client process wakes up and reads, all ten chunks are already sitting in its receive buffer, so one `recv()` scoops up everything.

### Scenario 2: 100 ms between sends → **5 bytes**

`recv()` returns as soon as **any** data is available. The first `"HELLO"` arrives within microseconds, the client wakes up and grabs it long before the second `send()` happens. The other 45 bytes never get read, because the client calls `recv()` only once and then exits.

### Scenario 3: Nagle's algorithm and `TCP_NODELAY`

Every TCP packet carries about 40 bytes of IP + TCP headers. Sending 5 bytes of payload that way is wasteful (think of an SSH session sending one keystroke at a time). **Nagle's algorithm** fixes this:

1. The first small chunk is sent immediately.
2. While that chunk is still un-ACKed, new small chunks are held back in a buffer.
3. When the ACK arrives (or the buffer fills up to a full segment), everything is sent together.

`TCP_NODELAY` turns this off: "send each `send()` right away, don't wait for ACKs." It's what you want for latency-sensitive traffic like games, interactive protocols, and RPC.

**But here's the twist:** even with `TCP_NODELAY` and no delay, you'll *still* often see 50 bytes. 🤯

---

## 🔍 So who decides how the bytes are grouped?

A few independent things do:

- **The sender's kernel.** `send()` only *copies* bytes into a kernel buffer and returns. When and how they go out is the kernel's call (Nagle is one of the factors).
- **The network.** Segments can be merged, split, or delayed along the way.
- **The receiver's kernel.** Arriving bytes pile up in a receive buffer. `recv()` returns whatever is in there **right now**.
- **The scheduler.** A blocked `recv()` has to be woken up and actually run. If the sender finishes its whole loop first, the receiver sees everything in one go.

That last point is why Scenario 1 is a **race**, not a guarantee. On loopback (`127.0.0.1`) there isn't even a physical network card involved. Run the client a bunch of times and you may well see numbers other than 5 and 50. That's the whole lesson: **the result is timing, not logic.**

---

## ✅ What this means for real code

Since TCP gives you a stream, **you** must define where messages begin and end:

| Technique | How it works | Used by |
|---|---|---|
| **Delimiters** | End each message with a marker such as `\r\n` or `\0` | HTTP/1.1 headers, SMTP, Redis (RESP) |
| **Length prefix** | Send the size first, then the payload | Protobuf-based RPC, WebSocket frames, most binary protocols |
| **Fixed size** | Every message is exactly N bytes | Simple custom protocols |

And you must **read in a loop** until you have a full message. A minimal helper:

```c
// Read exactly `len` bytes, no matter how TCP chops them up.
// Returns 0 on success, -1 on error or if the peer closed early.
int recv_all(int fd, void *buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        ssize_t n = recv(fd, (char *)buf + got, len - got, 0);
        if (n <= 0) return -1;   // error, or connection closed (n == 0)
        got += (size_t)n;
    }
    return 0;
}
```

Same story for `send()`: it can legally write fewer bytes than you asked for, so production code loops there too.

---

## 🧠 Bonus: socket API cheat sheet

Questions this lab tends to raise:

**Why is the order `socket → bind → listen → accept` on the server, but just `socket → connect` on the client?**
`bind` claims an address and port. `listen` flips the socket to passive mode so the kernel starts completing handshakes. `accept` hands you a *new* socket for each connection. The client doesn't need `bind`, since `connect` makes the kernel pick a random ephemeral local port.

**What is `INADDR_ANY`?**
It's `0.0.0.0`: "listen on every local interface" (Wi-Fi, Ethernet, loopback). Bind to `127.0.0.1` instead and only local processes can connect.

**Why `SO_REUSEADDR`?**
After you stop the server, the port can linger in `TIME_WAIT`, and a quick restart fails with `Address already in use`. This option lets you rebind immediately.

**Why `htons()` and `inet_pton()`?**
The network uses big-endian byte order; your x86 CPU is little-endian, so `htons` ("host to network short") converts the port. `inet_pton` parses `"127.0.0.1"` into the 4 raw bytes the kernel expects.

**Does `send()` put a packet on the wire?**
No, it copies your bytes into the kernel's send buffer and returns. The kernel transmits when it decides to.

**Is a socket a file?**
Almost. It's a file descriptor, just an index into your process's table of open files, so `read()`/`write()` work on it too. Behind that index the kernel keeps a full TCP state machine with buffers, sequence numbers, and timers.

**Can one process juggle thousands of connections?**
Yes, even single-threaded, using `epoll` (Linux). Hand the kernel a list of sockets and sleep until *any* of them has data. That's the idea behind Nginx and Node.js, and a natural next experiment.

---


## 📂 Files

```text
.
├── server.c    # sends 10 x "HELLO" with optional delay / TCP_NODELAY
├── client.c    # one recv(), prints how many bytes arrived
└── README.md
```

Built for curiosity. Issues, experiments, and weird results are very welcome. 🙂