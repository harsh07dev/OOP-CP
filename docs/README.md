# Documentation

## Phase 1: Basic Prototype Documentation

This directory contains architectural and design documentation for the TCP Multi-Client Chat Application.

### Modules Overview

- **Server (`server/`)**: Manages listening sockets, client connection threads, client registry, and log output.
- **Client (`client/`)**: Manages the socket connection to the server, separated console sender thread, and message listener/receiver thread.
- **Common (`common/`)**: Shared protocol definitions and message serialization formats between server and client.
- **Logs (`logs/`)**: Server execution logs tracking connection, disconnection, and message events.

## Specifications and Progress Reports

- [Protocol Specification (Phase 4)](PROTOCOL_SPECIFICATION.md): Complete structured JSON protocol schema, legacy dual-mode compatibility, and validation rules.
- [Project Progress & Phase 2 Readiness](PROJECT_PROGRESS_AND_PHASE2_READINESS.md): Historical architectural audit and reliability evaluation.
