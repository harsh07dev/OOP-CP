/**
 * bridge.ts - High-performance, transparent WebSocket-to-TCP bridge.
 * Maps every browser WebSocket connection 1:1 to an independent TCP client socket,
 * enforcing LF framing ('\n'), wire-size constraints, and clean session lifecycle.
 */

import http from 'http';
import net from 'net';
import { WebSocket, WebSocketServer, RawData } from 'ws';
import { BridgeConfig } from './config.js';

interface ClientSession {
  id: string;
  ws: WebSocket;
  tcpSocket: net.Socket;
  tcpConnected: boolean;
  tcpBuffer: string;
  pendingTcpWrites: string[];
  remoteAddress: string;
  connectedAt: Date;
}

export class ChatWebSocketBridge {
  private config: BridgeConfig;
  private httpServer: http.Server | null = null;
  private wss: WebSocketServer | null = null;
  private sessions: Map<string, ClientSession> = new Map();
  private sessionCounter: number = 0;
  private pingIntervalTimer: NodeJS.Timeout | null = null;
  private isShuttingDown: boolean = false;

  constructor(config: BridgeConfig) {
    this.config = config;
  }

  /**
   * Starts the HTTP and WebSocket bridge server.
   */
  public async start(): Promise<void> {
    return new Promise((resolve, reject) => {
      // Create HTTP server for health checking and WebSocket upgrade
      this.httpServer = http.createServer((req, res) => {
        // Simple health and readiness endpoint
        if (req.url === '/health' || req.url === '/') {
          res.writeHead(200, { 'Content-Type': 'application/json' });
          res.end(
            JSON.stringify({
              status: 'ok',
              activeSessions: this.sessions.size,
              tcpTarget: `${this.config.tcpHost}:${this.config.tcpPort}`,
              timestamp: new Date().toISOString(),
            })
          );
          return;
        }

        res.writeHead(404, { 'Content-Type': 'text/plain' });
        res.end('Not Found');
      });

      this.wss = new WebSocketServer({
        server: this.httpServer,
        maxPayload: this.config.maxPayloadSize * 2, // Allow reasonable envelope overhead before rejection
      });

      this.wss.on('connection', (ws: WebSocket, req: http.IncomingMessage) => {
        this.handleNewWebSocketConnection(ws, req);
      });

      this.wss.on('error', (err: Error) => {
        console.error('[Bridge] WebSocket server error:', err.message);
      });

      // Start ping heartbeat timer
      if (this.config.pingIntervalMs > 0) {
        this.pingIntervalTimer = setInterval(() => {
          this.checkHeartbeats();
        }, this.config.pingIntervalMs);
      }

      this.httpServer.on('error', (err: Error) => {
        reject(err);
      });

      this.httpServer.listen(this.config.bridgePort, this.config.bridgeHost, () => {
        console.log(
          `[Bridge] WebSocket bridge listening on ws://${this.config.bridgeHost}:${this.config.bridgePort}`
        );
        console.log(
          `[Bridge] Forwarding 1:1 sessions to TCP server at ${this.config.tcpHost}:${this.config.tcpPort}`
        );
        resolve();
      });
    });
  }

  /**
   * Gracefully shuts down the bridge server and terminates all active TCP and WS connections.
   */
  public async stop(): Promise<void> {
    this.isShuttingDown = true;

    if (this.pingIntervalTimer) {
      clearInterval(this.pingIntervalTimer);
      this.pingIntervalTimer = null;
    }

    // Close all active sessions
    for (const session of this.sessions.values()) {
      this.destroySession(session, 'Bridge shutting down');
    }
    this.sessions.clear();

    return new Promise((resolve) => {
      if (this.wss) {
        this.wss.close(() => {
          if (this.httpServer) {
            this.httpServer.close(() => resolve());
          } else {
            resolve();
          }
        });
      } else if (this.httpServer) {
        this.httpServer.close(() => resolve());
      } else {
        resolve();
      }
    });
  }

  public getActiveSessionCount(): number {
    return this.sessions.size;
  }

  /**
   * Handles a new incoming browser WebSocket connection by spawning a dedicated TCP socket.
   */
  private handleNewWebSocketConnection(ws: WebSocket, req: http.IncomingMessage): void {
    if (this.isShuttingDown) {
      ws.close(1001, 'Bridge is shutting down');
      return;
    }

    const sessionId = `ws-session-${++this.sessionCounter}`;
    const remoteAddr = req.socket.remoteAddress || 'unknown';

    // Establish a dedicated TCP socket connection to the C++ server
    const tcpSocket = new net.Socket();
    tcpSocket.setNoDelay(true);

    const session: ClientSession = {
      id: sessionId,
      ws,
      tcpSocket,
      tcpConnected: false,
      tcpBuffer: '',
      pendingTcpWrites: [],
      remoteAddress: remoteAddr,
      connectedAt: new Date(),
    };

    this.sessions.set(sessionId, session);
    console.log(`[Bridge] [${sessionId}] New connection from ${remoteAddr}. Connecting to TCP ${this.config.tcpHost}:${this.config.tcpPort}...`);

    // Connect to TCP server
    tcpSocket.connect(this.config.tcpPort, this.config.tcpHost, () => {
      session.tcpConnected = true;
      console.log(`[Bridge] [${sessionId}] Connected to TCP chat server.`);

      // Flush any queued writes that arrived before TCP connect completed
      while (session.pendingTcpWrites.length > 0) {
        const payload = session.pendingTcpWrites.shift();
        if (payload) {
          tcpSocket.write(payload);
        }
      }
    });

    // Handle incoming data from the C++ TCP Server
    tcpSocket.on('data', (chunk: Buffer) => {
      session.tcpBuffer += chunk.toString('utf-8');

      // Extract all complete newline-delimited frames
      let newlineIdx: number;
      while ((newlineIdx = session.tcpBuffer.indexOf('\n')) !== -1) {
        let line = session.tcpBuffer.slice(0, newlineIdx);
        session.tcpBuffer = session.tcpBuffer.slice(newlineIdx + 1);

        // Strip trailing carriage return if present
        if (line.endsWith('\r')) {
          line = line.slice(0, -1);
        }

        if (line.length === 0) {
          continue;
        }

        // Forward to the browser WebSocket client
        if (ws.readyState === WebSocket.OPEN) {
          ws.send(line);
        }
      }
    });

    // Handle TCP connection closure by server
    tcpSocket.on('close', (hadError: boolean) => {
      console.log(`[Bridge] [${sessionId}] TCP connection closed (hadError: ${hadError}).`);
      if (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING) {
        ws.close(1000, 'TCP backend closed connection');
      }
      this.sessions.delete(sessionId);
    });

    // Handle TCP socket errors
    tcpSocket.on('error', (err: Error) => {
      console.error(`[Bridge] [${sessionId}] TCP socket error:`, err.message);
      if (ws.readyState === WebSocket.OPEN) {
        // Send a structured error before closing if possible
        const errPayload = JSON.stringify({
          version: 1,
          type: 'error',
          code: 'ERR_BACKEND_UNAVAILABLE',
          detail: `TCP chat server connection error: ${err.message}`,
          status: 'error',
        });
        ws.send(errPayload);
      }
      this.destroySession(session, 'TCP error');
    });

    // Handle incoming messages from the Browser WebSocket client
    ws.on('message', (data: RawData) => {
      let messageStr: string;
      if (typeof data === 'string') {
        messageStr = data;
      } else if (Buffer.isBuffer(data)) {
        messageStr = data.toString('utf-8');
      } else if (Array.isArray(data)) {
        messageStr = Buffer.concat(data).toString('utf-8');
      } else {
        messageStr = Buffer.from(data).toString('utf-8');
      }

      // Trim outer whitespace
      messageStr = messageStr.trim();
      if (!messageStr) {
        return;
      }

      // Enforce wire limit invariant (2048 bytes with trailing newline)
      const wireSize = Buffer.byteLength(messageStr, 'utf-8') + 1; // +1 for '\n'
      if (wireSize > this.config.maxPayloadSize) {
        console.warn(
          `[Bridge] [${sessionId}] Rejected oversized message from browser (${wireSize} > ${this.config.maxPayloadSize} bytes).`
        );
        if (ws.readyState === WebSocket.OPEN) {
          ws.send(
            JSON.stringify({
              version: 1,
              type: 'error',
              code: 'ERR_MESSAGE_TOO_LARGE',
              detail: `Message wire size (${wireSize} bytes) exceeds limit of ${this.config.maxPayloadSize} bytes`,
              status: 'error',
            })
          );
        }
        return;
      }

      // Format with newline delimiter for wire framing
      const framedData = messageStr + '\n';

      if (session.tcpConnected) {
        tcpSocket.write(framedData);
      } else {
        // Buffer until TCP connects
        session.pendingTcpWrites.push(framedData);
      }
    });

    // Handle WebSocket close from browser
    ws.on('close', (code: number, reason: Buffer) => {
      console.log(
        `[Bridge] [${sessionId}] Browser WebSocket closed (${code}: ${reason.toString() || 'normal'}).`
      );
      this.destroySession(session, 'WebSocket closed');
    });

    // Handle WebSocket errors
    ws.on('error', (err: Error) => {
      console.error(`[Bridge] [${sessionId}] Browser WebSocket error:`, err.message);
      this.destroySession(session, 'WebSocket error');
    });
  }

  /**
   * Cleanly closes both ends of a session and removes it from the registry.
   */
  private destroySession(session: ClientSession, reason: string): void {
    this.sessions.delete(session.id);

    try {
      if (!session.tcpSocket.destroyed) {
        session.tcpSocket.destroy();
      }
    } catch {
      // Ignore already closed socket
    }

    try {
      if (
        session.ws.readyState === WebSocket.OPEN ||
        session.ws.readyState === WebSocket.CONNECTING
      ) {
        session.ws.close(1000, reason);
      }
    } catch {
      // Ignore already closed WS
    }
  }

  /**
   * Pings all active WebSockets to keep connections alive and prune dead sockets.
   */
  private checkHeartbeats(): void {
    for (const session of this.sessions.values()) {
      if (session.ws.readyState === WebSocket.OPEN) {
        session.ws.ping();
      }
    }
  }
}
