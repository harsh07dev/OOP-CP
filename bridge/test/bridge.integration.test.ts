/**
 * bridge.integration.test.ts
 *
 * End-to-end integration tests proving:
 * 1. Multiple browser WebSocket sessions maintain distinct sessions & separate identities.
 * 2. Proper TCP framing, correlation IDs, ACKs, events, direct messages, rooms, and presence.
 * 3. Oversized payload handling, duplicate username rejection, and clean disconnect propagation.
 */

import { spawn, ChildProcess } from 'child_process';
import path from 'path';
import fs from 'fs';
import { WebSocket } from 'ws';
import { ChatWebSocketBridge } from '../src/bridge.js';
import { BridgeConfig } from '../src/config.js';

const TCP_PORT = 8090;
const BRIDGE_PORT = 8091;

function findServerExe(): string {
  const candidates = [
    path.resolve(process.cwd(), 'build', 'chat_server.exe'),
    path.resolve(process.cwd(), '..', 'build', 'chat_server.exe'),
    path.resolve(process.cwd(), 'chat_server.exe'),
    path.resolve(process.cwd(), '..', 'chat_server.exe'),
  ];
  for (const c of candidates) {
    if (fs.existsSync(c)) {
      return c;
    }
  }
  return candidates[0];
}

const SERVER_EXE = findServerExe();
const DB_FILE = path.resolve(path.dirname(SERVER_EXE), '..', 'bridge_test.db');

let serverProc: ChildProcess | null = null;
let bridge: ChatWebSocketBridge | null = null;

function sleep(ms: number): Promise<void> {
  return new Promise((r) => setTimeout(r, ms));
}

class TestClient {
  public ws: WebSocket;
  public messages: any[] = [];
  private messageListeners: ((msg: any) => void)[] = [];

  constructor(url: string) {
    this.ws = new WebSocket(url);
    this.ws.on('message', (data) => {
      try {
        const parsed = JSON.parse(data.toString('utf-8'));
        this.messages.push(parsed);
        for (const listener of this.messageListeners) {
          listener(parsed);
        }
      } catch (err) {
        console.error('TestClient JSON parse error:', err, data.toString());
      }
    });
  }

  public async waitOpen(): Promise<void> {
    if (this.ws.readyState === WebSocket.OPEN) return;
    return new Promise((resolve, reject) => {
      this.ws.once('open', () => resolve());
      this.ws.once('error', (err) => reject(err));
    });
  }

  public send(msg: any): void {
    this.ws.send(JSON.stringify(msg));
  }

  public async waitForMessage(predicate: (msg: any) => boolean, timeoutMs = 4000): Promise<any> {
    // Check if already received
    const existing = this.messages.find(predicate);
    if (existing) return existing;

    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        const idx = this.messageListeners.indexOf(listener);
        if (idx !== -1) this.messageListeners.splice(idx, 1);
        reject(new Error(`Timeout waiting for message matching predicate. Received: ${JSON.stringify(this.messages)}`));
      }, timeoutMs);

      const listener = (msg: any) => {
        if (predicate(msg)) {
          clearTimeout(timer);
          const idx = this.messageListeners.indexOf(listener);
          if (idx !== -1) this.messageListeners.splice(idx, 1);
          resolve(msg);
        }
      };

      this.messageListeners.push(listener);
    });
  }

  public close(): void {
    if (this.ws.readyState === WebSocket.OPEN || this.ws.readyState === WebSocket.CONNECTING) {
      this.ws.close();
    }
  }
}

async function startBackendServer(): Promise<void> {
  if (fs.existsSync(DB_FILE)) {
    fs.unlinkSync(DB_FILE);
  }

  console.log(`[Test Setup] Starting C++ server on port ${TCP_PORT}...`);
  serverProc = spawn(SERVER_EXE, [TCP_PORT.toString(), DB_FILE], {
    cwd: path.dirname(SERVER_EXE),
    env: {
      ...process.env,
      PATH: `${path.dirname(SERVER_EXE)};${process.env.PATH || ''}`,
    },
    stdio: 'ignore',
  });

  // Wait for server to bind port
  await sleep(1000);
}

async function stopBackendServer(): Promise<void> {
  if (serverProc) {
    serverProc.kill();
    serverProc = null;
  }
  await sleep(300);
  if (fs.existsSync(DB_FILE)) {
    try {
      fs.unlinkSync(DB_FILE);
    } catch {}
  }
}

async function runTests() {
  console.log('=== Starting WebSocket Bridge Integration Tests ===\n');
  let passed = 0;
  let failed = 0;

  const assert = (condition: boolean, testName: string) => {
    if (condition) {
      console.log(`[PASS] ${testName}`);
      passed++;
    } else {
      console.error(`[FAIL] ${testName}`);
      failed++;
      throw new Error(`Assertion failed: ${testName}`);
    }
  };

  try {
    await startBackendServer();

    const config: BridgeConfig = {
      bridgeHost: '127.0.0.1',
      bridgePort: BRIDGE_PORT,
      tcpHost: '127.0.0.1',
      tcpPort: TCP_PORT,
      maxPayloadSize: 2048,
      pingIntervalMs: 5000,
    };

    bridge = new ChatWebSocketBridge(config);
    await bridge.start();
    assert(true, 'Bridge started and listening on ws://127.0.0.1:' + BRIDGE_PORT);

    const wsUrl = `ws://127.0.0.1:${BRIDGE_PORT}`;

    // Test 1: Connect Alice and register
    console.log('\n--- Test 1: First Browser Session Registration (Alice) ---');
    const alice = new TestClient(wsUrl);
    await alice.waitOpen();

    alice.send({
      version: 1,
      type: 'register_req',
      id: 'reg-alice',
      username: 'Alice',
    });

    const aliceAck = await alice.waitForMessage((m) => m.type === 'register_ack' && m.id === 'reg-alice');
    assert(aliceAck.status === 'ok' && aliceAck.username === 'Alice', 'Alice receives register_ack with status ok');

    // Test 2: Connect Bob and attempt duplicate registration
    console.log('\n--- Test 2: Duplicate Username Rejection Across Sessions ---');
    const bob = new TestClient(wsUrl);
    await bob.waitOpen();

    bob.send({
      version: 1,
      type: 'register_req',
      id: 'reg-dup',
      username: 'alice', // Case-insensitive duplicate
    });

    const dupErr = await bob.waitForMessage((m) => m.type === 'error' && m.id === 'reg-dup');
    assert(dupErr.code === 'ERR_USERNAME_TAKEN', 'Bob rejected when attempting duplicate username "alice"');

    // Test 3: Bob registers under separate identity and presence event received by Alice
    console.log('\n--- Test 3: Separate Identity Registration & Real-Time Presence ---');
    // Connect fresh socket for Bob after rejection (since backend closes on registration rejection)
    bob.close();
    await sleep(200);

    const bob2 = new TestClient(wsUrl);
    await bob2.waitOpen();

    bob2.send({
      version: 1,
      type: 'register_req',
      id: 'reg-bob',
      username: 'Bob',
    });

    const bobAck = await bob2.waitForMessage((m) => m.type === 'register_ack' && m.id === 'reg-bob');
    assert(bobAck.status === 'ok' && bobAck.username === 'Bob', 'Bob successfully registered under distinct identity');

    // Alice should receive user_joined or system_event join for Bob
    const joinEvent = await alice.waitForMessage(
      (m) => (m.type === 'user_joined' || (m.type === 'system_event' && m.event === 'join')) && m.username === 'Bob'
    );
    assert(joinEvent.username === 'Bob', 'Alice receives real-time join event for Bob');

    // Test 4: Public Broadcast and Sender ACK Policy
    console.log('\n--- Test 4: Public Messaging, Correlation IDs, and Broadcast ---');
    alice.send({
      version: 1,
      type: 'msg_send',
      id: 'pub-msg-1',
      content: 'Hello World from Alice!',
    });

    const aliceMsgAck = await alice.waitForMessage((m) => m.type === 'msg_ack' && m.id === 'pub-msg-1');
    assert(aliceMsgAck.status === 'ok' && !!aliceMsgAck.msg_id, 'Alice receives msg_ack with server-assigned msg_id');

    const bobBroadcast = await bob2.waitForMessage(
      (m) => m.type === 'msg_broadcast' && m.from === 'Alice' && m.content === 'Hello World from Alice!'
    );
    assert(bobBroadcast.msg_id === aliceMsgAck.msg_id, 'Bob receives msg_broadcast matching Alice msg_ack id');

    // Verify Alice does NOT receive her own broadcast
    const aliceSelfBroadcast = alice.messages.find(
      (m) => m.type === 'msg_broadcast' && m.content === 'Hello World from Alice!'
    );
    assert(!aliceSelfBroadcast, 'Alice does not receive self-broadcast');

    // Test 5: Direct Private Messaging and Privacy Isolation
    console.log('\n--- Test 5: Direct Private Messaging & Isolation ---');
    bob2.send({
      version: 1,
      type: 'msg_direct_req',
      id: 'dm-1',
      to: 'Alice',
      content: 'Secret message for Alice only',
    });

    const bobDmAck = await bob2.waitForMessage((m) => m.type === 'msg_direct_ack' && m.id === 'dm-1');
    assert(bobDmAck.status === 'ok' && bobDmAck.to === 'Alice', 'Bob receives msg_direct_ack for Alice');

    const aliceDm = await alice.waitForMessage(
      (m) => m.type === 'msg_direct' && m.from === 'Bob' && m.content === 'Secret message for Alice only'
    );
    assert(aliceDm.from === 'Bob' && aliceDm.to === 'Alice', 'Alice receives direct message from Bob');

    // Connect a third observer (Eve) to verify private message isolation
    const eve = new TestClient(wsUrl);
    await eve.waitOpen();
    eve.send({
      version: 1,
      type: 'register_req',
      id: 'reg-eve',
      username: 'Eve',
    });
    await eve.waitForMessage((m) => m.type === 'register_ack' && m.id === 'reg-eve');

    const eveLeakedDm = eve.messages.find((m) => m.type === 'msg_direct');
    assert(!eveLeakedDm, 'Third-party observer Eve receives 0 direct messages');

    // Test 6: Dynamic Chat Rooms
    console.log('\n--- Test 6: Chat Room Lifecycle, Membership & Room Messaging ---');
    alice.send({
      version: 1,
      type: 'room_create_req',
      id: 'rc-1',
      room: 'project-chat',
    });

    const roomCreateResp = await alice.waitForMessage((m) => m.type === 'room_create_resp' && m.id === 'rc-1');
    assert(roomCreateResp.status === 'ok' && roomCreateResp.room === 'project-chat', 'Alice creates room project-chat');

    // Bob joins the room
    bob2.send({
      version: 1,
      type: 'room_join_req',
      id: 'rj-1',
      room: 'project-chat',
    });
    const bobJoinResp = await bob2.waitForMessage((m) => m.type === 'room_join_resp' && m.id === 'rj-1');
    assert(bobJoinResp.status === 'ok', 'Bob successfully joins room project-chat');

    // Alice should receive room_user_joined event
    const roomJoinEvent = await alice.waitForMessage(
      (m) => m.type === 'room_user_joined' && m.room === 'project-chat' && m.username === 'Bob'
    );
    assert(!!roomJoinEvent, 'Alice receives room_user_joined event for Bob');

    // Bob sends a message to the room
    bob2.send({
      version: 1,
      type: 'room_msg_req',
      id: 'rm-1',
      room: 'project-chat',
      content: 'Hello Project Team!',
    });

    const bobRoomAck = await bob2.waitForMessage((m) => m.type === 'room_msg_ack' && m.id === 'rm-1');
    assert(bobRoomAck.status === 'ok', 'Bob receives room_msg_ack');

    const aliceRoomMsg = await alice.waitForMessage(
      (m) => m.type === 'room_msg' && m.room === 'project-chat' && m.content === 'Hello Project Team!'
    );
    assert(aliceRoomMsg.from === 'Bob', 'Alice receives room_msg from Bob');

    // Eve (not in room) must NOT receive the room message
    const eveRoomLeak = eve.messages.find((m) => m.type === 'room_msg');
    assert(!eveRoomLeak, 'Non-member Eve receives 0 room messages');

    // Test 7: History Retrieval (Public)
    console.log('\n--- Test 7: Cursor-based History Retrieval ---');
    alice.send({
      version: 1,
      type: 'history_req',
      id: 'hist-1',
      conversation_type: 'public',
      limit: 10,
    });

    const histResp = await alice.waitForMessage((m) => m.type === 'history_resp' && m.id === 'hist-1');
    assert(Array.isArray(histResp.messages) && histResp.messages.length > 0, 'History response contains stored public messages');
    assert(histResp.messages.some((x: any) => x.content === 'Hello World from Alice!'), 'History includes Alice public message');

    // Test 8: Oversized Payload Guard
    console.log('\n--- Test 8: Bridge Wire Limit Guard (Oversized Payloads) ---');
    const hugeContent = 'A'.repeat(2500);
    alice.send({
      version: 1,
      type: 'msg_send',
      id: 'huge-1',
      content: hugeContent,
    });

    const sizeErr = await alice.waitForMessage((m) => m.type === 'error' && m.code === 'ERR_MESSAGE_TOO_LARGE');
    assert(!!sizeErr, 'Bridge safely rejects oversized payload (>2048 bytes) with ERR_MESSAGE_TOO_LARGE');

    // Test 9: Disconnection Cleanup & Presence Broadcast
    console.log('\n--- Test 9: Disconnection and Presence Propagation ---');
    bob2.close();
    await sleep(300);

    const leaveEvent = await alice.waitForMessage(
      (m) => (m.type === 'user_left' || (m.type === 'system_event' && m.event === 'leave')) && m.username === 'Bob'
    );
    assert(leaveEvent.username === 'Bob', 'Alice receives leave event when Bob disconnects');

    // Clean up remaining test clients
    alice.close();
    eve.close();

    console.log(`\n=================================================`);
    console.log(` BRIDGE INTEGRATION TESTS: ALL ${passed} TESTS PASSED!`);
    console.log(`=================================================\n`);
  } catch (err) {
    console.error('\n[TEST FAILURE]', err);
    process.exitCode = 1;
  } finally {
    if (bridge) {
      await bridge.stop();
    }
    await stopBackendServer();
  }
}

runTests();
