# 💬 Chat Server Protocol & Architecture

## Overview

---

## Packet Types

| Type | Direction | Purpose |
|------|-----------|---------|
| `SRV_CONNECT` | Client → Server | Initial connection and authentication |
| `SRV_DISCONNECT` | Client → Server | Graceful disconnection |
| `SRV_MESSAGE` | Server → Client | Server-wide notifications and messages |
| `CH_CONNECT` | Client → Server | Join or create a channel |
| `CH_DISCONNECT` | Client → Server | Leave a channel |
| `CH_MESSAGE` | Client ↔ Server | Send/broadcast messages in a channel |
| `CH_COMMAND` | Client → Server | Perform channel management operations |

---

## 🔌 Protocol Specification

### SRV_CONNECT
Establishes a client connection and registers a username.

**Request:**
- Null-terminated ASCII string (max 12 characters): desired username

**Response:**
- Null-terminated ASCII string: username + unique client identifier

---

### SRV_DISCONNECT
Terminates the connection gracefully.

**Request:**
- Null byte or empty payload

**Response:**
- None

---

### SRV_MESSAGE
Server sends notifications or messages to a client.

**Request:**
- 8-bit integer: message type (Info, Error, Announcement)
- Null-terminated ASCII string (max 1000 bytes): message content

**Response:**
- None

---

### CH_CONNECT
Join an existing channel or create a new one.

**Request:**
- 8-bit integer: creation flag (1 = create if not exists, 0 = join only)
- 32-bit integer: target channel ID

**Response:**
- 32-bit integer: channel ID
- 32-bit integer: emperor ID (channel creator)
- 8-bit integer: privacy status (secret/public)

---

### CH_DISCONNECT
Leave a channel.

**Request:**
- 32-bit integer: channel ID

**Response:**
- 32-bit integer: channel ID (mirror of request)

---

### CH_MESSAGE
Send a message to a channel.

**Request:**
- 32-bit integer: target channel ID
- Null-terminated ASCII string (max 1000 bytes): message content

**Response:**
- 32-bit integer: channel ID
- 32-bit integer: sender client ID
- Null-terminated ASCII string: broadcasted message

---

### CH_COMMAND
Execute channel management operations.

**Request:**
- 8-bit integer: operation code
  - `1`: Change channel privacy
  - `2`: Promote member to moderator
  - `3`: Promote moderator to emperor
  - `4`: Invite a member
  - `5`: Kick member
  - `6`: Change channel name
  - `7`: Pin a message
  - `8`: Destroy server
- Variable payload: 32-bit integer or null-terminated ASCII string (depends on operation)

---

## 🏗️ Architecture

### 🖥️ Server
The main component that manages all client connections and channels.

**Responsibilities:**
- Accepts and manages client connections via epoll
- Routes requests to appropriate handlers
- Manages the global thread pool for concurrent operations
- Owns all channel instances and maintains client references

**Key Properties:**
- Uses epoll for efficient I/O multiplexing
- One-request-at-a-time processing per client (sequential per FD)
- Centralized thread pool for async operations
- Owns unique pointers to channels
- Owns shared pointers to clients

**Request Handling:**
File descriptors are only rearmed in the epoll event pool after the current request is fully processed. This ensures sequential request handling per client and prevents race conditions.

---

### Client
Stateless connection wrapper associated with a file descriptor.

**Properties:**
- Unique client ID
- Username (max 12 characters)
- Connected channels list
- No authentication required (as of 10/29/2025)
- Standalone data structure with no pointer ownership

---

### Channel
A chat room where clients can exchange messages with role-based permissions.

**Properties:**
- Unique channel ID
- **Emperor**: The client who created the channel (1 per channel)
- **Moderators**: Privileged members (max 5 per channel)
- **Members**: Regular connected clients (max 100 per channel)
- Privacy status (public/secret)

**Relationships:**
- Holds weak pointers to connected clients
- Can request server self-destruction through weak server pointer

---

## Component Relationships

```
Server (shared pointer)
  ├─ Channels (unique pointers, owned by Server)
  ├─ Clients (shared pointers)
  └─ Thread Pool (global)

Client (shared pointer)
  └─ Passes as reference to Server and Channel handlers

Channel (unique pointer)
  └─ Holds weak pointers to Clients
  └─ Holds weak pointer to Server (for thread-pool access)
```

---

## Implementation Status

### Completed
- [x] I/O multiplexing with epoll
- [x] Global thread pool for concurrent request handling
- [x] Request/response protocol specification
- [x] Architecture design and documentation
- [x] Server connection handling (`SRV_CONNECT`)
- [x] Server disconnection (`SRV_DISCONNECT`)
- [x] Channel connection (`CH_CONNECT`)
- [x] Channel messaging (`CH_MESSAGE`)
- [x] Channel disconnection (`CH_DISCONNECT`)
- [x] Multi-client message broadcasting

### In Progress / Todo
- [ ] Channel command operations (`CH_COMMAND`)
- [ ] Basic client application
- [ ] Performance optimization

---

## Technical Notes

- **No Authentication**: Clients are identified only by username and ID
- **Non-blocking**: All I/O operations are non-blocking via epoll
- **Thread-safe**: Thread pool handles concurrent operations safely
- **Sequential Processing**: Per-client request serialization prevents conflicts
- **Memory Management**: Smart pointers ensure proper resource cleanup
