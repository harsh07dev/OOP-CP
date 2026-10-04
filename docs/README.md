# Documentation

## Phase 1: Basic Prototype Documentation

This directory contains architectural and design documentation for the TCP Multi-Client Chat Application.

### Modules Overview

- **Server (`server/`)**: Manages listening sockets, client connection threads, client registry, and log output.
- **Client (`client/`)**: Manages the socket connection to the server, separated console sender thread, and message listener/receiver thread.
- **Common (`common/`)**: Shared protocol definitions and message serialization formats between server and client.
- **Logs (`logs/`)**: Server execution logs tracking connection, disconnection, and message events.
