## Packet Types 
- `SRV_CONNECT`    : Initial connection to the server.
- `SRV_DISCONNECT` : Disconnects from the server.
- `SVR_MESSAGE`    : Server wide messages from the server to client.
- `CH_CONNECT`     : Client attempts to connect to a channel.
- `CH_DISCONNECT`  : Client disconnects from a channel.
- `CH_MESSAGE`     : Client sends a message on the channel.
- `CH_COMMAND`     : Channel management commands.

## Request/Response Packet Payload

#### `SRV_CONNECT`
###### Request
- Null terminated string containing the client's desired username. 
- The username may not exceed twelve characteres.
###### Response
- Null Terminated ASCII String : Mirror of the username plus their unique identifier.

#### `SRV_DISCONNECT` 
###### Request
- Null byte may suffice. The server will not read this request type's payload.
###### Response
- No response

#### `SRV_MESSAGE`
###### Request
- 8-bit integer : Indicates the nature of the message (Info, Error, Annoucement)
- Null-terminated ASCII String : Max of 1000 bytes per message (1000 characteres).
###### Response
- No response

#### `CH_CONNECT`
###### Request
- 8-bit integer : either one or zero, indicating if the channel shall be created if not found.
- 32-bit integer : id of the target channel to join.
###### Response
- 32-bit integer : channel's id
- 32-bit integer : emperor's id
- 8-bit integer  : secret status

#### `CH_DISCONNECT`
###### Request
- 32-bit integer : id of the target channel to leave.
###### Response
- Mirror of the request header.

#### `CH_MESSAGE`
###### Request
- 32-bit integer : id of the target channel where the message will be sent.
- Null terminated ASCII String : Max of 1000 bytes per message (1000 characteres).
###### Response
- 32-bit integer : Channel identifier
- 32-bit integer : Client identifier
- Null-terminated ASCII String : broadcasted message

#### `CH_COMMAND`
- 8-bit integer : operation identifier.
  - 1 : change channel privacy
  - 2 : promote member to moderator
  - 3 : promote moderator do emperor
  - 4 : invite a member
  - 5 : kick member
  - 6 : change channel name
  - 7 : pin a message on the channel
  - 8 : destroy server
- Varies based on the operation : can be either a 32-bit integer or a Null terminated ASCII String.

## Checklist
- [x] I/O Multiplexing for incoming requests and connections.
- [x] Global thread pool to handle request handling and intensive I/O operations.
- [x] Think about and documment request/responses types and body formatting.
- [x] Architect and document application components, ownerships and connections.
- [ ] Test basic functionalities.
    - [x] Server Connection [`SRV_CONNECT`]
    - [x] Server Disconnect [`SRV_DISCONNECT`]
    - [x] Channel Connection [`CH_CONNECT`]
    - [x] Channel Messaging [`CH_MESSAGE`]
    - [ ] Channel Commands [`CH_COMMAND`]
    - [x] Channel Disconnection [`CH_DISCONNECT`]
- [ ] Test message broadcasting to multiple clients.
- [ ] Create a basic client ?
- [ ] Optimize until you get tired.

## Components Logic Overview

### Server
The server is the main component of the structure of the application, it's the first point of contact with the clients' file descriptors. It holds shared pointers to the connected clients in order to create weak pointers that will be passed down to request handlers and other functionalities from server and channels. It also holds a unique pointer to all channels created, and will be the intermediary that will link the clients and the channels. 

The server uses of epoll system to handle the connections simultaneously. Also contains a centralized thread pool that will be used across the application to handle operations concurrently.

Server holds unique pointers of [Channel] and shared pointers of [Client]

#### Request Handling
Once a client's file descriptor has an event notified in the epoll, that file descriptor will not be notified in the event pool until it is rearmed. File descriptors are only rearmed once the server has finished handling it's former request, allowing the server to proccess one request at a time in order to avoid conflicts.

### Client
Clients are stateful data structures associated with file descriptors. They hold an id, an username and the connected channel pool. (There's not authentication for client connections [29/10/2025]).

Client holds no pointers, it's a standalone structure.

### Channel
A channel is a chat-room that the clients can create and connect to. Each channel contains one emperor (the client that created the channel), up to five moderators (assigned by the emperor) and a max of one-hundred concurrent members.

Channel holds weak pointers of [Client] connected to it.

### Components Connections
- [Server] is a shared pointer. 
    - Will be passed down as a weak pointer to [Channel] in order to request it's self destruction.
- [Clients] is a shared pointer. 
    - Will be passed down to [Server] and [Channel] functions to handle requests.

- [Channel] is a unique pointer owned by [Server]
    - Will only be accessed directly by the [Server] at [Client]'s request.

    

