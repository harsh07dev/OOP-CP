/**
 * index.ts - Entry point for the WebSocket-to-TCP bridge service.
 */

import { loadConfig } from './config.js';
import { ChatWebSocketBridge } from './bridge.js';

async function main() {
  const config = loadConfig();

  console.log('====================================================');
  console.log('   TCP CHAT SYSTEM - WEBSOCKET BRIDGE SERVICE       ');
  console.log('====================================================');
  console.log(`Bridge listening on:   ws://${config.bridgeHost}:${config.bridgePort}`);
  console.log(`TCP chat target:       ${config.tcpHost}:${config.tcpPort}`);
  console.log(`Max wire payload size: ${config.maxPayloadSize} bytes`);
  console.log('====================================================\n');

  const bridge = new ChatWebSocketBridge(config);

  const shutdown = async (signal: string) => {
    console.log(`\n[Bridge] Received ${signal}. Shutting down gracefully...`);
    try {
      await bridge.stop();
      console.log('[Bridge] Teardown complete. Exiting.');
      process.exit(0);
    } catch (err) {
      console.error('[Bridge] Error during shutdown:', err);
      process.exit(1);
    }
  };

  process.on('SIGINT', () => shutdown('SIGINT'));
  process.on('SIGTERM', () => shutdown('SIGTERM'));

  try {
    await bridge.start();
  } catch (err) {
    console.error('[FATAL] Failed to start WebSocket bridge:', err);
    process.exit(1);
  }
}

main().catch((err) => {
  console.error('[FATAL] Unhandled rejection:', err);
  process.exit(1);
});
