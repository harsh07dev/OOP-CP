/**
 * config.ts - Configuration management for the WebSocket-to-TCP bridge.
 * Loads settings from environment variables and command line flags with safe defaults.
 */

export interface BridgeConfig {
  bridgeHost: string;
  bridgePort: number;
  tcpHost: string;
  tcpPort: number;
  maxPayloadSize: number;
  pingIntervalMs: number;
}

function parseArg(flag: string): string | undefined {
  const idx = process.argv.indexOf(flag);
  if (idx !== -1 && idx + 1 < process.argv.length) {
    return process.argv[idx + 1];
  }
  return undefined;
}

export function loadConfig(): BridgeConfig {
  const bridgeHost =
    parseArg('--host') ||
    process.env.BRIDGE_HOST ||
    '0.0.0.0';

  const bridgePort = parseInt(
    parseArg('--port') ||
    process.env.BRIDGE_PORT ||
    process.env.PORT ||
    '8081',
    10
  );

  const tcpHost =
    parseArg('--tcp-host') ||
    process.env.TCP_HOST ||
    '127.0.0.1';

  const tcpPort = parseInt(
    parseArg('--tcp-port') ||
    process.env.TCP_PORT ||
    '8080',
    10
  );

  const maxPayloadSize = parseInt(
    process.env.MAX_PAYLOAD_SIZE || '2048',
    10
  );

  const pingIntervalMs = parseInt(
    process.env.PING_INTERVAL_MS || '30000',
    10
  );

  return {
    bridgeHost,
    bridgePort: isNaN(bridgePort) ? 8081 : bridgePort,
    tcpHost,
    tcpPort: isNaN(tcpPort) ? 8080 : tcpPort,
    maxPayloadSize: isNaN(maxPayloadSize) ? 2048 : maxPayloadSize,
    pingIntervalMs: isNaN(pingIntervalMs) ? 30000 : pingIntervalMs,
  };
}
