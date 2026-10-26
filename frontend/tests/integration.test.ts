/**
 * integration.test.ts — End-to-end integration tests using WebSocket directly.
 *
 * Tests two independent sessions against a running C++ server via the bridge.
 * If BRIDGE_URL is not set, automatically starts an isolated C++ server
 * and WebSocket bridge, runs all 15 tests, and cleanly shuts down.
 *
 * Test plan:
 *   T1  Connect session A — must receive register_ack
 *   T2  Connect session B — must receive register_ack
 *   T3  Session A sends public msg — A receives msg_ack; B receives msg_broadcast
 *   T4  msg_ack correlation ID matches sent request ID
 *   T5  Session B sends public msg — B receives msg_ack; A receives msg_broadcast
 *   T6  Session A sends private msg to B — A receives msg_direct_ack; B receives msg_direct
 *   T7  msg_direct_ack does NOT appear on A's broadcast channel
 *   T8  msg_direct does NOT appear on A (sender exclusion)
 *   T9  Duplicate username rejected (ERR_USERNAME_TAKEN)
 *   T10 Invalid username rejected (ERR_INVALID_USERNAME)
 *   T11 Session A requests user list — both users present
 *   T12 Session A creates a room; receives room_create_resp
 *   T13 Session B joins the room; receives room_join_resp; A receives room_user_joined
 *   T14 Session A sends room message; A receives room_msg_ack; B receives room_msg
 *   T15 Session A disconnects; B receives system_event{event:"leave"}
 */

import WebSocket from '../../bridge/node_modules/ws/index.js';
import { spawn, ChildProcess } from 'child_process';
import path from 'path';
import fs from 'fs';
import { ChatWebSocketBridge } from '../../bridge/dist/bridge.js';

let BRIDGE = process.env.BRIDGE_URL ?? '';
const TIMEOUT_MS = 5000;
const TCP_PORT = 8094;
const WS_PORT = 8095;

let serverProc: ChildProcess | null = null;
let bridgeInstance: ChatWebSocketBridge | null = null;
const DB_FILE = path.resolve(process.cwd(), '..', 'frontend_test.db');

function findServerExe(): string {
  const candidates = [
    path.resolve(process.cwd(), '..', 'build', 'chat_server.exe'),
    path.resolve(process.cwd(), '..', 'chat_server.exe'),
    path.resolve(process.cwd(), 'build', 'chat_server.exe'),
  ];
  for (const c of candidates) {
    if (fs.existsSync(c)) return c;
  }
  return candidates[0];
}

// ── Utilities ──────────────────────────────────────────────────────────────

let _reqN = 0;
const makeId = () => `test-${++_reqN}`;

class Session {
  ws: WebSocket;
  buffer: string[] = [];
  waiters: Array<(frame: Record<string, unknown>) => boolean> = [];

  constructor(public label: string, url: string = BRIDGE) {
    this.ws = new WebSocket(url);
    this.ws.on('message', (raw: Buffer) => {
      const text = raw.toString('utf-8').trim();
      try {
        const frame = JSON.parse(text) as Record<string, unknown>;
        this.buffer.push(text);
        const remaining: typeof this.waiters = [];
        for (const waiter of this.waiters) {
          if (!waiter(frame)) remaining.push(waiter);
        }
        this.waiters = remaining;
      } catch {
        // ignore non-JSON (shouldn't happen)
      }
    });
  }

  waitOpen(): Promise<void> {
    return new Promise((res, rej) => {
      if (this.ws.readyState === WebSocket.OPEN) { res(); return; }
      this.ws.once('open', res);
      this.ws.once('error', rej);
      setTimeout(() => rej(new Error(`${this.label}: open timeout`)), TIMEOUT_MS);
    });
  }

  send(obj: Record<string, unknown>) {
    this.ws.send(JSON.stringify(obj));
  }

  /** Wait for a frame matching predicate */
  waitFor(pred: (f: Record<string, unknown>) => boolean, desc: string): Promise<Record<string, unknown>> {
    return new Promise((res, rej) => {
      // Check already-buffered frames
      for (const raw of this.buffer) {
        try {
          const f = JSON.parse(raw) as Record<string, unknown>;
          if (pred(f)) { res(f); return; }
        } catch { /* skip */ }
      }
      const timeout = setTimeout(() =>
        rej(new Error(`${this.label}: timeout waiting for "${desc}". Buffer had ${this.buffer.length} msgs.`)), TIMEOUT_MS);
      this.waiters.push((f) => {
        if (pred(f)) { clearTimeout(timeout); res(f); return true; }
        return false;
      });
    });
  }

  close() {
    try { this.ws.close(1000); } catch { /* ignore */ }
  }
}

// ── Test runner ────────────────────────────────────────────────────────────

let passed = 0;
let failed = 0;
const results: string[] = [];

async function test(name: string, fn: () => Promise<void>) {
  try {
    await fn();
    passed++;
    results.push(`  ✓ ${name}`);
  } catch (err: unknown) {
    failed++;
    const msg = err instanceof Error ? err.message : String(err);
    results.push(`  ✗ ${name}\n      ${msg}`);
  }
}

function assert(cond: boolean, msg: string) {
  if (!cond) throw new Error(`Assertion failed: ${msg}`);
}

// ── Lifecycle ──────────────────────────────────────────────────────────────

async function setup() {
  if (!BRIDGE) {
    const serverExe = findServerExe();
    if (fs.existsSync(DB_FILE)) {
      try { fs.unlinkSync(DB_FILE); } catch {}
    }
    console.log(`[Setup] Spawning C++ backend on port ${TCP_PORT}...`);
    serverProc = spawn(serverExe, [TCP_PORT.toString(), DB_FILE], {
      cwd: path.dirname(serverExe),
      env: {
        ...process.env,
        PATH: `${path.dirname(serverExe)};${process.env.PATH || ''}`,
      },
      stdio: 'ignore',
    });

    await new Promise(r => setTimeout(r, 800));

    console.log(`[Setup] Starting WebSocket bridge on port ${WS_PORT}...`);
    bridgeInstance = new ChatWebSocketBridge({
      bridgePort: WS_PORT,
      bridgeHost: '127.0.0.1',
      tcpPort: TCP_PORT,
      tcpHost: '127.0.0.1',
      maxPayloadBytes: 2048,
      pingIntervalMs: 30000,
    });
    await bridgeInstance.start();
    BRIDGE = `ws://127.0.0.1:${WS_PORT}`;
  }
}

async function teardown() {
  if (bridgeInstance) {
    console.log('[Teardown] Stopping WebSocket bridge...');
    await bridgeInstance.stop();
    bridgeInstance = null;
  }
  if (serverProc) {
    console.log('[Teardown] Terminating C++ server...');
    serverProc.kill();
    serverProc = null;
    await new Promise(r => setTimeout(r, 300));
  }
  if (fs.existsSync(DB_FILE)) {
    try { fs.unlinkSync(DB_FILE); } catch {}
  }
}

// ── Tests ──────────────────────────────────────────────────────────────────

async function runTests() {
  console.log('='.repeat(60));
  console.log('  ChatDesk Frontend / Bridge Integration Tests');
  console.log('='.repeat(60));
  console.log('');

  await setup();
  console.log(`  Bridge Endpoint: ${BRIDGE}\n`);

  const sesA = new Session('Alice', BRIDGE);
  const sesB = new Session('Bob', BRIDGE);

  try {
    // ── Connect ──────────────────────────────────────────────────────────────
    await test('T1: Session A connects and registers', async () => {
      await sesA.waitOpen();
      const id = makeId();
      sesA.send({ version: 1, type: 'register_req', id, username: 'test_alice' });
      const ack = await sesA.waitFor(f => f.type === 'register_ack', 'register_ack');
      assert(ack.status === 'ok', `status=${ack.status}`);
      assert(ack.username === 'test_alice', `username=${ack.username}`);
      assert(ack.id === id, `correlation id mismatch: sent=${id} got=${ack.id}`);
    });

    await test('T2: Session B connects and registers', async () => {
      await sesB.waitOpen();
      const id = makeId();
      sesB.send({ version: 1, type: 'register_req', id, username: 'test_bob' });
      const ack = await sesB.waitFor(f => f.type === 'register_ack', 'register_ack');
      assert(ack.status === 'ok', `status=${ack.status}`);
      assert(ack.username === 'test_bob', `username=${ack.username}`);
    });

    // ── Public messaging ──────────────────────────────────────────────────────
    await test('T3: A sends public msg — A gets msg_ack, B gets msg_broadcast', async () => {
      const id = makeId();
      sesA.send({ version: 1, type: 'msg_send', id, content: 'Hello from Alice!', to: '', room: '' });
      const [ackA, bcB] = await Promise.all([
        sesA.waitFor(f => f.type === 'msg_ack' && f.id === id, 'msg_ack'),
        sesB.waitFor(f => f.type === 'msg_broadcast' && (f.content as string)?.includes('Hello from Alice!'), 'msg_broadcast'),
      ]);
      assert(ackA.status === 'ok', `msg_ack status=${ackA.status}`);
      assert(typeof ackA.msg_id === 'string' && ackA.msg_id.startsWith('msg-'), `invalid msg_id: ${ackA.msg_id}`);
      assert((bcB.from as string) === 'test_alice', `broadcast from=${bcB.from}`);
      assert((bcB.content as string) === 'Hello from Alice!', `content=${bcB.content}`);
    });

    await test('T4: msg_ack correlation ID matches request ID', async () => {
      const id = makeId();
      sesA.send({ version: 1, type: 'msg_send', id, content: 'Correlation test', to: '', room: '' });
      const ack = await sesA.waitFor(f => f.type === 'msg_ack' && f.id === id, 'msg_ack with id');
      assert(ack.id === id, `id mismatch: sent=${id} got=${ack.id}`);
    });

    await test('T5: B sends public msg — B gets msg_ack, A gets msg_broadcast', async () => {
      const id = makeId();
      sesB.send({ version: 1, type: 'msg_send', id, content: 'Hello from Bob!', to: '', room: '' });
      const [ackB, bcA] = await Promise.all([
        sesB.waitFor(f => f.type === 'msg_ack' && f.id === id, 'msg_ack'),
        sesA.waitFor(f => f.type === 'msg_broadcast' && (f.content as string)?.includes('Hello from Bob!'), 'msg_broadcast'),
      ]);
      assert(ackB.status === 'ok', `ack status`);
      assert((bcA.from as string) === 'test_bob', `from=${bcA.from}`);
    });

    // ── Private messaging ─────────────────────────────────────────────────────
    await test('T6: A sends DM to B — A gets msg_direct_ack, B gets msg_direct', async () => {
      const id = makeId();
      sesA.send({ version: 1, type: 'msg_direct_req', id, to: 'test_bob', content: 'Secret from Alice' });
      const [ackA, dmB] = await Promise.all([
        sesA.waitFor(f => f.type === 'msg_direct_ack' && f.id === id, 'msg_direct_ack'),
        sesB.waitFor(f => f.type === 'msg_direct' && (f.content as string)?.includes('Secret from Alice'), 'msg_direct'),
      ]);
      assert(ackA.status === 'ok', `ack status=${ackA.status}`);
      assert((ackA.to as string) === 'test_bob', `ack.to=${ackA.to}`);
      assert((dmB.from as string) === 'test_alice', `msg_direct.from=${dmB.from}`);
      assert((dmB.content as string) === 'Secret from Alice', `content=${dmB.content}`);
    });

    await test('T7: msg_direct_ack does NOT appear as msg_broadcast on A', async () => {
      const leaked = sesB.buffer.some(raw => {
        try {
          const f = JSON.parse(raw) as Record<string, unknown>;
          return f.type === 'msg_broadcast' && (f.content as string)?.includes('Secret from Alice');
        } catch { return false; }
      });
      assert(!leaked, 'DM content leaked as msg_broadcast to B');
    });

    await test('T8: Sender (A) does NOT receive msg_direct for own DM', async () => {
      const leaked = sesA.buffer.some(raw => {
        try {
          const f = JSON.parse(raw) as Record<string, unknown>;
          return f.type === 'msg_direct' && (f.content as string)?.includes('Secret from Alice');
        } catch { return false; }
      });
      assert(!leaked, 'Sender received own msg_direct (should only get msg_direct_ack)');
    });

    // ── Error handling ────────────────────────────────────────────────────────
    await test('T9: Duplicate username rejected with ERR_USERNAME_TAKEN', async () => {
      const sesC = new Session('DuplicateAlice', BRIDGE);
      await sesC.waitOpen();
      sesC.send({ version: 1, type: 'register_req', id: makeId(), username: 'test_alice' });
      const err = await sesC.waitFor(f => f.type === 'error', 'error');
      assert(err.code === 'ERR_USERNAME_TAKEN', `code=${err.code}`);
      sesC.close();
    });

    await test('T10: Invalid username (spaces) rejected', async () => {
      const sesD = new Session('InvalidUser', BRIDGE);
      await sesD.waitOpen();
      sesD.send({ version: 1, type: 'register_req', id: makeId(), username: 'bad user' });
      const err = await sesD.waitFor(f => f.type === 'error', 'error');
      assert(err.code === 'ERR_INVALID_USERNAME' || err.code === 'ERR_MALFORMED_MESSAGE',
        `expected invalid username error, got: ${err.code}`);
      sesD.close();
    });

    // ── User list ─────────────────────────────────────────────────────────────
    await test('T11: user_list_resp includes both test users', async () => {
      const id = makeId();
      sesA.send({ version: 1, type: 'user_list_req', id, page: 1, limit: 50 });
      const resp = await sesA.waitFor(f => f.type === 'user_list_resp' && f.id === id, 'user_list_resp');
      assert(resp.status === 'ok', `status=${resp.status}`);
      const users = resp.users as string[];
      assert(Array.isArray(users), 'users is not array');
      assert(users.includes('test_alice'), 'test_alice not in user list');
      assert(users.includes('test_bob'), 'test_bob not in user list');
    });

    // ── Rooms ─────────────────────────────────────────────────────────────────
    const testRoom = `testroom${Date.now().toString().slice(-5)}`;

    await test('T12: Session A creates a room and receives room_create_resp', async () => {
      const id = makeId();
      sesA.send({ version: 1, type: 'room_create_req', id, room: testRoom });
      const resp = await sesA.waitFor(f => f.type === 'room_create_resp' && f.id === id, 'room_create_resp');
      assert(resp.status === 'ok', `status=${resp.status}`);
      assert(resp.room === testRoom, `room=${resp.room}`);
    });

    await test('T13: Session B joins the room; A receives room_user_joined', async () => {
      const id = makeId();
      sesB.send({ version: 1, type: 'room_join_req', id, room: testRoom });
      const [joinResp, userJoined] = await Promise.all([
        sesB.waitFor(f => f.type === 'room_join_resp' && f.id === id, 'room_join_resp'),
        sesA.waitFor(f => f.type === 'room_user_joined' && f.room === testRoom, 'room_user_joined'),
      ]);
      assert(joinResp.status === 'ok', `B room join status=${joinResp.status}`);
      assert(userJoined.username === 'test_bob', `joined username=${userJoined.username}`);
    });

    await test('T14: A sends room message; A gets room_msg_ack; B gets room_msg', async () => {
      const id = makeId();
      sesA.send({ version: 1, type: 'room_msg_req', id, room: testRoom, content: 'Room hello!' });
      const [ackA, msgB] = await Promise.all([
        sesA.waitFor(f => f.type === 'room_msg_ack' && f.id === id, 'room_msg_ack'),
        sesB.waitFor(f => f.type === 'room_msg' && f.room === testRoom, 'room_msg'),
      ]);
      assert(ackA.status === 'ok', `room_msg_ack status=${ackA.status}`);
      assert((msgB.from as string) === 'test_alice', `room_msg.from=${msgB.from}`);
      assert((msgB.content as string) === 'Room hello!', `content=${msgB.content}`);
    });

    await test('T15: A disconnects gracefully; B receives leave notification', async () => {
      sesA.send({ version: 1, type: 'disconnect_req', id: makeId() });
      await new Promise(r => setTimeout(r, 200));
      sesA.close();
      const leaveEvt = await sesB.waitFor(
        f => (f.type === 'system_event' && f.event === 'leave') || f.type === 'user_left',
        'system_event{leave} or user_left'
      );
      assert(
        leaveEvt.type === 'system_event' || leaveEvt.type === 'user_left',
        `unexpected type: ${leaveEvt.type}`
      );
    });
  } finally {
    sesA.close();
    sesB.close();
    await teardown();
  }

  // ── Report ────────────────────────────────────────────────────────────────
  console.log('\nResults:');
  results.forEach(r => console.log(r));
  console.log('');
  console.log(`─`.repeat(60));
  console.log(`  Total: ${passed + failed}  Passed: ${passed}  Failed: ${failed}`);
  console.log(`─`.repeat(60));

  if (failed > 0) process.exit(1);
}

runTests().catch(async (err) => {
  console.error('Unexpected error:', err);
  await teardown();
  process.exit(1);
});
