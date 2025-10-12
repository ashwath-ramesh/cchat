# cchat

Minimal chat server built in C using poll() for I/O multiplexing, with WebSocket bridge and web clients.

## Core Features

### Server (C)

- Non-blocking TCP server using poll() multiplexing
- Dynamic connection pool (with hard limit of connections)
- Partial send() handling with retry logic
- Broadcast messaging to all connected clients
- Connection/disconnection announcements
- Message timestamps
- Heartbeat: Online/last-seen per user
- Message length caps
- Back-pressure handling for slow client-handling
- Graceful shutdown on SIGINT/SIGTERM
- Observability (metrics + structured logs)

### WebSocket Bridge (Node.js)

- Bidirectional TCP to WebSocket proxy

### Web Client (HTML/JavaScript)

- Browser-based chat interface
- Plain text messages
- Single room for all clients
- Typing indicator

## Architecture

```
[Browser Client] <--WebSocket--> [Node.js Bridge] <--TCP--> [C Server]
```

## Quick Start

### Prerequisites

- C compiler (gcc/clang)
- Node.js (v14+)
- Modern browser with WebSocket support

### Running the Server

```bash
# Build and run C server
make build-server
make run-server

# In separate terminal: run WebSocket bridge
make run-bridge

# Open web client
make open-client
```

## Configuration

Environment variables:

- `SERVER_HOST` - TCP server hostname (default: localhost)
- `SERVER_PORT` - TCP server port (default: 3490)
- `WS_PORT` - WebSocket bridge port (default: 8080)
- `MAX_CLIENTS` - Maximum concurrent connections (default: 100)
