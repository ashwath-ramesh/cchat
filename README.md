# cchat

Minimal chat server built in C using poll() for I/O multiplexing, with WebSocket bridge and web clients.

## Core Features

### Server (C)

- [x] Non-blocking TCP server using poll() multiplexing
- [x] Dynamic connection pool (with hard limit of connections)
- [x] Partial send() handling with retry logic
- [x] Broadcast messaging to all connected clients
- [x] Connection/disconnection announcements
- [ ] Message timestamps
- [ ] Heartbeat: Online/last-seen per user
- [x] Message length caps
- [x] Back-pressure handling for slow client-handling
- [ ] Graceful shutdown on SIGINT/SIGTERM
- [ ] Observability (metrics + structured logs)

### WebSocket Bridge (Node.js)

- [x] Bidirectional TCP to WebSocket proxy

### Web Client (HTML/JavaScript)

- [x] Browser-based chat interface
- [x] Plain text messages
- [x] Single room for all clients
- [ ] Typing indicator

### WebSocket Server (C) - In Development

- [x] WebSocket handshake parser (RFC 6455)
- [x] SHA-1 + Base64 for Sec-WebSocket-Accept generation
- [x] HTTP 101 Switching Protocols response
- [x] WebSocket frame decoder (text/binary)
- [x] WebSocket frame encoder (server→client)
- [x] Extended payload length support (126, 127)
- [x] Masking/unmasking support
- [ ] Integration with poll() event loop
- [ ] Connection to TCP chat server (port 3490)
- [ ] Message broadcasting through WebSocket
- [ ] Ping/Pong heartbeat
- [ ] Graceful close handshake
- [ ] Multiple concurrent browser connections

## Architecture

### Current (Node.js Bridge) - Production Ready

```
[Browser Client] <--WebSocket--> [Node.js Bridge] <--TCP--> [C Server]
```

### Target (C WebSocket Server) - Core Protocol Complete, Integration In Progress

**Development Setup:**

```
[Browser] <--WebSocket--> [C WebSocket Server] <--TCP--> [C Chat Server]
          ws://localhost:8080    (port 8080)         (port 3490)
                                 - Handshake parser
                                 - Frame encoder/decoder
                                 - Protocol translator
```

**Production Setup (with Caddy):**

```
[Browser] <--HTTPS--> [Caddy Reverse Proxy] <--WebSocket--> [C WebSocket Server] <--TCP--> [C Chat Server]
          wss://                             ws://localhost:8080    (port 8080)         (port 3490)
                                                                    - Handles multiple browsers
                                                                    - One TCP conn between WS and Server
                                                                    - Translates WebSocket ↔ plain text
```

## Quick Start

### Prerequisites

- C compiler (gcc/clang)
- Node.js (v14+)
- Modern browser with WebSocket support

### Running the Server

**Option 1: Using Node.js WebSocket Bridge (Current)**

```bash
# Build and run C server
make build-server
make run-server

# In separate terminal: run WebSocket bridge
make run-bridge

# Open web client
make open-client
```

**Option 2: Using C WebSocket Server (In Development)**

```bash
# Build and run C WebSocket server
make build-ws
make run-ws

# Open web client (configured for ws://localhost:8080)
make open-client
```

## Configuration

Environment variables:

- `SERVER_HOST` - TCP server hostname (default: localhost)
- `SERVER_PORT` - TCP server port (default: 3490)
- `WS_PORT` - WebSocket bridge port (default: 8080)
- `MAX_CLIENTS` - Maximum concurrent connections (default: 100)
