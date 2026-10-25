# Documentation

This directory contains the architecture, protocol, and progress documentation
for the C++ TCP Multi-Client Chat Application.

## Current documentation

- [Interactive quickstart and demo](../docs.html): Build/run steps, an animated
  two-client transcript, and the console command reference.
- [Protocol Specification](PROTOCOL_SPECIFICATION.md): Complete Structured
  Protocol v1 schema, legacy v0 compatibility, validation rules, error codes,
  private messaging, rooms, and persistent history.
- [Project Progress & Phase 2 Readiness](PROJECT_PROGRESS_AND_PHASE2_READINESS.md):
  Historical engineering audit, Phase 1/2 hardening record, and Phase 3/4
  implementation notes.
- [README](../README.md): Current build instructions, client command reference,
  feature list, and project status.

## Modules overview

- **Server (`server/`)**: Listening socket, per-client worker threads, client
  registry, room manager, SQLite message store, and logging.
- **Client (`client/`)**: Structured registration, console command handling,
  sender/receiver workers, and non-blocking console input.
- **Common (`common/`)**: Shared protocol constants, structured message
  serialization, and the zero-dependency JSON parser.
- **Logs (`logs/`)**: Server execution logs.

## Current status

Core MVP and Phase 4 server features are complete. The interactive console
client supports public chat, private messages, rooms, online user listings, and
persistent history. Authentication, heartbeat, TLS, and GUI work remain outside
the current scope.
