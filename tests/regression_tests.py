import socket
import subprocess
import time
import sys
import os
import struct
import threading
import json
import queue

def find_executable(name):
    env_var = name.upper() + "_BIN"
    if os.environ.get(env_var) and os.path.isfile(os.environ[env_var]):
        return os.path.abspath(os.environ[env_var])

    exe_suffix = ".exe" if sys.platform == "win32" else ""
    target_name = f"{name}{exe_suffix}"
    candidates = [
        os.path.join(".", target_name),
        os.path.join("build", target_name),
        os.path.join("build", "Release", target_name),
        os.path.join("build", "Debug", target_name),
    ]
    target = os.path.abspath(target_name)
    for c in candidates:
        if os.path.isfile(c):
            target = os.path.abspath(c)
            break
    if sys.platform != "win32" and os.path.isfile(target):
        try:
            os.chmod(target, 0o755)
        except OSError:
            pass
    return target

SERVER_EXE = find_executable("chat_server")
CLIENT_EXE = find_executable("chat_client")
HOST = "127.0.0.1"
PORT = 8080

class BufferedSocket:
    def __init__(self, sock=None):
        if sock is None:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        else:
            self.sock = sock
        self.buf = b""

    def connect(self, endpoint):
        self.sock.connect(endpoint)

    def sendall(self, data):
        self.sock.sendall(data)

    def setsockopt(self, *args):
        self.sock.setsockopt(*args)

    def set_linger_abort(self):
        # On Windows Winsock, struct linger is (u_short, u_short) = 'hh' (4 bytes)
        # On Linux/POSIX, struct linger is (int, int) = 'ii' (8 bytes)
        fmt = 'hh' if sys.platform == "win32" else 'ii'
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack(fmt, 1, 0))

    def close(self):
        try:
            self.sock.close()
        except:
            pass

    def recv_line(self, timeout=3.0):
        start_time = time.time()
        while b"\n" not in self.buf:
            elapsed = time.time() - start_time
            remaining = timeout - elapsed
            if remaining <= 0:
                break
            try:
                self.sock.settimeout(remaining)
                chunk = self.sock.recv(1024)
                if not chunk:
                    break
                self.buf += chunk
            except (socket.timeout, OSError):
                break
        if b"\n" in self.buf:
            pos = self.buf.index(b"\n")
            line = self.buf[:pos].decode('utf-8', errors='ignore').rstrip('\r')
            self.buf = self.buf[pos + 1:]
            return line
        if self.buf:
            res = self.buf.decode('utf-8', errors='ignore')
            self.buf = b""
            return res
        return ""

    def recv_until(self, expected_substr, timeout=4.0):
        start = time.time()
        while time.time() - start < timeout:
            line = self.recv_line(timeout=max(0.2, timeout - (time.time() - start)))
            if expected_substr in line:
                return line
        return ""

TEST_DB = os.path.abspath(f"test_regression_{os.getpid()}.db")

def cleanup_db(path):
    for f in [path, path + "-wal", path + "-shm"]:
        if os.path.isfile(f):
            try:
                os.remove(f)
            except OSError:
                pass

def start_server(port=8080, db_path=None, env=None):
    global SERVER_EXE
    target_db = db_path if db_path is not None else TEST_DB
    cmd = [SERVER_EXE, str(port), target_db]
    server_env = os.environ.copy()
    if env:
        server_env.update(env)
    p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=server_env)
    time.sleep(0.8)
    return p

def stop_server(p):
    if p.poll() is None:
        p.terminate()
        try:
            p.wait(timeout=3)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()

def find_free_port():
    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    probe.bind((HOST, 0))
    port = probe.getsockname()[1]
    probe.close()
    return port

class ProcessLineReader:
    """Non-blocking line reader for an interactive subprocess (cross-platform)."""
    def __init__(self, process):
        self.process = process
        self.lines = queue.Queue()
        self.thread = threading.Thread(target=self._reader, daemon=True)
        self.thread.start()

    def _reader(self):
        try:
            for line in self.process.stdout:
                self.lines.put(line.rstrip("\r\n"))
        except Exception:
            pass

    def read_until(self, needle, timeout=5.0):
        collected = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            remaining = max(0.05, deadline - time.time())
            try:
                line = self.lines.get(timeout=remaining)
            except queue.Empty:
                break
            collected.append(line)
            if needle in line:
                return line, collected
        return None, collected

    def drain(self, duration=0.2):
        collected = []
        deadline = time.time() + duration
        while time.time() < deadline:
            try:
                collected.append(self.lines.get(timeout=max(0.01, deadline - time.time())))
            except queue.Empty:
                break
        return collected

def run_all_tests():
    global SERVER_EXE, CLIENT_EXE
    results = {}
    print("=== Starting Regression Tests ===")

    # Test 1: Clean Compilation
    print("\n--- Test 1: Clean Compilation ---")
    cmake_cache = os.path.join("build", "CMakeCache.txt")
    if os.path.isfile(cmake_cache):
        print("CMake build directory detected ('build/CMakeCache.txt'). Verifying CMake target build...")
        compile_res = subprocess.run(
            ["cmake", "--build", "build", "--config", "Release", "--target", "chat_server"],
            capture_output=True, text=True
        )
        compile_client_res = subprocess.run(
            ["cmake", "--build", "build", "--config", "Release", "--target", "chat_client"],
            capture_output=True, text=True
        )
    else:
        print("Using direct compiler build with strong warnings as errors...")
        server_cmd = [
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Icommon/include", "-Iserver/include",
            "server/src/main.cpp", "server/src/server.cpp", "server/src/client_manager.cpp", "server/src/room_manager.cpp",
            "server/src/message_store.cpp", "server/src/logger.cpp"
        ]
        if sys.platform == "win32":
            server_cmd += ["-lws2_32", "-lsqlite3", "-o", "chat_server.exe"]
        else:
            server_cmd += ["-pthread", "-lsqlite3", "-o", "chat_server"]

        client_cmd = [
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Icommon/include", "-Iclient/include",
            "client/src/main.cpp", "client/src/client.cpp", "client/src/chat_session.cpp",
            "client/src/sender.cpp", "client/src/receiver.cpp"
        ]
        if sys.platform == "win32":
            client_cmd += ["-lws2_32", "-o", "chat_client.exe"]
        else:
            client_cmd += ["-pthread", "-o", "chat_client"]

        compile_res = subprocess.run(server_cmd, capture_output=True, text=True)
        compile_client_res = subprocess.run(client_cmd, capture_output=True, text=True)

    # Re-resolve executable paths after compilation
    SERVER_EXE = find_executable("chat_server")
    CLIENT_EXE = find_executable("chat_client")
    print(f"Target Server Executable: {SERVER_EXE}")
    print(f"Target Client Executable: {CLIENT_EXE}")

    if compile_res.returncode == 0 and compile_client_res.returncode == 0 and os.path.isfile(SERVER_EXE):
        results["Test 1: Server and client compile cleanly"] = "Passed"
        print("Test 1 PASSED: Server and client compiled cleanly without errors.")
    else:
        results["Test 1: Server and client compile cleanly"] = "Failed"
        print("Test 1 FAILED:", compile_res.stderr, compile_client_res.stderr)

    # Start main test server
    srv = start_server()
    try:
        # Test 2: Two or three clients connect simultaneously
        print("\n--- Test 2: Simultaneous connections ---")
        s1 = BufferedSocket()
        s1.connect((HOST, PORT))
        s1.sendall(b"Alice\n")
        r1 = s1.recv_line()

        s2 = BufferedSocket()
        s2.connect((HOST, PORT))
        s2.sendall(b"Bob\n")
        r2 = s2.recv_line()

        s3 = BufferedSocket()
        s3.connect((HOST, PORT))
        s3.sendall(b"Charlie\n")
        r3 = s3.recv_line()

        if r1 == "OK" and r2 == "OK" and r3 == "OK":
            results["Test 2: Two or three clients connect simultaneously"] = "Passed"
            print("Test 2 PASSED: Alice, Bob, Charlie connected simultaneously and got OK.")
        else:
            results["Test 2: Two or three clients connect simultaneously"] = f"Failed (r1={r1}, r2={r2}, r3={r3})"
            print("Test 2 FAILED")

        # Flush initial join messages
        time.sleep(0.2)
        s1.recv_line(timeout=0.1)
        s2.recv_line(timeout=0.1)

        # Test 3: Bidirectional broadcasting still works
        print("\n--- Test 3: Bidirectional broadcasting ---")
        s1.sendall(b"Hello Bob from Alice\n")
        msg_at_bob = s2.recv_until("Hello Bob from Alice", timeout=2.0)
        s2.sendall(b"Hello Alice from Bob\n")
        msg_at_alice = s1.recv_until("Hello Alice from Bob", timeout=2.0)

        if "[Alice]: Hello Bob from Alice" in msg_at_bob and "[Bob]: Hello Alice from Bob" in msg_at_alice:
            results["Test 3: Bidirectional broadcasting still works"] = "Passed"
            print("Test 3 PASSED: Both received the other's broadcast.")
        else:
            results["Test 3: Bidirectional broadcasting still works"] = f"Failed (bob_got={msg_at_bob}, alice_got={msg_at_alice})"
            print("Test 3 FAILED")

        # Test 4: The sender does not receive its own broadcast
        print("\n--- Test 4: Sender does not receive its own broadcast ---")
        s1.sendall(b"Echo test message\n")
        bob_echo = s2.recv_until("Echo test message", timeout=2.0)
        alice_self = s1.recv_line(timeout=0.5)

        if "[Alice]: Echo test message" in bob_echo and not alice_self:
            results["Test 4: The sender does not receive its own broadcast"] = "Passed"
            print("Test 4 PASSED: Alice did not receive her own broadcast.")
        else:
            results["Test 4: The sender does not receive its own broadcast"] = f"Failed (alice_self={alice_self})"
            print("Test 4 FAILED")

        # Test 5: Duplicate usernames are rejected case-insensitively
        print("\n--- Test 5: Duplicate username rejection (case-insensitive) ---")
        s_dup = BufferedSocket()
        s_dup.connect((HOST, PORT))
        s_dup.sendall(b"aLiCe\n")
        r_dup = s_dup.recv_line()
        s_dup.close()

        if r_dup == "ERR_USERNAME_TAKEN":
            results["Test 5: Duplicate usernames are rejected case-insensitively"] = "Passed"
            print("Test 5 PASSED: Server rejected duplicate case-insensitive username 'aLiCe'.")
        else:
            results["Test 5: Duplicate usernames are rejected case-insensitively"] = f"Failed (resp={r_dup})"
            print("Test 5 FAILED:", r_dup)

        # Test 6: Invalid usernames are rejected
        print("\n--- Test 6: Invalid username rejection ---")
        s_inv = BufferedSocket()
        s_inv.connect((HOST, PORT))
        s_inv.sendall(b"Invalid Username With Spaces\n")
        r_inv = s_inv.recv_line()
        s_inv.close()

        if r_inv == "ERR_INVALID_USERNAME":
            results["Test 6: Invalid usernames are rejected"] = "Passed"
            print("Test 6 PASSED: Server rejected invalid username with spaces.")
        else:
            results["Test 6: Invalid usernames are rejected"] = f"Failed (resp={r_inv})"
            print("Test 6 FAILED:", r_inv)

        # Test 7: Valid messages at the size limit
        print("\n--- Test 7: Valid message at size limit ---")
        payload_2047 = "X" * 2047 # wire size 2048 with \n
        s1.sendall((payload_2047 + "\n").encode('utf-8'))
        bob_limit_msg = s2.recv_until("XXXX", timeout=3.0)

        expected_prefix = "[Alice]: " + ("X" * 100)
        if expected_prefix in bob_limit_msg and len(bob_limit_msg) == len("[Alice]: ") + 2047:
            results["Test 7: Valid messages at the size limit are handled according to the documented limit"] = "Passed"
            print("Test 7 PASSED: 2047-char payload was delivered accurately to Bob.")
        else:
            results["Test 7: Valid messages at the size limit are handled according to the documented limit"] = f"Failed (len={len(bob_limit_msg)})"
            print("Test 7 FAILED:", len(bob_limit_msg))

        # Test 8: Oversized messages are rejected safely
        print("\n--- Test 8: Oversized message rejection ---")
        payload_2048 = "Y" * 2048 # wire size = 2049 bytes with \n -> exceeds MAX_MESSAGE_SIZE 2048
        s1.sendall((payload_2048 + "\n").encode('utf-8'))
        bob_got_oversized = s2.recv_until("YYYY", timeout=1.0)

        # Newline-free oversized input: a rogue client sending 2050 bytes without \n
        s_rogue = BufferedSocket()
        s_rogue.connect((HOST, PORT))
        s_rogue.sendall(b"Rogue_" + b"Z" * 2050)
        time.sleep(0.3)
        rogue_closed = False
        try:
            s_rogue.sock.settimeout(1.0)
            chunk = s_rogue.sock.recv(1024)
            if not chunk:
                rogue_closed = True
        except:
            rogue_closed = True
        s_rogue.close()

        if not bob_got_oversized and rogue_closed:
            results["Test 8: Oversized messages are rejected safely"] = "Passed"
            print("Test 8 PASSED: Framed oversized message was dropped; newline-free rogue was disconnected.")
        else:
            results["Test 8: Oversized messages are rejected safely"] = f"Failed (bob_got={bob_got_oversized}, rogue_closed={rogue_closed})"
            print("Test 8 FAILED")

        # Test 9: Fragmented and coalesced newline-delimited input
        print("\n--- Test 9: Fragmented and coalesced input ---")
        frag_parts = [b"Frag", b"mented ", b"message", b" test\n"]
        for part in frag_parts:
            s1.sendall(part)
            time.sleep(0.05)
        bob_frag = s2.recv_until("Fragmented message test", timeout=2.0)

        s1.sendall(b"MsgA\nMsgB\nMsgC\n")
        bob_coal1 = s2.recv_line(timeout=2.0)
        bob_coal2 = s2.recv_line(timeout=2.0)
        bob_coal3 = s2.recv_line(timeout=2.0)

        if ("[Alice]: Fragmented message test" in bob_frag and
            "[Alice]: MsgA" in bob_coal1 and
            "[Alice]: MsgB" in bob_coal2 and
            "[Alice]: MsgC" in bob_coal3):
            results["Test 9: Fragmented and coalesced newline-delimited input is handled correctly"] = "Passed"
            print("Test 9 PASSED: Fragmented and coalesced inputs parsed correctly.")
        else:
            results["Test 9: Fragmented and coalesced newline-delimited input is handled correctly"] = f"Failed (frag={bob_frag}, c1={bob_coal1}, c2={bob_coal2}, c3={bob_coal3})"
            print("Test 9 FAILED")

        # Test 10: Abrupt client disconnection removes the client
        print("\n--- Test 10: Abrupt client disconnection ---")
        s3.set_linger_abort()
        s3.close()
        time.sleep(0.3)
        bob_charlie_leave = s2.recv_until("Charlie has left the chat", timeout=2.0)
        if "Charlie has left the chat" in bob_charlie_leave:
            results["Test 10: Abrupt client disconnection removes the client"] = "Passed"
            print("Test 10 PASSED: Charlie abrupt disconnect broadcast to Bob.")
        else:
            results["Test 10: Abrupt client disconnection removes the client"] = f"Failed (got={bob_charlie_leave})"
            print("Test 10 FAILED:", bob_charlie_leave)

        # Clean up Alice and Bob from tests 2-10
        s1.close()
        s2.close()
        time.sleep(0.4)
    finally:
        stop_server(srv)

    # Test 11: A failed broadcast does not terminate unrelated clients
    print("\n--- Test 11: Failed broadcast does not terminate unrelated clients ---")
    srv_11 = start_server()
    try:
        s_sender = BufferedSocket()
        s_sender.connect((HOST, PORT))
        s_sender.sendall(b"Sender11\n")
        r_sender = s_sender.recv_line()
        print("Sender11 reg response:", repr(r_sender))

        s_victim = BufferedSocket()
        s_victim.connect((HOST, PORT))
        s_victim.sendall(b"Victim11\n")
        r_victim = s_victim.recv_line()
        print("Victim11 reg response:", repr(r_victim))

        s_listener = BufferedSocket()
        s_listener.connect((HOST, PORT))
        s_listener.sendall(b"Listener11\n")
        r_listener = s_listener.recv_line()
        print("Listener11 reg response:", repr(r_listener))
        time.sleep(0.3)

        # Sever victim socket with SO_LINGER=0 (hard RST)
        s_victim.set_linger_abort()
        s_victim.close()
        time.sleep(0.4)

        # Sender broadcasts message. Broadcast to victim fails, but listener MUST receive it
        s_sender.sendall(b"Message after victim severed\n")
        listener_got = s_listener.recv_until("Message after victim severed", timeout=4.0)

        if "[Sender11]: Message after victim severed" in listener_got:
            results["Test 11: A failed broadcast does not terminate unrelated clients"] = "Passed"
            print("Test 11 PASSED: Listener received broadcast despite victim socket being severed.")
        else:
            results["Test 11: A failed broadcast does not terminate unrelated clients"] = f"Failed (got={repr(listener_got)})"
            print("Test 11 FAILED:", repr(listener_got))

        s_sender.close()
        s_listener.close()
    finally:
        stop_server(srv_11)

    # Test 12: Concurrent broadcasts do not interleave message frames
    print("\n--- Test 12: Concurrent broadcasts do not interleave message frames ---")
    srv_12 = start_server()
    try:
        s_observer = BufferedSocket()
        s_observer.connect((HOST, PORT))
        s_observer.sendall(b"Observer12\n")
        r_obs = s_observer.recv_line()
        print("Observer12 reg response:", repr(r_obs))

        s_spammer1 = BufferedSocket()
        s_spammer1.connect((HOST, PORT))
        s_spammer1.sendall(b"SpamA\n")
        r_spa = s_spammer1.recv_line()
        print("SpamA reg response:", repr(r_spa))

        s_spammer2 = BufferedSocket()
        s_spammer2.connect((HOST, PORT))
        s_spammer2.sendall(b"SpamB\n")
        r_spb = s_spammer2.recv_line()
        print("SpamB reg response:", repr(r_spb))
        time.sleep(0.3)

        # Flush initial join notifications at observer
        s_observer.recv_line(timeout=0.1)
        s_observer.recv_line(timeout=0.1)

        def spam_client(sock, prefix, count):
            for i in range(count):
                sock.sendall(f"{prefix}_{i}\n".encode('utf-8'))
                time.sleep(0.005)

        t1 = threading.Thread(target=spam_client, args=(s_spammer1, "MsgA", 20))
        t2 = threading.Thread(target=spam_client, args=(s_spammer2, "MsgB", 20))
        t1.start()
        t2.start()
        t1.join()
        t2.join()

        # Observer receives all 40 messages
        observer_msgs = []
        start_wait = time.time()
        while len(observer_msgs) < 40 and (time.time() - start_wait < 6.0):
            line = s_observer.recv_line(timeout=1.0)
            if not line:
                time.sleep(0.05)
                continue
            if "MsgA_" in line or "MsgB_" in line:
                observer_msgs.append(line)

        interleaved = False
        for m in observer_msgs:
            if not (m.startswith("[SpamA]: MsgA_") or m.startswith("[SpamB]: MsgB_")):
                interleaved = True
                break

        if len(observer_msgs) >= 38 and not interleaved:
            results["Test 12: Concurrent broadcasts do not interleave message frames"] = "Passed"
            print(f"Test 12 PASSED: Received {len(observer_msgs)} messages without any interleaving.")
        else:
            results["Test 12: Concurrent broadcasts do not interleave message frames"] = f"Failed (count={len(observer_msgs)}, interleaved={interleaved})"
            print("Test 12 FAILED:", len(observer_msgs))

        s_observer.close()
        s_spammer1.close()
        s_spammer2.close()
    finally:
        stop_server(srv_12)

    # Test 13: Server shutdown while clients are connected completes without hanging
    print("\n--- Test 13: Server shutdown with connected clients ---")
    srv2 = start_server()
    time.sleep(0.3)
    s_shut1 = BufferedSocket()
    s_shut1.connect((HOST, PORT))
    s_shut1.sendall(b"ShutClient1\n")
    s_shut1.recv_line()
    s_shut2 = BufferedSocket()
    s_shut2.connect((HOST, PORT))
    s_shut2.sendall(b"ShutClient2\n")
    s_shut2.recv_line()

    t0 = time.time()
    stop_server(srv2)
    shut_duration = time.time() - t0
    s_shut1.close()
    s_shut2.close()

    if shut_duration < 2.5:
        results["Test 13: Server shutdown while clients are connected completes without hanging"] = "Passed"
        print(f"Test 13 PASSED: Server shutdown completed cleanly in {shut_duration:.2f}s.")
    else:
        results["Test 13: Server shutdown while clients are connected completes without hanging"] = f"Failed (took {shut_duration}s)"
        print("Test 13 FAILED")

    # Test 14 & 17: Client shutdown, /quit, registration handshake
    print("\n--- Test 14 & 17: Client /quit and registration handshake ---")
    srv3 = start_server()
    try:
        client_input = "127.0.0.1\n8080\nFrank\n/quit\n"
        c_proc = subprocess.run([CLIENT_EXE], input=client_input, capture_output=True, text=True, timeout=5)
        out = c_proc.stdout

        handshake_ok = "Registration successful! Welcome, Frank." in out
        quit_ok = "Disconnecting from server..." in out and "Disconnected from server." in out and c_proc.returncode == 0

        if quit_ok:
            results["Test 14: Client shutdown and /quit work without double-close or thread hangs"] = "Passed"
            print("Test 14 PASSED: Client executed /quit cleanly with exit code 0.")
        else:
            results["Test 14: Client shutdown and /quit work without double-close or thread hangs"] = f"Failed (ret={c_proc.returncode})"
            print("Test 14 FAILED:", out)

        if handshake_ok:
            results["Test 17: The registration handshake remains compatible with the client"] = "Passed"
            print("Test 17 PASSED: Client handshake with server succeeded.")
        else:
            results["Test 17: The registration handshake remains compatible with the client"] = "Failed"
            print("Test 17 FAILED")

    finally:
        stop_server(srv3)

    # Test 15: The server can restart and accept new connections
    print("\n--- Test 15: Server restart and port rebinding ---")
    srv4 = start_server()
    time.sleep(0.3)
    s_restart = BufferedSocket()
    s_restart.connect((HOST, PORT))
    s_restart.sendall(b"RestartUser\n")
    resp_restart = s_restart.recv_line()
    s_restart.close()
    stop_server(srv4)

    if resp_restart == "OK":
        results["Test 15: The server can restart and accept new connections"] = "Passed"
        print("Test 15 PASSED: Server restarted immediately on port 8080 without WSAEADDRINUSE.")
    else:
        results["Test 15: The server can restart and accept new connections"] = f"Failed (resp={resp_restart})"
        print("Test 15 FAILED")

    # Test 16: Logs remain timestamped and append correctly across sessions
    print("\n--- Test 16: Timestamped logging across sessions ---")
    log_path = "logs/chat.log"
    if os.path.exists(log_path):
        with open(log_path, "r", encoding="utf-8", errors="ignore") as f:
            log_lines = f.readlines()
        has_timestamps = any("[" in l and "]" in l and any(lvl in l for lvl in ["[INFO]", "[CHAT]", "[WARNING]", "[WARN]", "[ERROR]"]) for l in log_lines)
        has_recent = any("RestartUser" in l or "Frank" in l or "Alice" in l or "Sender11" in l for l in log_lines)
        if has_timestamps and has_recent and len(log_lines) > 20:
            results["Test 16: Logs remain timestamped and append correctly across sessions"] = "Passed"
            print(f"Test 16 PASSED: {len(log_lines)} log entries verified with timestamps and log levels.")
        else:
            results["Test 16: Logs remain timestamped and append correctly across sessions"] = f"Failed (lines={len(log_lines)})"
            print("Test 16 FAILED")
    else:
        results["Test 16: Logs remain timestamped and append correctly across sessions"] = "Failed (log file missing)"

    # ==================== PHASE 4 TESTS: STRUCTURED PROTOCOL ====================
    print("\n=== Starting Phase 4 Structured Protocol Tests ===")
    srv_struct = start_server()
    try:
        # Test 18: Structured client registration and ACK
        print("\n--- Test 18: Structured client registration and ACK ---")
        s_s1 = BufferedSocket()
        s_s1.connect((HOST, PORT))
        reg_payload = json.dumps({"version": 1, "type": "register_req", "id": "req-18", "username": "StructAlice"}) + "\n"
        s_s1.sendall(reg_payload.encode('utf-8'))
        resp18_raw = s_s1.recv_line(timeout=2.0)
        try:
            data18 = json.loads(resp18_raw)
            if (data18.get("version") == 1 and data18.get("type") == "register_ack" and
                data18.get("status") == "ok" and data18.get("id") == "req-18" and
                data18.get("username") == "StructAlice"):
                results["Test 18: Structured client registration and ACK"] = "Passed"
                print("Test 18 PASSED: Structured registration acknowledged with correct correlation ID.")
            else:
                results["Test 18: Structured client registration and ACK"] = f"Failed (data={data18})"
                print("Test 18 FAILED:", data18)
        except Exception as e:
            results["Test 18: Structured client registration and ACK"] = f"Failed (json error: {e})"
            print("Test 18 FAILED with JSON parse error:", resp18_raw)

        # Test 19: Structured client duplicate username rejection
        print("\n--- Test 19: Structured client duplicate username rejection ---")
        s_dup = BufferedSocket()
        s_dup.connect((HOST, PORT))
        dup_payload = json.dumps({"version": 1, "type": "register_req", "id": "req-19", "username": "structalice"}) + "\n"
        s_dup.sendall(dup_payload.encode('utf-8'))
        resp19_raw = s_dup.recv_line(timeout=2.0)
        s_dup.close()
        try:
            data19 = json.loads(resp19_raw)
            if (data19.get("type") == "error" and data19.get("code") == "ERR_USERNAME_TAKEN" and
                data19.get("id") == "req-19"):
                results["Test 19: Structured client duplicate username rejection"] = "Passed"
                print("Test 19 PASSED: Server rejected duplicate structured username with ERR_USERNAME_TAKEN.")
            else:
                results["Test 19: Structured client duplicate username rejection"] = f"Failed (data={data19})"
                print("Test 19 FAILED:", data19)
        except Exception as e:
            results["Test 19: Structured client duplicate username rejection"] = f"Failed (json error: {e})"
            print("Test 19 FAILED with JSON parse error:", resp19_raw)

        # Test 20: Structured client chat broadcast and message ACK with correlation IDs
        print("\n--- Test 20: Structured chat broadcast and message ACK ---")
        s_s2 = BufferedSocket()
        s_s2.connect((HOST, PORT))
        reg_bob = json.dumps({"version": 1, "type": "register_req", "id": "req-20-bob", "username": "StructBob"}) + "\n"
        s_s2.sendall(reg_bob.encode('utf-8'))
        s_s2.recv_line(timeout=2.0) # Bob's ACK
        # Alice receives Bob's join system event
        alice_got_join = s_s1.recv_line(timeout=2.0)
        join_data = json.loads(alice_got_join) if alice_got_join else {}

        # Alice sends message
        msg_payload = json.dumps({"version": 1, "type": "msg_send", "id": "req-msg-20", "content": "Hello Bob from Alice!"}) + "\n"
        s_s1.sendall(msg_payload.encode('utf-8'))
        alice_ack_raw = s_s1.recv_line(timeout=2.0)
        bob_bcast_raw = s_s2.recv_line(timeout=2.0)

        try:
            ack_data = json.loads(alice_ack_raw)
            bcast_data = json.loads(bob_bcast_raw)
            ack_ok = (ack_data.get("type") == "msg_ack" and ack_data.get("id") == "req-msg-20" and
                      ack_data.get("status") == "ok" and "msg_id" in ack_data)
            bcast_ok = (bcast_data.get("type") == "msg_broadcast" and bcast_data.get("from") == "StructAlice" and
                        bcast_data.get("content") == "Hello Bob from Alice!" and "msg_id" in bcast_data and
                        "timestamp" in bcast_data)
            if ack_ok and bcast_ok and join_data.get("event") == "join":
                results["Test 20: Structured chat broadcast and message ACK with correlation IDs"] = "Passed"
                print("Test 20 PASSED: Structured ACK and broadcast verified with msg_id and timestamps.")
            else:
                results["Test 20: Structured chat broadcast and message ACK with correlation IDs"] = f"Failed (ack={ack_data}, bcast={bcast_data})"
                print("Test 20 FAILED:", ack_data, bcast_data)
        except Exception as e:
            results["Test 20: Structured chat broadcast and message ACK with correlation IDs"] = f"Failed ({e})"
            print("Test 20 FAILED with error:", e)

        # Test 21: Cross-protocol broadcasting (Structured client <-> Legacy client)
        print("\n--- Test 21: Cross-protocol broadcasting ---")
        s_leg = BufferedSocket()
        s_leg.connect((HOST, PORT))
        s_leg.sendall(b"LegacyCharlie\n")
        resp_leg = s_leg.recv_line(timeout=2.0)

        # Alice and Bob receive structured system event for Charlie's join
        s_s1.recv_line(timeout=1.0)
        s_s2.recv_line(timeout=1.0)

        # 1. Structured Alice sends -> Legacy Charlie must receive plain text [StructAlice]: ...
        cross_msg1 = json.dumps({"version": 1, "type": "msg_send", "id": "req-cross-1", "content": "Hello legacy world!"}) + "\n"
        s_s1.sendall(cross_msg1.encode('utf-8'))
        s_s1.recv_line(timeout=2.0) # Alice ACK
        s_s2.recv_line(timeout=2.0) # Bob JSON broadcast
        charlie_got_from_alice = s_leg.recv_line(timeout=2.0)

        # 2. Legacy Charlie sends -> Structured Alice must receive structured JSON msg_broadcast
        s_leg.sendall(b"Hello structured world from Charlie!\n")
        alice_got_from_charlie_raw = s_s1.recv_line(timeout=2.0)
        s_s2.recv_line(timeout=2.0) # Bob also receives JSON broadcast

        try:
            alice_got_from_charlie = json.loads(alice_got_from_charlie_raw)
            legacy_received_ok = charlie_got_from_alice == "[StructAlice]: Hello legacy world!"
            struct_received_ok = (alice_got_from_charlie.get("type") == "msg_broadcast" and
                                  alice_got_from_charlie.get("from") == "LegacyCharlie" and
                                  alice_got_from_charlie.get("content") == "Hello structured world from Charlie!")
            if resp_leg == "OK" and legacy_received_ok and struct_received_ok:
                results["Test 21: Cross-protocol bidirectional broadcasting (Structured <-> Legacy)"] = "Passed"
                print("Test 21 PASSED: Seamless cross-protocol translation verified in both directions.")
            else:
                results["Test 21: Cross-protocol bidirectional broadcasting (Structured <-> Legacy)"] = f"Failed (leg={charlie_got_from_alice}, struct={alice_got_from_charlie})"
                print("Test 21 FAILED")
        except Exception as e:
            results["Test 21: Cross-protocol bidirectional broadcasting (Structured <-> Legacy)"] = f"Failed ({e})"
            print("Test 21 FAILED with error:", e)

        # Test 22: Escaped newlines in message content
        print("\n--- Test 22: Escaped newlines in message content ---")
        multiline_content = "Line One\nLine Two\nLine Three"
        ml_msg = json.dumps({"version": 1, "type": "msg_send", "id": "req-ml", "content": multiline_content}) + "\n"
        s_s1.sendall(ml_msg.encode('utf-8'))
        s_s1.recv_line(timeout=2.0) # Alice ACK
        bob_ml_raw = s_s2.recv_line(timeout=2.0)
        try:
            bob_ml_data = json.loads(bob_ml_raw)
            if bob_ml_data.get("content") == multiline_content:
                results["Test 22: Escaped newlines in message content handled without breaking framing"] = "Passed"
                print("Test 22 PASSED: Multiline content with embedded newlines preserved through wire framing.")
            else:
                results["Test 22: Escaped newlines in message content handled without breaking framing"] = f"Failed (content={bob_ml_data.get('content')})"
                print("Test 22 FAILED:", bob_ml_data)
        except Exception as e:
            results["Test 22: Escaped newlines in message content handled without breaking framing"] = f"Failed ({e})"
            print("Test 22 FAILED with error:", e)

        # Flush charlie's line(s) from Test 22
        time.sleep(0.2)
        s_leg.recv_line(timeout=0.2)
        s_leg.recv_line(timeout=0.2)
        s_leg.recv_line(timeout=0.2)

        # Test 23: Malformed structured messages rejected safely
        print("\n--- Test 23: Malformed structured message rejection ---")
        s_mal = BufferedSocket()
        s_mal.connect((HOST, PORT))
        s_mal.sendall(b'{"version": 1, "type": "register_req", "username": unquoted_value\n')
        mal_raw = s_mal.recv_line(timeout=2.0)
        s_mal.close()
        try:
            mal_data = json.loads(mal_raw)
            if mal_data.get("type") == "error" and mal_data.get("code") == "ERR_MALFORMED_MESSAGE":
                results["Test 23: Malformed structured messages rejected safely"] = "Passed"
                print("Test 23 PASSED: Malformed JSON safely rejected with ERR_MALFORMED_MESSAGE without crashing.")
            else:
                results["Test 23: Malformed structured messages rejected safely"] = f"Failed (data={mal_data})"
                print("Test 23 FAILED:", mal_data)
        except Exception as e:
            results["Test 23: Malformed structured messages rejected safely"] = f"Failed ({e})"
            print("Test 23 FAILED with error:", e)

        # Test 24: Unsupported protocol version rejected safely
        print("\n--- Test 24: Unsupported protocol version rejection ---")
        s_ver = BufferedSocket()
        s_ver.connect((HOST, PORT))
        ver_payload = json.dumps({"version": 99, "type": "register_req", "username": "FutureUser"}) + "\n"
        s_ver.sendall(ver_payload.encode('utf-8'))
        ver_raw = s_ver.recv_line(timeout=2.0)
        s_ver.close()
        try:
            ver_data = json.loads(ver_raw)
            if ver_data.get("type") == "error" and ver_data.get("code") == "ERR_UNSUPPORTED_VERSION":
                results["Test 24: Unsupported protocol version rejected safely"] = "Passed"
                print("Test 24 PASSED: Unsupported protocol version rejected with ERR_UNSUPPORTED_VERSION.")
            else:
                results["Test 24: Unsupported protocol version rejected safely"] = f"Failed (data={ver_data})"
                print("Test 24 FAILED:", ver_data)
        except Exception as e:
            results["Test 24: Unsupported protocol version rejected safely"] = f"Failed ({e})"
            print("Test 24 FAILED with error:", e)

        # Test 25: Missing required fields and empty message content rejected safely
        print("\n--- Test 25: Missing required fields and empty content rejection ---")
        s_miss = BufferedSocket()
        s_miss.connect((HOST, PORT))
        miss_payload = json.dumps({"version": 1, "type": "register_req"}) + "\n" # omitted username
        s_miss.sendall(miss_payload.encode('utf-8'))
        miss_raw = s_miss.recv_line(timeout=2.0)
        s_miss.close()

        # Alice sends empty content
        empty_payload = json.dumps({"version": 1, "type": "msg_send", "id": "req-empty", "content": ""}) + "\n"
        s_s1.sendall(empty_payload.encode('utf-8'))
        empty_raw = s_s1.recv_line(timeout=2.0)

        # Alice sends unknown type
        badtype_payload = json.dumps({"version": 1, "type": "unknown_action", "id": "req-unknown"}) + "\n"
        s_s1.sendall(badtype_payload.encode('utf-8'))
        badtype_raw = s_s1.recv_line(timeout=2.0)

        try:
            miss_data = json.loads(miss_raw)
            empty_data = json.loads(empty_raw)
            badtype_data = json.loads(badtype_raw)
            if (miss_data.get("code") == "ERR_INVALID_USERNAME" and
                empty_data.get("code") == "ERR_EMPTY_MESSAGE" and
                badtype_data.get("code") == "ERR_UNKNOWN_TYPE"):
                results["Test 25: Missing required fields and empty content rejected safely"] = "Passed"
                print("Test 25 PASSED: Missing fields, empty content, and unknown types rejected safely.")
            else:
                results["Test 25: Missing required fields and empty content rejected safely"] = f"Failed (miss={miss_data}, empty={empty_data}, badtype={badtype_data})"
                print("Test 25 FAILED")
        except Exception as e:
            results["Test 25: Missing required fields and empty content rejected safely"] = f"Failed ({e})"
            print("Test 25 FAILED with error:", e)

        # Test 26: Structured disconnect request (disconnect_req)
        print("\n--- Test 26: Structured disconnect request ---")
        disc_payload = json.dumps({"version": 1, "type": "disconnect_req", "id": "req-disc"}) + "\n"
        s_s1.sendall(disc_payload.encode('utf-8'))
        bob_leave_raw = s_s2.recv_line(timeout=2.0)
        charlie_leave_raw = s_leg.recv_line(timeout=2.0)

        try:
            bob_leave_data = json.loads(bob_leave_raw)
            bob_leave_ok = (bob_leave_data.get("type") == "system_event" and
                            bob_leave_data.get("event") == "leave" and
                            bob_leave_data.get("username") == "StructAlice")
            charlie_leave_ok = "[System] StructAlice has left the chat." in charlie_leave_raw
            if bob_leave_ok and charlie_leave_ok:
                results["Test 26: Structured disconnect request handled and broadcast correctly"] = "Passed"
                print("Test 26 PASSED: Structured disconnect_req broadcast correctly to both structured and legacy clients.")
            else:
                results["Test 26: Structured disconnect request handled and broadcast correctly"] = f"Failed (bob={bob_leave_data}, charlie={charlie_leave_raw})"
                print("Test 26 FAILED")
        except Exception as e:
            results["Test 26: Structured disconnect request handled and broadcast correctly"] = f"Failed ({e})"
            print("Test 26 FAILED with error:", e)

        s_s1.close()
        s_s2.close()
        s_leg.close()

        # Test 27: Protocol detection heuristic and isolation of malformed structured messages
        print("\n--- Test 27: Protocol detection and malformed structured message isolation ---")
        try:
            t27_passed = True
            t27_details = []

            # Sub-test 27A: Leading whitespace before valid structured registration
            s_t27_a = BufferedSocket()
            s_t27_a.connect((HOST, PORT))
            space_reg = "   " + json.dumps({"version": 1, "type": "register_req", "id": "t27-a", "username": "SpacedUser"}) + "\n"
            s_t27_a.sendall(space_reg.encode('utf-8'))
            resp_27a_raw = s_t27_a.recv_line(timeout=2.0)
            resp_27a = json.loads(resp_27a_raw)
            if resp_27a.get("type") == "register_ack" and resp_27a.get("username") == "SpacedUser" and resp_27a.get("id") == "t27-a":
                t27_details.append("27A (leading whitespace structured reg acknowledged) OK")
            else:
                t27_passed = False
                t27_details.append(f"27A failed: {resp_27a}")

            # Sub-test 27B: Leading whitespace before malformed JSON registration (must NOT be treated as legacy username)
            s_t27_b = BufferedSocket()
            s_t27_b.connect((HOST, PORT))
            mal_space = '   {"version": 1, "type": "register_req", username_unquoted_broken\n'
            s_t27_b.sendall(mal_space.encode('utf-8'))
            resp_27b_raw = s_t27_b.recv_line(timeout=2.0)
            s_t27_b.close()
            resp_27b = json.loads(resp_27b_raw) if resp_27b_raw.startswith("{") else {}
            if resp_27b.get("type") == "error" and resp_27b.get("code") == "ERR_MALFORMED_MESSAGE" and resp_27b_raw != "ERR_INVALID_USERNAME":
                t27_details.append("27B (malformed registration isolated as ERR_MALFORMED_MESSAGE) OK")
            else:
                t27_passed = False
                t27_details.append(f"27B failed: raw='{resp_27b_raw}' parsed={resp_27b}")

            # Sub-test 27C: Missing outer brace but structured markers present (must NOT be treated as legacy username)
            s_t27_c = BufferedSocket()
            s_t27_c.connect((HOST, PORT))
            nobrace = '"version": 1, "type": "register_req", "username": "NoBrace"\n'
            s_t27_c.sendall(nobrace.encode('utf-8'))
            resp_27c_raw = s_t27_c.recv_line(timeout=2.0)
            s_t27_c.close()
            resp_27c = json.loads(resp_27c_raw) if resp_27c_raw.startswith("{") else {}
            if resp_27c.get("type") == "error" and resp_27c.get("code") == "ERR_MALFORMED_MESSAGE" and resp_27c_raw != "ERR_INVALID_USERNAME":
                t27_details.append("27C (missing brace structured intent rejected as ERR_MALFORMED_MESSAGE) OK")
            else:
                t27_passed = False
                t27_details.append(f"27C failed: raw='{resp_27c_raw}' parsed={resp_27c}")

            # Sub-test 27D: Premature chat message before registration
            s_t27_d = BufferedSocket()
            s_t27_d.connect((HOST, PORT))
            premature_chat = json.dumps({"version": 1, "type": "msg_send", "content": "PrematureChat"}) + "\n"
            s_t27_d.sendall(premature_chat.encode('utf-8'))
            resp_27d_raw = s_t27_d.recv_line(timeout=2.0)
            s_t27_d.close()
            resp_27d = json.loads(resp_27d_raw) if resp_27d_raw.startswith("{") else {}
            if resp_27d.get("type") == "error" and resp_27d.get("code") == "ERR_UNKNOWN_TYPE":
                t27_details.append("27D (premature chat rejected with ERR_UNKNOWN_TYPE) OK")
            else:
                t27_passed = False
                t27_details.append(f"27D failed: raw='{resp_27d_raw}' parsed={resp_27d}")

            # Sub-test 27E: Post-registration malformed unstructured message from structured client
            s_t27_a.sendall(b"Not a valid JSON message\n")
            resp_27e_raw = s_t27_a.recv_line(timeout=2.0)
            resp_27e = json.loads(resp_27e_raw) if resp_27e_raw.startswith("{") else {}
            if resp_27e.get("type") == "error" and resp_27e.get("code") == "ERR_MALFORMED_MESSAGE":
                t27_details.append("27E (post-reg malformed message rejected as ERR_MALFORMED_MESSAGE) OK")
            else:
                t27_passed = False
                t27_details.append(f"27E failed: raw='{resp_27e_raw}' parsed={resp_27e}")

            s_t27_a.close()

            if t27_passed:
                results["Test 27: Protocol detection and structured message isolation"] = "Passed"
                print(f"Test 27 PASSED: {'; '.join(t27_details)}")
            else:
                results["Test 27: Protocol detection and structured message isolation"] = f"Failed ({'; '.join(t27_details)})"
                print(f"Test 27 FAILED: {'; '.join(t27_details)}")
        except Exception as e:
            results["Test 27: Protocol detection and structured message isolation"] = f"Failed ({e})"
            print("Test 27 FAILED with exception:", e)

        # Test 28: Message-size constraint under JSON escaping and wire limit enforcement
        print("\n--- Test 28: Message-size constraint under JSON escaping ---")
        try:
            t28_passed = True
            t28_details = []

            # Set up sender (structured) and listener (structured)
            s_esc_sender = BufferedSocket()
            s_esc_sender.connect((HOST, PORT))
            s_esc_sender.sendall((json.dumps({"version": 1, "type": "register_req", "username": "EscSender", "id": "reg-esc"}) + "\n").encode('utf-8'))
            s_esc_sender.recv_line(timeout=2.0)

            s_esc_listener = BufferedSocket()
            s_esc_listener.connect((HOST, PORT))
            s_esc_listener.sendall((json.dumps({"version": 1, "type": "register_req", "username": "EscListener", "id": "reg-lis"}) + "\n").encode('utf-8'))
            s_esc_listener.recv_line(timeout=2.0)
            # Drain join announcement on sender
            s_esc_sender.recv_line(timeout=1.0)

            # Sub-test 28A: Raw unescaped content is under 2048 bytes (980 quotes = 980 bytes),
            # but when JSON-escaped in the broadcast envelope (1960 escaped chars + ~115 envelope > 2048),
            # the server MUST reject it with ERR_MESSAGE_TOO_LARGE and NOT leak oversized frame to listeners.
            esc_980_content = '"' * 980
            req_980 = json.dumps({"version": 1, "type": "msg_send", "id": "req-esc-980", "content": esc_980_content}) + "\n"
            # Verify incoming wire payload is within 2048 limit
            assert len(req_980.encode('utf-8')) <= 2048, f"req_980 length {len(req_980.encode('utf-8'))} unexpectedly > 2048"

            s_esc_sender.sendall(req_980.encode('utf-8'))
            resp_980_raw = s_esc_sender.recv_line(timeout=2.0)
            resp_980 = json.loads(resp_980_raw)

            # Listener must NOT receive any message because broadcast was blocked
            listener_leak = s_esc_listener.recv_line(timeout=0.6)

            if (resp_980.get("type") == "error" and
                resp_980.get("code") == "ERR_MESSAGE_TOO_LARGE" and
                resp_980.get("id") == "req-esc-980" and
                listener_leak == ""):
                t28_details.append("28A (expanding JSON envelope rejected with ERR_MESSAGE_TOO_LARGE; no listener leak) OK")
            else:
                t28_passed = False
                t28_details.append(f"28A failed: resp={resp_980}, listener_leak='{listener_leak[:50]}'")

            # Sub-test 28B: Content with escaping that stays strictly within the 2048 wire limit is delivered
            esc_700_content = '"' * 700
            req_700 = json.dumps({"version": 1, "type": "msg_send", "id": "req-esc-700", "content": esc_700_content}) + "\n"
            s_esc_sender.sendall(req_700.encode('utf-8'))
            resp_700_raw = s_esc_sender.recv_line(timeout=2.0)
            resp_700 = json.loads(resp_700_raw)

            broadcast_raw = s_esc_listener.recv_line(timeout=2.0)
            broadcast_data = json.loads(broadcast_raw)
            broadcast_wire_len = len(broadcast_raw.encode('utf-8')) + 1 # include delimiter

            if (resp_700.get("type") == "msg_ack" and
                resp_700.get("id") == "req-esc-700" and
                broadcast_data.get("type") == "msg_broadcast" and
                broadcast_data.get("from") == "EscSender" and
                broadcast_data.get("content") == esc_700_content and
                broadcast_wire_len <= 2048):
                t28_details.append(f"28B (escaped content within limit delivered; wire size={broadcast_wire_len} <= 2048) OK")
            else:
                t28_passed = False
                t28_details.append(f"28B failed: resp={resp_700}, bcast_len={broadcast_wire_len}")

            # Sub-test 28C: Legacy client sends 1500 quotes (1501 bytes wire <= 2048).
            # Translated broadcast to structured listener must not exceed 2048 bytes wire limit due to defensive bounding.
            s_esc_leg = BufferedSocket()
            s_esc_leg.connect((HOST, PORT))
            s_esc_leg.sendall(b"EscLegacy\n")
            leg_ok = s_esc_leg.recv_line(timeout=2.0)
            assert leg_ok == "OK", f"Legacy registration failed: {leg_ok}"

            # Drain join announcement on listener
            s_esc_listener.recv_line(timeout=1.0)

            # Legacy sends 1500 quotes
            leg_payload = ('"' * 1500) + "\n"
            s_esc_leg.sendall(leg_payload.encode('utf-8'))

            leg_bcast_raw = s_esc_listener.recv_line(timeout=2.0)
            leg_bcast_wire_len = len(leg_bcast_raw.encode('utf-8')) + 1
            leg_bcast_data = json.loads(leg_bcast_raw)

            if (leg_bcast_wire_len <= 2048 and
                leg_bcast_data.get("type") == "msg_broadcast" and
                leg_bcast_data.get("from") == "EscLegacy" and
                isinstance(leg_bcast_data.get("content"), str)):
                t28_details.append(f"28C (legacy high-expansion chat defensively bounded to wire size={leg_bcast_wire_len} <= 2048) OK")
            else:
                t28_passed = False
                t28_details.append(f"28C failed: wire_len={leg_bcast_wire_len}, data={leg_bcast_data.get('type')}")

            s_esc_sender.close()
            s_esc_listener.close()
            s_esc_leg.close()

            if t28_passed:
                results["Test 28: Message-size constraint under JSON escaping"] = "Passed"
                print(f"Test 28 PASSED: {'; '.join(t28_details)}")
            else:
                results["Test 28: Message-size constraint under JSON escaping"] = f"Failed ({'; '.join(t28_details)})"
                print(f"Test 28 FAILED: {'; '.join(t28_details)}")
        except Exception as e:
            results["Test 28: Message-size constraint under JSON escaping"] = f"Failed ({e})"
            print("Test 28 FAILED with exception:", e)

        # Test 29: Online User List Query, Correlation IDs, and Registry Integrity
        print("\n--- Test 29: Online user list query and registry integrity ---")
        try:
            t29_passed = True
            t29_details = []

            # Sub-test 29A: Unregistered user list request rejected with ERR_NOT_REGISTERED
            s_unreg = BufferedSocket()
            s_unreg.connect((HOST, PORT))
            unreg_payload = json.dumps({"version": 1, "type": "user_list_req", "id": "req-unreg-99"}) + "\n"
            s_unreg.sendall(unreg_payload.encode('utf-8'))
            unreg_resp_raw = s_unreg.recv_line(timeout=2.0)
            s_unreg.close()
            unreg_resp = json.loads(unreg_resp_raw) if unreg_resp_raw.startswith("{") else {}
            if unreg_resp.get("type") == "error" and unreg_resp.get("code") == "ERR_NOT_REGISTERED" and unreg_resp.get("id") == "req-unreg-99":
                t29_details.append("29A (unregistered request rejected with ERR_NOT_REGISTERED) OK")
            else:
                t29_passed = False
                t29_details.append(f"29A failed: {unreg_resp}")

            # Sub-test 29B: Active registered users query
            s_u1 = BufferedSocket()
            s_u1.connect((HOST, PORT))
            s_u1.sendall((json.dumps({"version": 1, "type": "register_req", "username": "UListAlice", "id": "reg-u1"}) + "\n").encode('utf-8'))
            s_u1.recv_line(timeout=2.0)

            s_u2 = BufferedSocket()
            s_u2.connect((HOST, PORT))
            s_u2.sendall((json.dumps({"version": 1, "type": "register_req", "username": "UListBob", "id": "reg-u2"}) + "\n").encode('utf-8'))
            s_u2.recv_line(timeout=2.0)

            s_u3 = BufferedSocket()
            s_u3.connect((HOST, PORT))
            s_u3.sendall(b"UListCharlie\n")
            s_u3.recv_line(timeout=2.0)

            # Drain join announcements on s_u1
            s_u1.recv_line(timeout=1.0)
            s_u1.recv_line(timeout=1.0)

            # Sub-test 29C: Duplicate and invalid registration rejection
            s_dup = BufferedSocket()
            s_dup.connect((HOST, PORT))
            s_dup.sendall((json.dumps({"version": 1, "type": "register_req", "username": "ulistalice", "id": "reg-dup"}) + "\n").encode('utf-8'))
            dup_raw = s_dup.recv_line(timeout=2.0)
            s_dup.close()
            dup_resp = json.loads(dup_raw) if dup_raw.startswith("{") else {}

            s_inv = BufferedSocket()
            s_inv.connect((HOST, PORT))
            s_inv.sendall((json.dumps({"version": 1, "type": "register_req", "username": "Invalid User Name", "id": "reg-inv"}) + "\n").encode('utf-8'))
            inv_raw = s_inv.recv_line(timeout=2.0)
            s_inv.close()
            inv_resp = json.loads(inv_raw) if inv_raw.startswith("{") else {}

            if dup_resp.get("code") == "ERR_USERNAME_TAKEN" and inv_resp.get("code") == "ERR_INVALID_USERNAME":
                t29_details.append("29C (duplicate and invalid registrations rejected) OK")
            else:
                t29_passed = False
                t29_details.append(f"29C failed: dup={dup_resp}, inv={inv_resp}")

            # Sub-test 29D: User list response contains all active users including requester, matching correlation ID
            s_u1.sendall((json.dumps({"version": 1, "type": "user_list_req", "id": "req-list-29"}) + "\n").encode('utf-8'))
            list_raw = s_u1.recv_line(timeout=2.0)
            list_resp = json.loads(list_raw)

            users_set = set(list_resp.get("users", []))
            expected_set = {"UListAlice", "UListBob", "UListCharlie"}
            if (list_resp.get("type") == "user_list_resp" and
                list_resp.get("status") == "ok" and
                list_resp.get("id") == "req-list-29" and
                list_resp.get("total_users") == 3 and
                list_resp.get("count") == 3 and
                expected_set.issubset(users_set) and
                "ulistalice" not in users_set and
                "Invalid User Name" not in users_set and
                "UListAlice" in users_set):
                t29_details.append("29D (user list accurately includes active users and excludes invalid/duplicate) OK")
            else:
                t29_passed = False
                t29_details.append(f"29D failed: {list_resp}")

            if t29_passed:
                results["Test 29: Online user list query and registry integrity"] = "Passed"
                print(f"Test 29 PASSED: {'; '.join(t29_details)}")
            else:
                results["Test 29: Online user list query and registry integrity"] = f"Failed ({'; '.join(t29_details)})"
                print(f"Test 29 FAILED: {'; '.join(t29_details)}")
        except Exception as e:
            results["Test 29: Online user list query and registry integrity"] = f"Failed ({e})"
            print("Test 29 FAILED with exception:", e)

        # Test 30: Real-time Join/Leave Updates and Registry State Synchronization
        print("\n--- Test 30: Real-time join/leave updates and registry synchronization ---")
        try:
            t30_passed = True
            t30_details = []

            # Connect Dave (Structured) while Alice is listening
            s_u4 = BufferedSocket()
            s_u4.connect((HOST, PORT))
            s_u4.sendall((json.dumps({"version": 1, "type": "register_req", "username": "UListDave", "id": "reg-u4"}) + "\n").encode('utf-8'))
            s_u4.recv_line(timeout=2.0)

            # Alice receives join notification
            dave_join_raw = s_u1.recv_line(timeout=2.0)
            dave_join = json.loads(dave_join_raw) if dave_join_raw.startswith("{") else {}
            if (dave_join.get("type") == "system_event" and
                dave_join.get("event") == "join" and
                dave_join.get("username") == "UListDave"):
                t30_details.append("30A (join notification received by listener) OK")
            else:
                t30_passed = False
                t30_details.append(f"30A failed: {dave_join}")

            # Alice queries user list: Dave is now present
            s_u1.sendall((json.dumps({"version": 1, "type": "user_list_req", "id": "req-list-30a"}) + "\n").encode('utf-8'))
            list_30a_raw = s_u1.recv_line(timeout=2.0)
            list_30a = json.loads(list_30a_raw)
            if "UListDave" in list_30a.get("users", []) and list_30a.get("total_users") == 4:
                t30_details.append("30B (registry reflects joined user) OK")
            else:
                t30_passed = False
                t30_details.append(f"30B failed: {list_30a}")

            # Dave disconnects with disconnect_req
            s_u4.sendall((json.dumps({"version": 1, "type": "disconnect_req", "id": "disc-u4"}) + "\n").encode('utf-8'))
            s_u4.close()

            # Alice receives leave notification
            dave_leave_raw = s_u1.recv_line(timeout=2.0)
            dave_leave = json.loads(dave_leave_raw) if dave_leave_raw.startswith("{") else {}
            # Verify no second leave notification
            dup_leave = s_u1.recv_line(timeout=0.4)

            if (dave_leave.get("type") == "system_event" and
                dave_leave.get("event") == "leave" and
                dave_leave.get("username") == "UListDave" and
                dup_leave == ""):
                t30_details.append("30C (exactly one leave notification received) OK")
            else:
                t30_passed = False
                t30_details.append(f"30C failed: leave={dave_leave}, dup='{dup_leave}'")

            # Alice queries user list: Dave is removed
            s_u1.sendall((json.dumps({"version": 1, "type": "user_list_req", "id": "req-list-30b"}) + "\n").encode('utf-8'))
            list_30b_raw = s_u1.recv_line(timeout=2.0)
            list_30b = json.loads(list_30b_raw)
            if "UListDave" not in list_30b.get("users", []) and list_30b.get("total_users") == 3:
                t30_details.append("30D (registry reflects departed user) OK")
            else:
                t30_passed = False
                t30_details.append(f"30D failed: {list_30b}")

            # Legacy client sends chat: legacy compatibility preserved
            s_u3.sendall(b"Hello from legacy Charlie\n")
            charlie_bcast_raw = s_u1.recv_line(timeout=2.0)
            charlie_bcast = json.loads(charlie_bcast_raw) if charlie_bcast_raw.startswith("{") else {}
            if (charlie_bcast.get("type") == "msg_broadcast" and
                charlie_bcast.get("from") == "UListCharlie" and
                charlie_bcast.get("content") == "Hello from legacy Charlie"):
                t30_details.append("30E (legacy client communication preserved) OK")
            else:
                t30_passed = False
                t30_details.append(f"30E failed: {charlie_bcast}")

            s_u1.close()
            s_u2.close()
            s_u3.close()

            if t30_passed:
                results["Test 30: Real-time join/leave updates and registry synchronization"] = "Passed"
                print(f"Test 30 PASSED: {'; '.join(t30_details)}")
            else:
                results["Test 30: Real-time join/leave updates and registry synchronization"] = f"Failed ({'; '.join(t30_details)})"
                print(f"Test 30 FAILED: {'; '.join(t30_details)}")
        except Exception as e:
            results["Test 30: Real-time join/leave updates and registry synchronization"] = f"Failed ({e})"
            print("Test 30 FAILED with exception:", e)

        # Test 31: User List Pagination and Oversized Wire Limit Policy
        print("\n--- Test 31: User list pagination and oversized wire limit policy ---")
        try:
            t31_passed = True
            t31_details = []

            # Connect 3 clients for pagination tests
            s_p1 = BufferedSocket()
            s_p1.connect((HOST, PORT))
            s_p1.sendall((json.dumps({"version": 1, "type": "register_req", "username": "PagAlice", "id": "reg-p1"}) + "\n").encode('utf-8'))
            s_p1.recv_line(timeout=2.0)

            s_p2 = BufferedSocket()
            s_p2.connect((HOST, PORT))
            s_p2.sendall((json.dumps({"version": 1, "type": "register_req", "username": "PagBob", "id": "reg-p2"}) + "\n").encode('utf-8'))
            s_p2.recv_line(timeout=2.0)

            s_p3 = BufferedSocket()
            s_p3.connect((HOST, PORT))
            s_p3.sendall((json.dumps({"version": 1, "type": "register_req", "username": "PagCharlie", "id": "reg-p3"}) + "\n").encode('utf-8'))
            s_p3.recv_line(timeout=2.0)

            # Drain join events on s_p1
            s_p1.recv_line(timeout=1.0)
            s_p1.recv_line(timeout=1.0)

            # Sub-test 31A: Page 1 with limit 2
            s_p1.sendall((json.dumps({"version": 1, "type": "user_list_req", "id": "req-pag-1", "page": 1, "limit": 2}) + "\n").encode('utf-8'))
            p1_raw = s_p1.recv_line(timeout=2.0)
            p1_data = json.loads(p1_raw)
            p1_users = p1_data.get("users", [])

            # Sub-test 31B: Page 2 with limit 2
            s_p1.sendall((json.dumps({"version": 1, "type": "user_list_req", "id": "req-pag-2", "page": 2, "limit": 2}) + "\n").encode('utf-8'))
            p2_raw = s_p1.recv_line(timeout=2.0)
            p2_data = json.loads(p2_raw)
            p2_users = p2_data.get("users", [])

            if (p1_data.get("page") == 1 and p1_data.get("total_pages") == 2 and p1_data.get("total_users") == 3 and len(p1_users) == 2 and
                p2_data.get("page") == 2 and p2_data.get("total_pages") == 2 and p2_data.get("total_users") == 3 and len(p2_users) == 1 and
                len(set(p1_users).intersection(set(p2_users))) == 0 and
                set(p1_users).union(set(p2_users)) == {"PagAlice", "PagBob", "PagCharlie"}):
                t31_details.append("31A & 31B (page 1 and page 2 pagination slices accurate and disjoint) OK")
            else:
                t31_passed = False
                t31_details.append(f"31A/B failed: p1={p1_data}, p2={p2_data}")

            # Sub-test 31C: Page 3 with limit 2 (out of range)
            s_p1.sendall((json.dumps({"version": 1, "type": "user_list_req", "id": "req-pag-3", "page": 3, "limit": 2}) + "\n").encode('utf-8'))
            p3_raw = s_p1.recv_line(timeout=2.0)
            p3_data = json.loads(p3_raw)
            if p3_data.get("page") == 3 and p3_data.get("count") == 0 and p3_data.get("users") == []:
                t31_details.append("31C (out-of-range page returns empty list with status ok) OK")
            else:
                t31_passed = False
                t31_details.append(f"31C failed: {p3_data}")

            # Sub-test 31D: Negative page parameter rejected safely
            s_p1.sendall((json.dumps({"version": 1, "type": "user_list_req", "id": "req-pag-neg", "page": -1}) + "\n").encode('utf-8'))
            p_neg_raw = s_p1.recv_line(timeout=2.0)
            p_neg_data = json.loads(p_neg_raw)
            if p_neg_data.get("type") == "error" and p_neg_data.get("code") == "ERR_MALFORMED_MESSAGE":
                t31_details.append("31D (negative page rejected with ERR_MALFORMED_MESSAGE) OK")
            else:
                t31_passed = False
                t31_details.append(f"31D failed: {p_neg_data}")

            # Sub-test 31E: Clamping requested limit to MAX_USER_LIST_LIMIT and strictly bounding wire size
            s_p1.sendall((json.dumps({"version": 1, "type": "user_list_req", "id": "req-pag-clamp", "limit": 9999}) + "\n").encode('utf-8'))
            p_clamp_raw = s_p1.recv_line(timeout=2.0)
            p_clamp_wire_size = len(p_clamp_raw.encode('utf-8')) + 1
            p_clamp_data = json.loads(p_clamp_raw)
            if p_clamp_data.get("status") == "ok" and p_clamp_wire_size <= 2048:
                t31_details.append(f"31E (large limit clamped safely, wire size={p_clamp_wire_size} <= 2048) OK")
            else:
                t31_passed = False
                t31_details.append(f"31E failed: wire_size={p_clamp_wire_size}")

            s_p1.close()
            s_p2.close()
            s_p3.close()

            if t31_passed:
                results["Test 31: User list pagination and oversized wire limit policy"] = "Passed"
                print(f"Test 31 PASSED: {'; '.join(t31_details)}")
            else:
                results["Test 31: User list pagination and oversized wire limit policy"] = f"Failed ({'; '.join(t31_details)})"
                print(f"Test 31 FAILED: {'; '.join(t31_details)}")
        except Exception as e:
            results["Test 31: User list pagination and oversized wire limit policy"] = f"Failed ({e})"
            print("Test 31 FAILED with exception:", e)

        # Test 32: Concurrent Registration, Query, and Disconnection Thread-Safety
        print("\n--- Test 32: Concurrent registration, query, and disconnection thread-safety ---")
        try:
            con_errors = []
            num_con_threads = 8

            def worker_concurrent_task(thread_id):
                uname = f"ConUser_{thread_id}"
                try:
                    sock = BufferedSocket()
                    sock.connect((HOST, PORT))
                    # Register
                    reg = json.dumps({"version": 1, "type": "register_req", "username": uname, "id": f"reg-{thread_id}"}) + "\n"
                    sock.sendall(reg.encode('utf-8'))
                    
                    # Read until register_ack is received (filtering out concurrent join/leave system_events)
                    start_t = time.time()
                    ack = None
                    while time.time() - start_t < 4.0:
                        line = sock.recv_line(timeout=1.0)
                        if not line:
                            continue
                        try:
                            data = json.loads(line)
                            if data.get("type") == "register_ack":
                                ack = data
                                break
                        except Exception:
                            pass

                    if not ack or ack.get("username") != uname:
                        con_errors.append(f"Thread {thread_id} registration failed: {ack}")
                        sock.close()
                        return

                    # Query user list
                    time.sleep(0.05)
                    q = json.dumps({"version": 1, "type": "user_list_req", "id": f"q-{thread_id}"}) + "\n"
                    sock.sendall(q.encode('utf-8'))
                    
                    # Read until user_list_resp is received (filtering out concurrent broadcasts)
                    start_t = time.time()
                    resp = None
                    while time.time() - start_t < 4.0:
                        line = sock.recv_line(timeout=1.0)
                        if not line:
                            continue
                        try:
                            data = json.loads(line)
                            if data.get("type") == "user_list_resp":
                                resp = data
                                break
                        except Exception:
                            pass

                    if not resp or resp.get("status") != "ok" or resp.get("id") != f"q-{thread_id}":
                        con_errors.append(f"Thread {thread_id} list query failed: {resp}")

                    # Disconnect
                    disc = json.dumps({"version": 1, "type": "disconnect_req", "id": f"disc-{thread_id}"}) + "\n"
                    sock.sendall(disc.encode('utf-8'))
                    time.sleep(0.05)
                    sock.close()
                except Exception as ex:
                    con_errors.append(f"Thread {thread_id} exception: {ex}")

            threads = []
            for i in range(num_con_threads):
                t = threading.Thread(target=worker_concurrent_task, args=(i,))
                threads.append(t)
                t.start()

            for t in threads:
                t.join(timeout=5.0)

            if len(con_errors) == 0:
                results["Test 32: Concurrent registration, query, and disconnection thread-safety"] = "Passed"
                print(f"Test 32 PASSED: All {num_con_threads} concurrent threads completed successfully without corruption.")
            else:
                results["Test 32: Concurrent registration, query, and disconnection thread-safety"] = f"Failed ({len(con_errors)} errors: {con_errors[:3]})"
                print(f"Test 32 FAILED: {con_errors}")
        except Exception as e:
            results["Test 32: Concurrent registration, query, and disconnection thread-safety"] = f"Failed ({e})"
            print("Test 32 FAILED with exception:", e)

        def recv_matching(sock, expected_types, timeout=3.0):
            start = time.time()
            while time.time() - start < timeout:
                line = sock.recv_line(timeout=max(0.2, timeout - (time.time() - start)))
                if not line:
                    continue
                try:
                    data = json.loads(line)
                    if data.get("type") in expected_types:
                        return data
                except Exception:
                    pass
            return None

        # Test 33: Private messaging delivery, correlation ID, message ID, timestamp, and isolation
        print("\n--- Test 33: Private messaging delivery, correlation ID, and isolation ---")
        try:
            s_dm_a = BufferedSocket()
            s_dm_a.connect((HOST, PORT))
            s_dm_a.sendall((json.dumps({"version": 1, "type": "register_req", "username": "DmAlice33", "id": "reg-dma"}) + "\n").encode('utf-8'))
            s_dm_a.recv_line(timeout=2.0)

            s_dm_b = BufferedSocket()
            s_dm_b.connect((HOST, PORT))
            s_dm_b.sendall((json.dumps({"version": 1, "type": "register_req", "username": "DmBob33", "id": "reg-dmb"}) + "\n").encode('utf-8'))
            s_dm_b.recv_line(timeout=2.0)

            s_dm_e = BufferedSocket()
            s_dm_e.connect((HOST, PORT))
            s_dm_e.sendall((json.dumps({"version": 1, "type": "register_req", "username": "DmEve33", "id": "reg-dme"}) + "\n").encode('utf-8'))
            s_dm_e.recv_line(timeout=2.0)

            time.sleep(0.1)

            # Alice sends DM to Bob
            dm_req = json.dumps({"version": 1, "type": "msg_direct_req", "id": "req-dm-33", "to": "DmBob33", "content": "Secret 12345"}) + "\n"
            s_dm_a.sendall(dm_req.encode('utf-8'))

            ack_data = recv_matching(s_dm_a, ["msg_direct_ack"], timeout=2.0)
            dm_data = recv_matching(s_dm_b, ["msg_direct"], timeout=2.0)
            eve_line = s_dm_e.recv_line(timeout=0.6)

            ack_ok = (ack_data is not None and
                      ack_data.get("status") == "ok" and
                      ack_data.get("id") == "req-dm-33" and
                      ack_data.get("to") == "DmBob33" and
                      isinstance(ack_data.get("msg_id"), str) and
                      ack_data.get("msg_id").startswith("msg-") and
                      isinstance(ack_data.get("timestamp"), int) and
                      ack_data.get("timestamp") > 0)

            dm_ok = (dm_data is not None and
                     dm_data.get("from") == "DmAlice33" and
                     dm_data.get("to") == "DmBob33" and
                     dm_data.get("content") == "Secret 12345" and
                     ack_data is not None and
                     dm_data.get("msg_id") == ack_data.get("msg_id"))

            eve_ok = (eve_line == "" or "Secret 12345" not in eve_line)

            if ack_ok and dm_ok and eve_ok:
                results["Test 33: Private messaging delivery, correlation ID, and isolation"] = "Passed"
                print("Test 33 PASSED: Direct message delivered to recipient with matching correlation ID and zero leak.")
            else:
                results["Test 33: Private messaging delivery, correlation ID, and isolation"] = f"Failed (ack={ack_data}, dm={dm_data}, eve='{eve_line}')"
                print("Test 33 FAILED")

            s_dm_a.close()
            s_dm_b.close()
            s_dm_e.close()
        except Exception as e:
            results["Test 33: Private messaging delivery, correlation ID, and isolation"] = f"Failed ({e})"
            print("Test 33 FAILED with exception:", e)

        # Test 34: Private messaging recipient resolution and error handling
        print("\n--- Test 34: Private messaging recipient resolution and error handling ---")
        try:
            t34_passed = True
            t34_details = []

            s_a34 = BufferedSocket()
            s_a34.connect((HOST, PORT))
            s_a34.sendall((json.dumps({"version": 1, "type": "register_req", "username": "Alice34", "id": "reg-a34"}) + "\n").encode('utf-8'))
            s_a34.recv_line(timeout=2.0)

            s_b34 = BufferedSocket()
            s_b34.connect((HOST, PORT))
            s_b34.sendall((json.dumps({"version": 1, "type": "register_req", "username": "Bob34", "id": "reg-b34"}) + "\n").encode('utf-8'))
            s_b34.recv_line(timeout=2.0)

            # 34A: Nonexistent recipient
            s_a34.sendall((json.dumps({"version": 1, "type": "msg_direct_req", "id": "req-34a", "to": "NoSuchUser", "content": "Hi"}) + "\n").encode('utf-8'))
            resp_34a = recv_matching(s_a34, ["error"], timeout=2.0)
            if resp_34a and resp_34a.get("code") == "ERR_RECIPIENT_NOT_FOUND" and resp_34a.get("id") == "req-34a":
                t34_details.append("34A (nonexistent recipient rejected with ERR_RECIPIENT_NOT_FOUND) OK")
            else:
                t34_passed = False
                t34_details.append(f"34A failed: {resp_34a}")

            # 34B: Self-messaging
            s_a34.sendall((json.dumps({"version": 1, "type": "msg_direct_req", "id": "req-34b", "to": "Alice34", "content": "Note to self"}) + "\n").encode('utf-8'))
            resp_34b = recv_matching(s_a34, ["error"], timeout=2.0)
            if resp_34b and resp_34b.get("code") == "ERR_CANNOT_MESSAGE_SELF" and resp_34b.get("id") == "req-34b":
                t34_details.append("34B (self-messaging rejected with ERR_CANNOT_MESSAGE_SELF) OK")
            else:
                t34_passed = False
                t34_details.append(f"34B failed: {resp_34b}")

            # 34C: Invalid username format
            s_a34.sendall((json.dumps({"version": 1, "type": "msg_direct_req", "id": "req-34c", "to": "Invalid User!", "content": "Hi"}) + "\n").encode('utf-8'))
            resp_34c = recv_matching(s_a34, ["error"], timeout=2.0)
            if resp_34c and resp_34c.get("code") == "ERR_INVALID_USERNAME" and resp_34c.get("id") == "req-34c":
                t34_details.append("34C (invalid username format rejected with ERR_INVALID_USERNAME) OK")
            else:
                t34_passed = False
                t34_details.append(f"34C failed: {resp_34c}")

            # 34D: Case-insensitive recipient matching
            s_a34.sendall((json.dumps({"version": 1, "type": "msg_direct_req", "id": "req-34d", "to": "bOb34", "content": "Case insensitive"}) + "\n").encode('utf-8'))
            ack_34d = recv_matching(s_a34, ["msg_direct_ack"], timeout=2.0)
            dm_34d = recv_matching(s_b34, ["msg_direct"], timeout=2.0)
            if ack_34d and dm_34d and dm_34d.get("content") == "Case insensitive" and dm_34d.get("to") == "Bob34":
                t34_details.append("34D (case-insensitive username resolution verified) OK")
            else:
                t34_passed = False
                t34_details.append(f"34D failed: ack={ack_34d}, dm={dm_34d}")

            # 34E: Recipient disconnect during delivery does not crash server
            s_b34.set_linger_abort()
            s_b34.close()
            time.sleep(0.1)
            s_a34.sendall((json.dumps({"version": 1, "type": "msg_direct_req", "id": "req-34e", "to": "Bob34", "content": "Are you there?"}) + "\n").encode('utf-8'))
            resp_34e = recv_matching(s_a34, ["error"], timeout=2.0)
            if resp_34e and resp_34e.get("code") == "ERR_RECIPIENT_NOT_FOUND":
                t34_details.append("34E (disconnected recipient safely handled without server crash) OK")
            else:
                t34_passed = False
                t34_details.append(f"34E failed: {resp_34e}")

            s_a34.close()

            if t34_passed:
                results["Test 34: Private messaging recipient resolution and error handling"] = "Passed"
                print(f"Test 34 PASSED: {'; '.join(t34_details)}")
            else:
                results["Test 34: Private messaging recipient resolution and error handling"] = f"Failed ({'; '.join(t34_details)})"
                print(f"Test 34 FAILED: {'; '.join(t34_details)}")
        except Exception as e:
            results["Test 34: Private messaging recipient resolution and error handling"] = f"Failed ({e})"
            print("Test 34 FAILED with exception:", e)

        # Test 35: Private messaging wire limit bounding and privacy preservation
        print("\n--- Test 35: Private messaging wire limit bounding and privacy preservation ---")
        try:
            t35_passed = True
            t35_details = []

            s_a35 = BufferedSocket()
            s_a35.connect((HOST, PORT))
            s_a35.sendall((json.dumps({"version": 1, "type": "register_req", "username": "Alice35", "id": "reg-a35"}) + "\n").encode('utf-8'))
            s_a35.recv_line(timeout=2.0)

            s_b35 = BufferedSocket()
            s_b35.connect((HOST, PORT))
            s_b35.sendall((json.dumps({"version": 1, "type": "register_req", "username": "Bob35", "id": "reg-b35"}) + "\n").encode('utf-8'))
            s_b35.recv_line(timeout=2.0)

            # 35A: Oversized serialized payload (> 2048 bytes due to escaping)
            big_content = '"' * 980
            s_a35.sendall((json.dumps({"version": 1, "type": "msg_direct_req", "id": "req-35a", "to": "Bob35", "content": big_content}) + "\n").encode('utf-8'))
            resp_35a = recv_matching(s_a35, ["error"], timeout=2.0)
            bob_leak = s_b35.recv_line(timeout=0.6)
            if resp_35a and resp_35a.get("code") == "ERR_MESSAGE_TOO_LARGE" and resp_35a.get("id") == "req-35a" and (bob_leak == "" or "type" not in bob_leak):
                t35_details.append("35A (oversized private message rejected with ERR_MESSAGE_TOO_LARGE; no recipient leak) OK")
            else:
                t35_passed = False
                t35_details.append(f"35A failed: resp={resp_35a}, leak='{bob_leak}'")

            # 35B: Bounded payload that fits within 2048 bytes wire
            fit_content = '"' * 700
            s_a35.sendall((json.dumps({"version": 1, "type": "msg_direct_req", "id": "req-35b", "to": "Bob35", "content": fit_content}) + "\n").encode('utf-8'))
            ack_35b = recv_matching(s_a35, ["msg_direct_ack"], timeout=2.0)
            dm_35b = recv_matching(s_b35, ["msg_direct"], timeout=2.0)
            if ack_35b and dm_35b and dm_35b.get("content") == fit_content:
                t35_details.append("35B (bounded private message delivered successfully) OK")
            else:
                t35_passed = False
                t35_details.append(f"35B failed: ack={ack_35b}, dm={dm_35b}")

            s_a35.close()
            s_b35.close()

            if t35_passed:
                results["Test 35: Private messaging wire limit bounding and privacy preservation"] = "Passed"
                print(f"Test 35 PASSED: {'; '.join(t35_details)}")
            else:
                results["Test 35: Private messaging wire limit bounding and privacy preservation"] = f"Failed ({'; '.join(t35_details)})"
                print(f"Test 35 FAILED: {'; '.join(t35_details)}")
        except Exception as e:
            results["Test 35: Private messaging wire limit bounding and privacy preservation"] = f"Failed ({e})"
            print("Test 35 FAILED with exception:", e)

        # Test 36: Chat room lifecycle (creation, duplicate rejection, join, leave, auto-join, empty cleanup)
        print("\n--- Test 36: Chat room lifecycle ---")
        try:
            t36_passed = True
            t36_details = []

            s_u1 = BufferedSocket()
            s_u1.connect((HOST, PORT))
            s_u1.sendall((json.dumps({"version": 1, "type": "register_req", "username": "RoomUser1", "id": "reg-ru1"}) + "\n").encode('utf-8'))
            s_u1.recv_line(timeout=2.0)

            s_u2 = BufferedSocket()
            s_u2.connect((HOST, PORT))
            s_u2.sendall((json.dumps({"version": 1, "type": "register_req", "username": "RoomUser2", "id": "reg-ru2"}) + "\n").encode('utf-8'))
            s_u2.recv_line(timeout=2.0)

            # 36A: Create room #general and auto-join
            s_u1.sendall((json.dumps({"version": 1, "type": "room_create_req", "id": "req-cr-1", "room": "#general"}) + "\n").encode('utf-8'))
            cr_resp = recv_matching(s_u1, ["room_create_resp"], timeout=2.0)
            if cr_resp and cr_resp.get("status") == "ok" and cr_resp.get("room") == "#general" and cr_resp.get("id") == "req-cr-1":
                t36_details.append("36A (room creation succeeded) OK")
            else:
                t36_passed = False
                t36_details.append(f"36A failed: {cr_resp}")

            # Verify auto-join via room_members_req
            s_u1.sendall((json.dumps({"version": 1, "type": "room_members_req", "id": "req-mem-1", "room": "#general"}) + "\n").encode('utf-8'))
            mem_resp = recv_matching(s_u1, ["room_members_resp"], timeout=2.0)
            if mem_resp and "RoomUser1" in mem_resp.get("members", []):
                t36_details.append("36A-2 (creator auto-joined) OK")
            else:
                t36_passed = False
                t36_details.append(f"36A-2 failed: {mem_resp}")

            # 36B: Duplicate room creation rejected (case-insensitive)
            s_u2.sendall((json.dumps({"version": 1, "type": "room_create_req", "id": "req-cr-dup", "room": "#GENERAL"}) + "\n").encode('utf-8'))
            dup_resp = recv_matching(s_u2, ["error"], timeout=2.0)
            if dup_resp and dup_resp.get("code") == "ERR_ROOM_ALREADY_EXISTS":
                t36_details.append("36B (duplicate room creation rejected with ERR_ROOM_ALREADY_EXISTS) OK")
            else:
                t36_passed = False
                t36_details.append(f"36B failed: {dup_resp}")

            # 36C: User 2 joins room
            s_u2.sendall((json.dumps({"version": 1, "type": "room_join_req", "id": "req-jn-2", "room": "#general"}) + "\n").encode('utf-8'))
            jn_resp = recv_matching(s_u2, ["room_join_resp"], timeout=2.0)
            u1_got_join = recv_matching(s_u1, ["room_user_joined"], timeout=2.0)
            if jn_resp and jn_resp.get("status") == "ok" and u1_got_join and u1_got_join.get("username") == "RoomUser2":
                t36_details.append("36C (room join and room_user_joined broadcast verified) OK")
            else:
                t36_passed = False
                t36_details.append(f"36C failed: jn={jn_resp}, u1_got={u1_got_join}")

            # 36D: User 2 leaves room
            s_u2.sendall((json.dumps({"version": 1, "type": "room_leave_req", "id": "req-lv-2", "room": "#general"}) + "\n").encode('utf-8'))
            lv_resp = recv_matching(s_u2, ["room_leave_resp"], timeout=2.0)
            u1_got_leave = recv_matching(s_u1, ["room_user_left"], timeout=2.0)
            if lv_resp and lv_resp.get("status") == "ok" and u1_got_leave and u1_got_leave.get("username") == "RoomUser2":
                t36_details.append("36D (room leave and room_user_left broadcast verified) OK")
            else:
                t36_passed = False
                t36_details.append(f"36D failed: lv={lv_resp}, u1_got={u1_got_leave}")

            # 36E: Empty room auto-deletion
            s_u1.sendall((json.dumps({"version": 1, "type": "room_leave_req", "id": "req-lv-1", "room": "#general"}) + "\n").encode('utf-8'))
            recv_matching(s_u1, ["room_leave_resp"], timeout=2.0)

            s_u1.sendall((json.dumps({"version": 1, "type": "room_list_req", "id": "req-rlist-chk"}) + "\n").encode('utf-8'))
            rlist_resp = recv_matching(s_u1, ["room_list_resp"], timeout=2.0)
            if rlist_resp and "#general" not in rlist_resp.get("rooms", []):
                t36_details.append("36E (empty room automatically deleted) OK")
            else:
                t36_passed = False
                t36_details.append(f"36E failed: {rlist_resp}")

            # 36F: Recreate deleted room
            s_u2.sendall((json.dumps({"version": 1, "type": "room_create_req", "id": "req-cr-re", "room": "#general"}) + "\n").encode('utf-8'))
            re_resp = recv_matching(s_u2, ["room_create_resp"], timeout=2.0)
            if re_resp and re_resp.get("status") == "ok":
                t36_details.append("36F (re-creation of empty deleted room succeeded) OK")
            else:
                t36_passed = False
                t36_details.append(f"36F failed: {re_resp}")

            s_u1.close()
            s_u2.close()

            if t36_passed:
                results["Test 36: Chat room lifecycle (create, duplicate rejection, join, leave, empty cleanup)"] = "Passed"
                print(f"Test 36 PASSED: {'; '.join(t36_details)}")
            else:
                results["Test 36: Chat room lifecycle (create, duplicate rejection, join, leave, empty cleanup)"] = f"Failed ({'; '.join(t36_details)})"
                print(f"Test 36 FAILED: {'; '.join(t36_details)}")
        except Exception as e:
            results["Test 36: Chat room lifecycle (create, duplicate rejection, join, leave, empty cleanup)"] = f"Failed ({e})"
            print("Test 36 FAILED with exception:", e)

        # Test 37: Chat room messaging, sender ACK policy, and member isolation
        print("\n--- Test 37: Chat room messaging, sender ACK policy, and member isolation ---")
        try:
            s_a37 = BufferedSocket()
            s_a37.connect((HOST, PORT))
            s_a37.sendall((json.dumps({"version": 1, "type": "register_req", "username": "Alice37", "id": "reg-a37"}) + "\n").encode('utf-8'))
            s_a37.recv_line(timeout=2.0)

            s_b37 = BufferedSocket()
            s_b37.connect((HOST, PORT))
            s_b37.sendall((json.dumps({"version": 1, "type": "register_req", "username": "Bob37", "id": "reg-b37"}) + "\n").encode('utf-8'))
            s_b37.recv_line(timeout=2.0)

            s_c37 = BufferedSocket()
            s_c37.connect((HOST, PORT))
            s_c37.sendall((json.dumps({"version": 1, "type": "register_req", "username": "Charlie37", "id": "reg-c37"}) + "\n").encode('utf-8'))
            s_c37.recv_line(timeout=2.0)

            s_d37 = BufferedSocket()
            s_d37.connect((HOST, PORT))
            s_d37.sendall((json.dumps({"version": 1, "type": "register_req", "username": "David37", "id": "reg-d37"}) + "\n").encode('utf-8'))
            s_d37.recv_line(timeout=2.0)

            s_a37.sendall((json.dumps({"version": 1, "type": "room_create_req", "id": "c-alpha", "room": "#alpha"}) + "\n").encode('utf-8'))
            recv_matching(s_a37, ["room_create_resp"], timeout=2.0)

            s_b37.sendall((json.dumps({"version": 1, "type": "room_join_req", "id": "j-alpha", "room": "#alpha"}) + "\n").encode('utf-8'))
            recv_matching(s_b37, ["room_join_resp"], timeout=2.0)
            recv_matching(s_a37, ["room_user_joined"], timeout=2.0)

            s_c37.sendall((json.dumps({"version": 1, "type": "room_create_req", "id": "c-beta", "room": "#beta"}) + "\n").encode('utf-8'))
            recv_matching(s_c37, ["room_create_resp"], timeout=2.0)

            s_a37.sendall((json.dumps({"version": 1, "type": "room_msg_req", "id": "req-rmsg-37", "room": "#alpha", "content": "Alpha team message"}) + "\n").encode('utf-8'))

            ack_a = recv_matching(s_a37, ["room_msg_ack"], timeout=2.0)
            a_leak = s_a37.recv_line(timeout=0.4)
            b_msg = recv_matching(s_b37, ["room_msg"], timeout=2.0)
            c_leak = s_c37.recv_line(timeout=0.4)
            d_leak = s_d37.recv_line(timeout=0.4)

            ack_ok = (ack_a is not None and
                      ack_a.get("status") == "ok" and
                      ack_a.get("id") == "req-rmsg-37" and
                      ack_a.get("room") == "#alpha" and
                      isinstance(ack_a.get("msg_id"), str) and
                      ack_a.get("msg_id").startswith("msg-"))

            b_ok = (b_msg is not None and
                    b_msg.get("from") == "Alice37" and
                    b_msg.get("room") == "#alpha" and
                    b_msg.get("content") == "Alpha team message" and
                    ack_a is not None and
                    b_msg.get("msg_id") == ack_a.get("msg_id"))

            isolation_ok = (("Alpha team message" not in a_leak) and
                            ("Alpha team message" not in c_leak) and
                            ("Alpha team message" not in d_leak))

            if ack_ok and b_ok and isolation_ok:
                results["Test 37: Chat room messaging, sender ACK policy, and member isolation"] = "Passed"
                print("Test 37 PASSED: Room message delivered only to room peers; sender received ACK only; others received zero leak.")
            else:
                results["Test 37: Chat room messaging, sender ACK policy, and member isolation"] = f"Failed (ack={ack_a}, b={b_msg}, c_leak='{c_leak}')"
                print("Test 37 FAILED")

            s_a37.close()
            s_b37.close()
            s_c37.close()
            s_d37.close()
        except Exception as e:
            results["Test 37: Chat room messaging, sender ACK policy, and member isolation"] = f"Failed ({e})"
            print("Test 37 FAILED with exception:", e)

        # Test 38: Chat room authorization and non-member rejection
        print("\n--- Test 38: Chat room authorization and non-member rejection ---")
        try:
            t38_passed = True
            t38_details = []

            s_a38 = BufferedSocket()
            s_a38.connect((HOST, PORT))
            s_a38.sendall((json.dumps({"version": 1, "type": "register_req", "username": "Alice38", "id": "reg-a38"}) + "\n").encode('utf-8'))
            s_a38.recv_line(timeout=2.0)

            s_b38 = BufferedSocket()
            s_b38.connect((HOST, PORT))
            s_b38.sendall((json.dumps({"version": 1, "type": "register_req", "username": "Bob38", "id": "reg-b38"}) + "\n").encode('utf-8'))
            s_b38.recv_line(timeout=2.0)

            s_a38.sendall((json.dumps({"version": 1, "type": "room_create_req", "id": "c-priv", "room": "#private_room"}) + "\n").encode('utf-8'))
            recv_matching(s_a38, ["room_create_resp"], timeout=2.0)

            # 38A: Non-member Bob attempts to send room_msg_req -> ERR_NOT_IN_ROOM
            s_b38.sendall((json.dumps({"version": 1, "type": "room_msg_req", "id": "req-38a", "room": "#private_room", "content": "I am not a member"}) + "\n").encode('utf-8'))
            resp_38a = recv_matching(s_b38, ["error"], timeout=2.0)
            if resp_38a and resp_38a.get("code") == "ERR_NOT_IN_ROOM":
                t38_details.append("38A (non-member message rejected with ERR_NOT_IN_ROOM) OK")
            else:
                t38_passed = False
                t38_details.append(f"38A failed: {resp_38a}")

            # 38B: Non-member Bob attempts to leave room -> ERR_NOT_IN_ROOM
            s_b38.sendall((json.dumps({"version": 1, "type": "room_leave_req", "id": "req-38b", "room": "#private_room"}) + "\n").encode('utf-8'))
            resp_38b = recv_matching(s_b38, ["error"], timeout=2.0)
            if resp_38b and resp_38b.get("code") == "ERR_NOT_IN_ROOM":
                t38_details.append("38B (non-member leave rejected with ERR_NOT_IN_ROOM) OK")
            else:
                t38_passed = False
                t38_details.append(f"38B failed: {resp_38b}")

            # 38C: Non-member Bob attempts to query room members -> ERR_NOT_IN_ROOM
            s_b38.sendall((json.dumps({"version": 1, "type": "room_members_req", "id": "req-38c", "room": "#private_room"}) + "\n").encode('utf-8'))
            resp_38c = recv_matching(s_b38, ["error"], timeout=2.0)
            if resp_38c and resp_38c.get("code") == "ERR_NOT_IN_ROOM":
                t38_details.append("38C (non-member query rejected with ERR_NOT_IN_ROOM) OK")
            else:
                t38_passed = False
                t38_details.append(f"38C failed: {resp_38c}")

            # 38D: Message to nonexistent room -> ERR_ROOM_NOT_FOUND
            s_a38.sendall((json.dumps({"version": 1, "type": "room_msg_req", "id": "req-38d", "room": "#ghost_room", "content": "Boo"}) + "\n").encode('utf-8'))
            resp_38d = recv_matching(s_a38, ["error"], timeout=2.0)
            if resp_38d and resp_38d.get("code") == "ERR_ROOM_NOT_FOUND":
                t38_details.append("38D (nonexistent room message rejected with ERR_ROOM_NOT_FOUND) OK")
            else:
                t38_passed = False
                t38_details.append(f"38D failed: {resp_38d}")

            s_a38.close()
            s_b38.close()

            if t38_passed:
                results["Test 38: Chat room authorization and non-member rejection"] = "Passed"
                print(f"Test 38 PASSED: {'; '.join(t38_details)}")
            else:
                results["Test 38: Chat room authorization and non-member rejection"] = f"Failed ({'; '.join(t38_details)})"
                print(f"Test 38 FAILED: {'; '.join(t38_details)}")
        except Exception as e:
            results["Test 38: Chat room authorization and non-member rejection"] = f"Failed ({e})"
            print("Test 38 FAILED with exception:", e)

        # Test 39: Chat room membership events and disconnect cleanup
        print("\n--- Test 39: Chat room membership events and disconnect cleanup ---")
        try:
            t39_passed = True
            t39_details = []

            s_b39 = BufferedSocket()
            s_b39.connect((HOST, PORT))
            s_b39.sendall((json.dumps({"version": 1, "type": "register_req", "username": "Bob39", "id": "reg-b39"}) + "\n").encode('utf-8'))
            s_b39.recv_line(timeout=2.0)

            s_a39 = BufferedSocket()
            s_a39.connect((HOST, PORT))
            s_a39.sendall((json.dumps({"version": 1, "type": "register_req", "username": "Alice39", "id": "reg-a39"}) + "\n").encode('utf-8'))
            s_a39.recv_line(timeout=2.0)

            s_b39.sendall((json.dumps({"version": 1, "type": "room_create_req", "id": "c-lounge", "room": "#lounge"}) + "\n").encode('utf-8'))
            recv_matching(s_b39, ["room_create_resp"], timeout=2.0)

            s_a39.sendall((json.dumps({"version": 1, "type": "room_join_req", "id": "j-lounge", "room": "#lounge"}) + "\n").encode('utf-8'))
            recv_matching(s_a39, ["room_join_resp"], timeout=2.0)
            joined_ev = recv_matching(s_b39, ["room_user_joined"], timeout=2.0)
            if joined_ev and joined_ev.get("room") == "#lounge" and joined_ev.get("username") == "Alice39":
                t39_details.append("39A (room_user_joined event verified) OK")
            else:
                t39_passed = False
                t39_details.append(f"39A failed: {joined_ev}")

            # Alice abruptly disconnects
            s_a39.close()
            time.sleep(0.1)

            left_ev = recv_matching(s_b39, ["room_user_left"], timeout=2.0)
            if left_ev and left_ev.get("room") == "#lounge" and left_ev.get("username") == "Alice39":
                t39_details.append("39B (disconnect triggers room_user_left to remaining members) OK")
            else:
                t39_passed = False
                t39_details.append(f"39B failed: {left_ev}")

            s_b39.sendall((json.dumps({"version": 1, "type": "room_members_req", "id": "m-chk", "room": "#lounge"}) + "\n").encode('utf-8'))
            m_chk = recv_matching(s_b39, ["room_members_resp"], timeout=2.0)
            if m_chk and m_chk.get("members") == ["Bob39"]:
                t39_details.append("39C (disconnected user removed from room members) OK")
            else:
                t39_passed = False
                t39_details.append(f"39C failed: {m_chk}")

            s_b39.close()

            if t39_passed:
                results["Test 39: Chat room membership events and disconnect cleanup"] = "Passed"
                print(f"Test 39 PASSED: {'; '.join(t39_details)}")
            else:
                results["Test 39: Chat room membership events and disconnect cleanup"] = f"Failed ({'; '.join(t39_details)})"
                print(f"Test 39 FAILED: {'; '.join(t39_details)}")
        except Exception as e:
            results["Test 39: Chat room membership events and disconnect cleanup"] = f"Failed ({e})"
            print("Test 39 FAILED with exception:", e)

        # Test 40: Chat room listing, member listing, and pagination
        print("\n--- Test 40: Chat room listing, member listing, and pagination ---")
        try:
            t40_passed = True
            t40_details = []

            s_a40 = BufferedSocket()
            s_a40.connect((HOST, PORT))
            s_a40.sendall((json.dumps({"version": 1, "type": "register_req", "username": "Alice40", "id": "reg-a40"}) + "\n").encode('utf-8'))
            s_a40.recv_line(timeout=2.0)

            s_b40 = BufferedSocket()
            s_b40.connect((HOST, PORT))
            s_b40.sendall((json.dumps({"version": 1, "type": "register_req", "username": "Bob40", "id": "reg-b40"}) + "\n").encode('utf-8'))
            s_b40.recv_line(timeout=2.0)

            for rname in ["#r1", "#r2", "#r3"]:
                s_a40.sendall((json.dumps({"version": 1, "type": "room_create_req", "id": f"c-{rname}", "room": rname}) + "\n").encode('utf-8'))
                recv_matching(s_a40, ["room_create_resp"], timeout=2.0)

            s_b40.sendall((json.dumps({"version": 1, "type": "room_join_req", "id": "j-r1", "room": "#r1"}) + "\n").encode('utf-8'))
            recv_matching(s_b40, ["room_join_resp"], timeout=2.0)

            s_a40.sendall((json.dumps({"version": 1, "type": "room_list_req", "id": "rl-p1", "page": 1, "limit": 2}) + "\n").encode('utf-8'))
            rl_p1 = recv_matching(s_a40, ["room_list_resp"], timeout=2.0)
            if rl_p1 and rl_p1.get("count") == 2 and rl_p1.get("total_pages") >= 2:
                t40_details.append("40A (room_list_req page 1 slice verified) OK")
            else:
                t40_passed = False
                t40_details.append(f"40A failed: {rl_p1}")

            s_a40.sendall((json.dumps({"version": 1, "type": "room_members_req", "id": "rm-p1", "room": "#r1", "page": 1, "limit": 1}) + "\n").encode('utf-8'))
            rm_p1 = recv_matching(s_a40, ["room_members_resp"], timeout=2.0)
            if rm_p1 and rm_p1.get("count") == 1 and rm_p1.get("total_members") == 2 and rm_p1.get("total_pages") == 2:
                t40_details.append("40B (room_members_req pagination slice verified) OK")
            else:
                t40_passed = False
                t40_details.append(f"40B failed: {rm_p1}")

            s_a40.close()
            s_b40.close()

            if t40_passed:
                results["Test 40: Chat room listing, member listing, and pagination"] = "Passed"
                print(f"Test 40 PASSED: {'; '.join(t40_details)}")
            else:
                results["Test 40: Chat room listing, member listing, and pagination"] = f"Failed ({'; '.join(t40_details)})"
                print(f"Test 40 FAILED: {'; '.join(t40_details)}")
        except Exception as e:
            results["Test 40: Chat room listing, member listing, and pagination"] = f"Failed ({e})"
            print("Test 40 FAILED with exception:", e)

        # Test 41: Chat room wire limit bounding
        print("\n--- Test 41: Chat room wire limit bounding ---")
        try:
            t41_passed = True
            t41_details = []

            s_a41 = BufferedSocket()
            s_a41.connect((HOST, PORT))
            s_a41.sendall((json.dumps({"version": 1, "type": "register_req", "username": "Alice41", "id": "reg-a41"}) + "\n").encode('utf-8'))
            s_a41.recv_line(timeout=2.0)

            s_b41 = BufferedSocket()
            s_b41.connect((HOST, PORT))
            s_b41.sendall((json.dumps({"version": 1, "type": "register_req", "username": "Bob41", "id": "reg-b41"}) + "\n").encode('utf-8'))
            s_b41.recv_line(timeout=2.0)

            s_a41.sendall((json.dumps({"version": 1, "type": "room_create_req", "id": "c-w41", "room": "#wire41"}) + "\n").encode('utf-8'))
            recv_matching(s_a41, ["room_create_resp"], timeout=2.0)

            s_b41.sendall((json.dumps({"version": 1, "type": "room_join_req", "id": "j-w41", "room": "#wire41"}) + "\n").encode('utf-8'))
            recv_matching(s_b41, ["room_join_resp"], timeout=2.0)
            recv_matching(s_a41, ["room_user_joined"], timeout=2.0)

            big_content = '"' * 980
            s_a41.sendall((json.dumps({"version": 1, "type": "room_msg_req", "id": "req-41-big", "room": "#wire41", "content": big_content}) + "\n").encode('utf-8'))
            resp_41a = recv_matching(s_a41, ["error"], timeout=2.0)
            b_leak = s_b41.recv_line(timeout=0.5)

            if resp_41a and resp_41a.get("code") == "ERR_MESSAGE_TOO_LARGE" and (b_leak == "" or "type" not in b_leak):
                t41_details.append("41A (oversized room message rejected with ERR_MESSAGE_TOO_LARGE; no peer leak) OK")
            else:
                t41_passed = False
                t41_details.append(f"41A failed: resp={resp_41a}, b_leak='{b_leak}'")

            s_a41.close()
            s_b41.close()

            if t41_passed:
                results["Test 41: Chat room wire limit bounding"] = "Passed"
                print(f"Test 41 PASSED: {'; '.join(t41_details)}")
            else:
                results["Test 41: Chat room wire limit bounding"] = f"Failed ({'; '.join(t41_details)})"
                print(f"Test 41 FAILED: {'; '.join(t41_details)}")
        except Exception as e:
            results["Test 41: Chat room wire limit bounding"] = f"Failed ({e})"
            print("Test 41 FAILED with exception:", e)

        # Test 42: Concurrent room operations, private messaging, and thread safety under load
        print("\n--- Test 42: Concurrent room operations and private messaging ---")
        try:
            con_errors = []
            num_threads = 6

            def worker_task(tid):
                try:
                    s = BufferedSocket()
                    s.connect((HOST, PORT))
                    uname = f"ConUser42_{tid}"
                    s.sendall((json.dumps({"version": 1, "type": "register_req", "username": uname, "id": f"reg-{tid}"}) + "\n").encode('utf-8'))
                    ack = recv_matching(s, ["register_ack"], timeout=3.0)
                    if not ack or ack.get("username") != uname:
                        con_errors.append(f"Thread {tid} reg failed: {ack}")
                        s.close()
                        return

                    rname = f"#croom_{tid % 2}"
                    if tid % 2 == 0:
                        s.sendall((json.dumps({"version": 1, "type": "room_create_req", "id": f"cr-{tid}", "room": rname}) + "\n").encode('utf-8'))
                        recv_matching(s, ["room_create_resp", "error"], timeout=2.0)
                    else:
                        time.sleep(0.05)
                        s.sendall((json.dumps({"version": 1, "type": "room_join_req", "id": f"jn-{tid}", "room": rname}) + "\n").encode('utf-8'))
                        recv_matching(s, ["room_join_resp", "error"], timeout=2.0)

                    s.sendall((json.dumps({"version": 1, "type": "room_msg_req", "id": f"rm-{tid}", "room": rname, "content": f"Hi from {tid}"}) + "\n").encode('utf-8'))
                    recv_matching(s, ["room_msg_ack", "error"], timeout=2.0)

                    s.sendall((json.dumps({"version": 1, "type": "room_list_req", "id": f"rl-{tid}"}) + "\n").encode('utf-8'))
                    recv_matching(s, ["room_list_resp"], timeout=2.0)

                    time.sleep(0.05)
                    s.close()
                except Exception as ex:
                    con_errors.append(f"Thread {tid} exception: {ex}")

            th_list = []
            for i in range(num_threads):
                t = threading.Thread(target=worker_task, args=(i,))
                th_list.append(t)
                t.start()

            for t in th_list:
                t.join(timeout=6.0)

            if len(con_errors) == 0:
                results["Test 42: Concurrent room operations, private messaging, and thread safety"] = "Passed"
                print(f"Test 42 PASSED: All {num_threads} concurrent threads completed successfully.")
            else:
                results["Test 42: Concurrent room operations, private messaging, and thread safety"] = f"Failed ({con_errors})"
                print(f"Test 42 FAILED: {con_errors}")
        except Exception as e:
            results["Test 42: Concurrent room operations, private messaging, and thread safety"] = f"Failed ({e})"
            print("Test 42 FAILED with exception:", e)

        # Test 43: Cross-protocol isolation (legacy client compatibility)
        print("\n--- Test 43: Cross-protocol isolation ---")
        try:
            t43_passed = True
            t43_details = []

            s_leg43 = BufferedSocket()
            s_leg43.connect((HOST, PORT))
            s_leg43.sendall(b"LegUser43\n")
            leg_ack = s_leg43.recv_line(timeout=2.0)
            assert leg_ack == "OK", f"Legacy reg failed: {leg_ack}"

            s_a43 = BufferedSocket()
            s_a43.connect((HOST, PORT))
            s_a43.sendall((json.dumps({"version": 1, "type": "register_req", "username": "Alice43", "id": "reg-a43"}) + "\n").encode('utf-8'))
            s_a43.recv_line(timeout=2.0)

            s_b43 = BufferedSocket()
            s_b43.connect((HOST, PORT))
            s_b43.sendall((json.dumps({"version": 1, "type": "register_req", "username": "Bob43", "id": "reg-b43"}) + "\n").encode('utf-8'))
            s_b43.recv_line(timeout=2.0)

            time.sleep(0.1)
            while s_leg43.recv_line(timeout=0.2):
                pass

            # Alice sends private message to Bob
            s_a43.sendall((json.dumps({"version": 1, "type": "msg_direct_req", "id": "dm-43", "to": "Bob43", "content": "Private secret 43"}) + "\n").encode('utf-8'))
            recv_matching(s_a43, ["msg_direct_ack"], timeout=2.0)
            recv_matching(s_b43, ["msg_direct"], timeout=2.0)

            # Alice creates room #secret43 and sends room message
            s_a43.sendall((json.dumps({"version": 1, "type": "room_create_req", "id": "c-s43", "room": "#secret43"}) + "\n").encode('utf-8'))
            recv_matching(s_a43, ["room_create_resp"], timeout=2.0)
            s_b43.sendall((json.dumps({"version": 1, "type": "room_join_req", "id": "j-s43", "room": "#secret43"}) + "\n").encode('utf-8'))
            recv_matching(s_b43, ["room_join_resp"], timeout=2.0)
            recv_matching(s_a43, ["room_user_joined"], timeout=2.0)

            s_a43.sendall((json.dumps({"version": 1, "type": "room_msg_req", "id": "rm-43", "room": "#secret43", "content": "Room secret 43"}) + "\n").encode('utf-8'))
            recv_matching(s_a43, ["room_msg_ack"], timeout=2.0)
            recv_matching(s_b43, ["room_msg"], timeout=2.0)

            leg_leak = s_leg43.recv_line(timeout=0.4)
            if leg_leak == "" or ("Private secret" not in leg_leak and "Room secret" not in leg_leak):
                t43_details.append("43A (zero private or room leakage to legacy client) OK")
            else:
                t43_passed = False
                t43_details.append(f"43A failed: legacy got '{leg_leak}'")

            s_a43.sendall((json.dumps({"version": 1, "type": "msg_send", "id": "pub-43", "content": "Hello Public World!"}) + "\n").encode('utf-8'))
            recv_matching(s_a43, ["msg_ack"], timeout=2.0)
            b_pub = recv_matching(s_b43, ["msg_broadcast"], timeout=2.0)

            leg_pub = s_leg43.recv_line(timeout=2.0)
            if "[Alice43]: Hello Public World!" in leg_pub:
                t43_details.append("43B (public broadcast received by legacy client) OK")
            else:
                t43_passed = False
                t43_details.append(f"43B failed: leg_pub='{leg_pub}'")

            s_leg43.close()
            s_a43.close()
            s_b43.close()

            if t43_passed:
                results["Test 43: Cross-protocol isolation (legacy client compatibility)"] = "Passed"
                print(f"Test 43 PASSED: {'; '.join(t43_details)}")
            else:
                results["Test 43: Cross-protocol isolation (legacy client compatibility)"] = f"Failed ({'; '.join(t43_details)})"
                print(f"Test 43 FAILED: {'; '.join(t43_details)}")
        except Exception as e:
            results["Test 43: Cross-protocol isolation (legacy client compatibility)"] = f"Failed ({e})"
            print("Test 43 FAILED with exception:", e)

        # Test 44: Public history retrieval, ordering, empty history, and legacy client message persistence
        print("\n--- Test 44: Public history retrieval, ordering, and empty history ---")
        try:
            t44_passed = True
            t44_details = []

            s_a44 = BufferedSocket()
            s_a44.connect((HOST, PORT))
            s_a44.sendall((json.dumps({"version": 1, "type": "register_req", "username": "HistAlice44", "id": "reg-a44"}) + "\n").encode('utf-8'))
            recv_matching(s_a44, ["register_ack"], timeout=2.0)

            # Sub-test 44A: Empty public history when queried with before_id="msg-1"
            s_a44.sendall((json.dumps({"version": 1, "type": "history_req", "id": "h-empty", "conversation_type": "public", "before_id": "msg-1"}) + "\n").encode('utf-8'))
            empty_resp = recv_matching(s_a44, ["history_resp"], timeout=2.0)
            if empty_resp and empty_resp.get("status") == "ok" and empty_resp.get("messages") == [] and empty_resp.get("has_more") is False:
                t44_details.append("44A (empty history query returns empty array and has_more=false) OK")
            else:
                t44_passed = False
                t44_details.append(f"44A failed: {empty_resp}")

            # Sub-test 44B: Legacy public message + structured public messages stored and retrievable in chronological order
            s_leg44 = BufferedSocket()
            s_leg44.connect((HOST, PORT))
            s_leg44.sendall(b"HistLeg44\n")
            s_leg44.recv_line(timeout=2.0)

            s_leg44.sendall(b"Legacy public msg 44\n")
            time.sleep(0.05)

            s_a44.sendall((json.dumps({"version": 1, "type": "msg_send", "id": "pub-44-1", "content": "Structured public msg 44-1"}) + "\n").encode('utf-8'))
            recv_matching(s_a44, ["msg_ack"], timeout=2.0)

            s_a44.sendall((json.dumps({"version": 1, "type": "msg_send", "id": "pub-44-2", "content": "Structured public msg 44-2"}) + "\n").encode('utf-8'))
            recv_matching(s_a44, ["msg_ack"], timeout=2.0)

            s_a44.sendall((json.dumps({"version": 1, "type": "history_req", "id": "h-pub-44", "conversation_type": "public", "limit": 20}) + "\n").encode('utf-8'))
            pub_hist = recv_matching(s_a44, ["history_resp"], timeout=2.0)

            if pub_hist and pub_hist.get("status") == "ok":
                msgs = pub_hist.get("messages", [])
                contents = [m.get("content") for m in msgs]
                if ("Legacy public msg 44" in contents and
                    "Structured public msg 44-1" in contents and
                    "Structured public msg 44-2" in contents):
                    idx_leg = contents.index("Legacy public msg 44")
                    idx_s1 = contents.index("Structured public msg 44-1")
                    idx_s2 = contents.index("Structured public msg 44-2")
                    if idx_leg < idx_s1 < idx_s2:
                        t44_details.append("44B (legacy and structured public messages retrievable in chronological order) OK")
                    else:
                        t44_passed = False
                        t44_details.append(f"44B failed: order {idx_leg}, {idx_s1}, {idx_s2}")
                else:
                    t44_passed = False
                    t44_details.append(f"44B failed: contents={contents}")
            else:
                t44_passed = False
                t44_details.append(f"44B failed: {pub_hist}")

            s_leg44.close()
            s_a44.close()

            if t44_passed:
                results["Test 44: Public history retrieval, ordering, and empty history"] = "Passed"
                print(f"Test 44 PASSED: {'; '.join(t44_details)}")
            else:
                results["Test 44: Public history retrieval, ordering, and empty history"] = f"Failed ({'; '.join(t44_details)})"
                print(f"Test 44 FAILED: {'; '.join(t44_details)}")
        except Exception as e:
            results["Test 44: Public history retrieval, ordering, and empty history"] = f"Failed ({e})"
            print("Test 44 FAILED with exception:", e)

        # Test 45: Direct message persistence, bi-directional authorized retrieval, and third-party isolation
        print("\n--- Test 45: Direct message persistence, bi-directional retrieval, and third-party isolation ---")
        try:
            t45_passed = True
            t45_details = []

            s_a45 = BufferedSocket()
            s_a45.connect((HOST, PORT))
            s_a45.sendall((json.dumps({"version": 1, "type": "register_req", "username": "HistAlice45", "id": "reg-a45"}) + "\n").encode('utf-8'))
            recv_matching(s_a45, ["register_ack"], timeout=2.0)

            s_b45 = BufferedSocket()
            s_b45.connect((HOST, PORT))
            s_b45.sendall((json.dumps({"version": 1, "type": "register_req", "username": "HistBob45", "id": "reg-b45"}) + "\n").encode('utf-8'))
            recv_matching(s_b45, ["register_ack"], timeout=2.0)

            s_c45 = BufferedSocket()
            s_c45.connect((HOST, PORT))
            s_c45.sendall((json.dumps({"version": 1, "type": "register_req", "username": "HistCharlie45", "id": "reg-c45"}) + "\n").encode('utf-8'))
            recv_matching(s_c45, ["register_ack"], timeout=2.0)

            # Alice sends DM to Bob
            s_a45.sendall((json.dumps({"version": 1, "type": "msg_direct_req", "id": "dm-45-1", "to": "HistBob45", "content": "Secret A to B"}) + "\n").encode('utf-8'))
            recv_matching(s_a45, ["msg_direct_ack"], timeout=2.0)
            recv_matching(s_b45, ["msg_direct"], timeout=2.0)

            # Bob sends DM to Alice
            s_b45.sendall((json.dumps({"version": 1, "type": "msg_direct_req", "id": "dm-45-2", "to": "HistAlice45", "content": "Secret B to A"}) + "\n").encode('utf-8'))
            recv_matching(s_b45, ["msg_direct_ack"], timeout=2.0)
            recv_matching(s_a45, ["msg_direct"], timeout=2.0)

            # Alice retrieves direct history with Bob
            s_a45.sendall((json.dumps({"version": 1, "type": "history_req", "id": "h-dm-a", "conversation_type": "direct", "with": "HistBob45"}) + "\n").encode('utf-8'))
            a_hist = recv_matching(s_a45, ["history_resp"], timeout=2.0)
            a_msgs = [m.get("content") for m in a_hist.get("messages", [])] if a_hist else []

            # Bob retrieves direct history with Alice
            s_b45.sendall((json.dumps({"version": 1, "type": "history_req", "id": "h-dm-b", "conversation_type": "direct", "with": "HistAlice45"}) + "\n").encode('utf-8'))
            b_hist = recv_matching(s_b45, ["history_resp"], timeout=2.0)
            b_msgs = [m.get("content") for m in b_hist.get("messages", [])] if b_hist else []

            if (a_msgs == ["Secret A to B", "Secret B to A"] and
                b_msgs == ["Secret A to B", "Secret B to A"]):
                t45_details.append("45A (Alice and Bob bi-directional direct history identical and ordered) OK")
            else:
                t45_passed = False
                t45_details.append(f"45A failed: a_msgs={a_msgs}, b_msgs={b_msgs}")

            # Charlie (third party) attempts to query direct history with Alice
            s_c45.sendall((json.dumps({"version": 1, "type": "history_req", "id": "h-dm-c1", "conversation_type": "direct", "with": "HistAlice45"}) + "\n").encode('utf-8'))
            c1_hist = recv_matching(s_c45, ["history_resp"], timeout=2.0)
            c1_msgs = c1_hist.get("messages", []) if c1_hist else None

            # Charlie attempts to query direct history with Bob
            s_c45.sendall((json.dumps({"version": 1, "type": "history_req", "id": "h-dm-c2", "conversation_type": "direct", "with": "HistBob45"}) + "\n").encode('utf-8'))
            c2_hist = recv_matching(s_c45, ["history_resp"], timeout=2.0)
            c2_msgs = c2_hist.get("messages", []) if c2_hist else None

            if c1_msgs == [] and c2_msgs == []:
                t45_details.append("45B (third-party Charlie receives 0 direct messages from Alice/Bob conversation) OK")
            else:
                t45_passed = False
                t45_details.append(f"45B failed: c1={c1_msgs}, c2={c2_msgs}")

            s_a45.close()
            s_b45.close()
            s_c45.close()

            if t45_passed:
                results["Test 45: Direct message persistence, bi-directional retrieval, and third-party isolation"] = "Passed"
                print(f"Test 45 PASSED: {'; '.join(t45_details)}")
            else:
                results["Test 45: Direct message persistence, bi-directional retrieval, and third-party isolation"] = f"Failed ({'; '.join(t45_details)})"
                print(f"Test 45 FAILED: {'; '.join(t45_details)}")
        except Exception as e:
            results["Test 45: Direct message persistence, bi-directional retrieval, and third-party isolation"] = f"Failed ({e})"
            print("Test 45 FAILED with exception:", e)

        # Test 46: Room message persistence, membership authorization, and non-member isolation
        print("\n--- Test 46: Room message persistence, membership authorization, and non-member isolation ---")
        try:
            t46_passed = True
            t46_details = []

            s_a46 = BufferedSocket()
            s_a46.connect((HOST, PORT))
            s_a46.sendall((json.dumps({"version": 1, "type": "register_req", "username": "HistAlice46", "id": "reg-a46"}) + "\n").encode('utf-8'))
            recv_matching(s_a46, ["register_ack"], timeout=2.0)

            s_b46 = BufferedSocket()
            s_b46.connect((HOST, PORT))
            s_b46.sendall((json.dumps({"version": 1, "type": "register_req", "username": "HistBob46", "id": "reg-b46"}) + "\n").encode('utf-8'))
            recv_matching(s_b46, ["register_ack"], timeout=2.0)

            s_c46 = BufferedSocket()
            s_c46.connect((HOST, PORT))
            s_c46.sendall((json.dumps({"version": 1, "type": "register_req", "username": "HistCharlie46", "id": "reg-c46"}) + "\n").encode('utf-8'))
            recv_matching(s_c46, ["register_ack"], timeout=2.0)

            # Alice creates room #room46
            s_a46.sendall((json.dumps({"version": 1, "type": "room_create_req", "id": "c-46", "room": "#room46"}) + "\n").encode('utf-8'))
            recv_matching(s_a46, ["room_create_resp"], timeout=2.0)

            # Bob joins room #room46
            s_b46.sendall((json.dumps({"version": 1, "type": "room_join_req", "id": "j-46", "room": "#room46"}) + "\n").encode('utf-8'))
            recv_matching(s_b46, ["room_join_resp"], timeout=2.0)

            # Alice and Bob send messages in #room46
            s_a46.sendall((json.dumps({"version": 1, "type": "room_msg_req", "id": "rm-46-1", "room": "#room46", "content": "Room confidential 1"}) + "\n").encode('utf-8'))
            recv_matching(s_a46, ["room_msg_ack"], timeout=2.0)

            s_b46.sendall((json.dumps({"version": 1, "type": "room_msg_req", "id": "rm-46-2", "room": "#room46", "content": "Room confidential 2"}) + "\n").encode('utf-8'))
            recv_matching(s_b46, ["room_msg_ack"], timeout=2.0)

            # Bob requests room history
            s_b46.sendall((json.dumps({"version": 1, "type": "history_req", "id": "h-rm-b", "conversation_type": "room", "room": "#room46"}) + "\n").encode('utf-8'))
            rm_hist = recv_matching(s_b46, ["history_resp"], timeout=2.0)
            rm_msgs = [m.get("content") for m in rm_hist.get("messages", [])] if rm_hist else []

            if rm_msgs == ["Room confidential 1", "Room confidential 2"]:
                t46_details.append("46A (authorized member retrieves persistent room messages in order) OK")
            else:
                t46_passed = False
                t46_details.append(f"46A failed: rm_msgs={rm_msgs}")

            # Charlie (non-member) requests room history -> ERR_NOT_IN_ROOM
            s_c46.sendall((json.dumps({"version": 1, "type": "history_req", "id": "h-rm-c", "conversation_type": "room", "room": "#room46"}) + "\n").encode('utf-8'))
            c_err = recv_matching(s_c46, ["error"], timeout=2.0)
            if c_err and c_err.get("code") == "ERR_NOT_IN_ROOM":
                t46_details.append("46B (non-member Charlie rejected with ERR_NOT_IN_ROOM) OK")
            else:
                t46_passed = False
                t46_details.append(f"46B failed: {c_err}")

            # Bob leaves the room
            s_b46.sendall((json.dumps({"version": 1, "type": "room_leave_req", "id": "lv-46", "room": "#room46"}) + "\n").encode('utf-8'))
            recv_matching(s_b46, ["room_leave_resp"], timeout=2.0)

            # Bob now requests room history -> ERR_NOT_IN_ROOM
            s_b46.sendall((json.dumps({"version": 1, "type": "history_req", "id": "h-rm-b2", "conversation_type": "room", "room": "#room46"}) + "\n").encode('utf-8'))
            b_err = recv_matching(s_b46, ["error"], timeout=2.0)
            if b_err and b_err.get("code") == "ERR_NOT_IN_ROOM":
                t46_details.append("46C (former member Bob rejected with ERR_NOT_IN_ROOM after leaving) OK")
            else:
                t46_passed = False
                t46_details.append(f"46C failed: {b_err}")

            s_a46.close()
            s_b46.close()
            s_c46.close()

            if t46_passed:
                results["Test 46: Room message persistence, membership authorization, and non-member isolation"] = "Passed"
                print(f"Test 46 PASSED: {'; '.join(t46_details)}")
            else:
                results["Test 46: Room message persistence, membership authorization, and non-member isolation"] = f"Failed ({'; '.join(t46_details)})"
                print(f"Test 46 FAILED: {'; '.join(t46_details)}")
        except Exception as e:
            results["Test 46: Room message persistence, membership authorization, and non-member isolation"] = f"Failed ({e})"
            print("Test 46 FAILED with exception:", e)

        # Test 47: Persistence across server restart and durable message ID uniqueness
        print("\n--- Test 47: Persistence across server restart and durable message ID uniqueness ---")
        try:
            t47_passed = True
            t47_details = []

            s_pre = BufferedSocket()
            s_pre.connect((HOST, PORT))
            s_pre.sendall((json.dumps({"version": 1, "type": "register_req", "username": "HistUser47", "id": "reg-47"}) + "\n").encode('utf-8'))
            recv_matching(s_pre, ["register_ack"], timeout=2.0)

            s_pre.sendall((json.dumps({"version": 1, "type": "msg_send", "id": "m-pre", "content": "Survives Server Restart 47"}) + "\n").encode('utf-8'))
            pre_ack = recv_matching(s_pre, ["msg_ack"], timeout=2.0)
            pre_msg_id = pre_ack.get("msg_id", "")
            id_pre_num = int(pre_msg_id.split("-")[1]) if "-" in pre_msg_id else 0

            s_pre.close()
            time.sleep(0.2)

            # Stop server and restart pointing to the exact same TEST_DB
            stop_server(srv_struct)
            time.sleep(0.5)
            srv_struct = start_server(port=PORT, db_path=TEST_DB)
            time.sleep(0.5)

            s_post = BufferedSocket()
            s_post.connect((HOST, PORT))
            s_post.sendall((json.dumps({"version": 1, "type": "register_req", "username": "HistUser47Post", "id": "reg-47post"}) + "\n").encode('utf-8'))
            recv_matching(s_post, ["register_ack"], timeout=2.0)

            # Query public history after restart
            s_post.sendall((json.dumps({"version": 1, "type": "history_req", "id": "h-post", "conversation_type": "public", "limit": 20}) + "\n").encode('utf-8'))
            post_hist = recv_matching(s_post, ["history_resp"], timeout=2.0)
            post_contents = [m.get("content") for m in post_hist.get("messages", [])] if post_hist else []

            if "Survives Server Restart 47" in post_contents:
                t47_details.append("47A (messages persisted and retrievable across full server restart) OK")
            else:
                t47_passed = False
                t47_details.append(f"47A failed: contents={post_contents}")

            # Send new message and verify message ID continues monotonically
            s_post.sendall((json.dumps({"version": 1, "type": "msg_send", "id": "m-post", "content": "Post-restart message"}) + "\n").encode('utf-8'))
            post_ack = recv_matching(s_post, ["msg_ack"], timeout=2.0)
            post_msg_id = post_ack.get("msg_id", "")
            id_post_num = int(post_msg_id.split("-")[1]) if "-" in post_msg_id else 0

            if id_post_num > id_pre_num:
                t47_details.append(f"47B (durable message ID monotonically resumed: {pre_msg_id} -> {post_msg_id}) OK")
            else:
                t47_passed = False
                t47_details.append(f"47B failed: pre={pre_msg_id}, post={post_msg_id}")

            s_post.close()

            if t47_passed:
                results["Test 47: Persistence across server restart and durable message ID uniqueness"] = "Passed"
                print(f"Test 47 PASSED: {'; '.join(t47_details)}")
            else:
                results["Test 47: Persistence across server restart and durable message ID uniqueness"] = f"Failed ({'; '.join(t47_details)})"
                print(f"Test 47 FAILED: {'; '.join(t47_details)}")
        except Exception as e:
            results["Test 47: Persistence across server restart and durable message ID uniqueness"] = f"Failed ({e})"
            print("Test 47 FAILED with exception:", e)

        # Test 48: Cursor-based pagination (before_id, after_id, zero duplicate/omission invariant)
        print("\n--- Test 48: Cursor-based pagination ---")
        try:
            t48_passed = True
            t48_details = []

            s_a48 = BufferedSocket()
            s_a48.connect((HOST, PORT))
            s_a48.sendall((json.dumps({"version": 1, "type": "register_req", "username": "HistAlice48", "id": "reg-a48"}) + "\n").encode('utf-8'))
            recv_matching(s_a48, ["register_ack"], timeout=2.0)

            s_b48 = BufferedSocket()
            s_b48.connect((HOST, PORT))
            s_b48.sendall((json.dumps({"version": 1, "type": "register_req", "username": "HistBob48", "id": "reg-b48"}) + "\n").encode('utf-8'))
            recv_matching(s_b48, ["register_ack"], timeout=2.0)

            sent_ids = []
            sent_texts = [f"PageTest48 msg #{i}" for i in range(8)]
            for text in sent_texts:
                s_a48.sendall((json.dumps({"version": 1, "type": "msg_direct_req", "id": f"dm48-{text}", "to": "HistBob48", "content": text}) + "\n").encode('utf-8'))
                ack = recv_matching(s_a48, ["msg_direct_ack"], timeout=2.0)
                recv_matching(s_b48, ["msg_direct"], timeout=2.0)
                sent_ids.append(ack.get("msg_id"))

            # Page 1 (latest 3 messages, limit=3)
            s_a48.sendall((json.dumps({"version": 1, "type": "history_req", "id": "p-1", "conversation_type": "direct", "with": "HistBob48", "limit": 3}) + "\n").encode('utf-8'))
            p1 = recv_matching(s_a48, ["history_resp"], timeout=2.0)
            p1_msgs = p1.get("messages", [])
            p1_contents = [m.get("content") for m in p1_msgs]

            # Page 2 (older 3 messages, before_id = oldest ID of page 1)
            cursor1 = p1_msgs[0].get("id")
            s_a48.sendall((json.dumps({"version": 1, "type": "history_req", "id": "p-2", "conversation_type": "direct", "with": "HistBob48", "limit": 3, "before_id": cursor1}) + "\n").encode('utf-8'))
            p2 = recv_matching(s_a48, ["history_resp"], timeout=2.0)
            p2_msgs = p2.get("messages", [])
            p2_contents = [m.get("content") for m in p2_msgs]

            # Page 3 (remaining 2 messages, before_id = oldest ID of page 2)
            cursor2 = p2_msgs[0].get("id")
            s_a48.sendall((json.dumps({"version": 1, "type": "history_req", "id": "p-3", "conversation_type": "direct", "with": "HistBob48", "limit": 3, "before_id": cursor2}) + "\n").encode('utf-8'))
            p3 = recv_matching(s_a48, ["history_resp"], timeout=2.0)
            p3_msgs = p3.get("messages", [])
            p3_contents = [m.get("content") for m in p3_msgs]

            # Reconstruct chronological stream: page 3 + page 2 + page 1
            reconstructed = p3_contents + p2_contents + p1_contents

            if (p1.get("has_more") is True and p2.get("has_more") is True and p3.get("has_more") is False and
                reconstructed == sent_texts):
                t48_details.append("48A (backward cursor pagination via before_id reconstructed all 8 messages without duplicates or omissions) OK")
            else:
                t48_passed = False
                t48_details.append(f"48A failed: p1_has_more={p1.get('has_more')}, p2_has_more={p2.get('has_more')}, p3_has_more={p3.get('has_more')}, reconstructed={reconstructed}")

            # Forward cursor pagination via after_id
            cursor_after = sent_ids[2] # After msg #2
            s_a48.sendall((json.dumps({"version": 1, "type": "history_req", "id": "p-after", "conversation_type": "direct", "with": "HistBob48", "limit": 3, "after_id": cursor_after}) + "\n").encode('utf-8'))
            p_after = recv_matching(s_a48, ["history_resp"], timeout=2.0)
            after_contents = [m.get("content") for m in p_after.get("messages", [])]
            if after_contents == sent_texts[3:6]:
                t48_details.append("48B (forward cursor pagination via after_id returned exact slice) OK")
            else:
                t48_passed = False
                t48_details.append(f"48B failed: after_contents={after_contents}, expected={sent_texts[3:6]}")

            s_a48.close()
            s_b48.close()

            if t48_passed:
                results["Test 48: Cursor-based pagination (before_id and after_id)"] = "Passed"
                print(f"Test 48 PASSED: {'; '.join(t48_details)}")
            else:
                results["Test 48: Cursor-based pagination (before_id and after_id)"] = f"Failed ({'; '.join(t48_details)})"
                print(f"Test 48 FAILED: {'; '.join(t48_details)}")
        except Exception as e:
            results["Test 48: Cursor-based pagination (before_id and after_id)"] = f"Failed ({e})"
            print("Test 48 FAILED with exception:", e)

        # Test 49: Parameter validation and authorization bounds
        print("\n--- Test 49: Parameter validation and authorization bounds ---")
        try:
            t49_passed = True
            t49_details = []

            # 49A: Unregistered history request rejected
            s_unreg49 = BufferedSocket()
            s_unreg49.connect((HOST, PORT))
            s_unreg49.sendall((json.dumps({"version": 1, "type": "history_req", "id": "h-unreg", "conversation_type": "public"}) + "\n").encode('utf-8'))
            unreg_resp = recv_matching(s_unreg49, ["error"], timeout=2.0)
            s_unreg49.close()
            if unreg_resp and unreg_resp.get("code") == "ERR_NOT_REGISTERED":
                t49_details.append("49A (unregistered client rejected with ERR_NOT_REGISTERED) OK")
            else:
                t49_passed = False
                t49_details.append(f"49A failed: {unreg_resp}")

            s_reg49 = BufferedSocket()
            s_reg49.connect((HOST, PORT))
            s_reg49.sendall((json.dumps({"version": 1, "type": "register_req", "username": "HistUser49", "id": "reg-49"}) + "\n").encode('utf-8'))
            recv_matching(s_reg49, ["register_ack"], timeout=2.0)

            # 49B: Invalid conversation_type rejected
            s_reg49.sendall((json.dumps({"version": 1, "type": "history_req", "id": "h-bad-conv", "conversation_type": "galactic_chat"}) + "\n").encode('utf-8'))
            bad_conv_resp = recv_matching(s_reg49, ["error"], timeout=2.0)
            if bad_conv_resp and bad_conv_resp.get("code") in ["ERR_INVALID_CONVERSATION", "ERR_MALFORMED_MESSAGE"]:
                t49_details.append("49B (invalid conversation_type rejected) OK")
            else:
                t49_passed = False
                t49_details.append(f"49B failed: {bad_conv_resp}")

            # 49C: Missing 'with' for direct history rejected
            s_reg49.sendall((json.dumps({"version": 1, "type": "history_req", "id": "h-no-with", "conversation_type": "direct"}) + "\n").encode('utf-8'))
            no_with_resp = recv_matching(s_reg49, ["error"], timeout=2.0)
            if no_with_resp and no_with_resp.get("code") in ["ERR_INVALID_CONVERSATION", "ERR_VALIDATION_FAILED", "ERR_MALFORMED_MESSAGE"]:
                t49_details.append("49C (direct history without 'with' rejected) OK")
            else:
                t49_passed = False
                t49_details.append(f"49C failed: {no_with_resp}")

            # 49D: Missing 'room' for room history rejected
            s_reg49.sendall((json.dumps({"version": 1, "type": "history_req", "id": "h-no-room", "conversation_type": "room"}) + "\n").encode('utf-8'))
            no_room_resp = recv_matching(s_reg49, ["error"], timeout=2.0)
            if no_room_resp and no_room_resp.get("code") in ["ERR_INVALID_CONVERSATION", "ERR_VALIDATION_FAILED", "ERR_MALFORMED_MESSAGE"]:
                t49_details.append("49D (room history without 'room' rejected) OK")
            else:
                t49_passed = False
                t49_details.append(f"49D failed: {no_room_resp}")

            # 49E: Self direct history rejected
            s_reg49.sendall((json.dumps({"version": 1, "type": "history_req", "id": "h-self", "conversation_type": "direct", "with": "HistUser49"}) + "\n").encode('utf-8'))
            self_resp = recv_matching(s_reg49, ["error"], timeout=2.0)
            if self_resp and self_resp.get("code") in ["ERR_CANNOT_MESSAGE_SELF", "ERR_SELF_DIRECT_MESSAGE", "ERR_INVALID_CONVERSATION"]:
                t49_details.append("49E (self direct history rejected) OK")
            else:
                t49_passed = False
                t49_details.append(f"49E failed: {self_resp}")

            # 49F: Oversized limit safely clamped to MAX_HISTORY_LIMIT
            s_reg49.sendall((json.dumps({"version": 1, "type": "history_req", "id": "h-big-lim", "conversation_type": "public", "limit": 9999}) + "\n").encode('utf-8'))
            big_lim_resp = recv_matching(s_reg49, ["history_resp"], timeout=2.0)
            if big_lim_resp and big_lim_resp.get("status") == "ok":
                t49_details.append("49F (oversized limit safely clamped without error) OK")
            else:
                t49_passed = False
                t49_details.append(f"49F failed: {big_lim_resp}")

            s_reg49.close()

            if t49_passed:
                results["Test 49: Parameter validation and authorization bounds"] = "Passed"
                print(f"Test 49 PASSED: {'; '.join(t49_details)}")
            else:
                results["Test 49: Parameter validation and authorization bounds"] = f"Failed ({'; '.join(t49_details)})"
                print(f"Test 49 FAILED: {'; '.join(t49_details)}")
        except Exception as e:
            results["Test 49: Parameter validation and authorization bounds"] = f"Failed ({e})"
            print("Test 49 FAILED with exception:", e)

        # Test 50: Escaped characters and Unicode round-tripping
        print("\n--- Test 50: Escaped characters and Unicode round-tripping ---")
        try:
            t50_passed = True
            t50_details = []

            s_a50 = BufferedSocket()
            s_a50.connect((HOST, PORT))
            s_a50.sendall((json.dumps({"version": 1, "type": "register_req", "username": "HistAlice50", "id": "reg-a50"}) + "\n").encode('utf-8'))
            recv_matching(s_a50, ["register_ack"], timeout=2.0)

            s_b50 = BufferedSocket()
            s_b50.connect((HOST, PORT))
            s_b50.sendall((json.dumps({"version": 1, "type": "register_req", "username": "HistBob50", "id": "reg-b50"}) + "\n").encode('utf-8'))
            recv_matching(s_b50, ["register_ack"], timeout=2.0)

            test_payload = "Line 1\nLine 2\tTabbed\n\"Quotes\" & \\Backslashes\\ \u2764\uFE0F utf8 text"
            s_a50.sendall((json.dumps({"version": 1, "type": "msg_direct_req", "id": "dm-50", "to": "HistBob50", "content": test_payload}) + "\n").encode('utf-8'))
            recv_matching(s_a50, ["msg_direct_ack"], timeout=2.0)
            recv_matching(s_b50, ["msg_direct"], timeout=2.0)

            # Retrieve from SQLite history
            s_a50.sendall((json.dumps({"version": 1, "type": "history_req", "id": "h-50", "conversation_type": "direct", "with": "HistBob50"}) + "\n").encode('utf-8'))
            h50_resp = recv_matching(s_a50, ["history_resp"], timeout=2.0)
            h50_msgs = h50_resp.get("messages", []) if h50_resp else []
            retrieved_content = h50_msgs[-1].get("content") if h50_msgs else ""

            if retrieved_content == test_payload:
                t50_details.append("50 (multiline, escaped quotes, backslashes, and multibyte UTF-8 round-tripped identically) OK")
            else:
                t50_passed = False
                t50_details.append(f"50 failed: len_retrieved={len(retrieved_content)}, len_expected={len(test_payload)}")

            s_a50.close()
            s_b50.close()

            if t50_passed:
                results["Test 50: Escaped characters and Unicode round-tripping"] = "Passed"
                print(f"Test 50 PASSED: {'; '.join(t50_details)}")
            else:
                results["Test 50: Escaped characters and Unicode round-tripping"] = f"Failed ({'; '.join(t50_details)})"
                print(f"Test 50 FAILED: {'; '.join(t50_details)}")
        except Exception as e:
            results["Test 50: Escaped characters and Unicode round-tripping"] = f"Failed ({e})"
            print("Test 50 FAILED with exception:", e)

        # Test 51: History wire limit bounding
        print("\n--- Test 51: History wire limit bounding ---")
        try:
            t51_passed = True
            t51_details = []

            s_a51 = BufferedSocket()
            s_a51.connect((HOST, PORT))
            s_a51.sendall((json.dumps({"version": 1, "type": "register_req", "username": "HistAlice51", "id": "reg-a51"}) + "\n").encode('utf-8'))
            recv_matching(s_a51, ["register_ack"], timeout=2.0)

            s_b51 = BufferedSocket()
            s_b51.connect((HOST, PORT))
            s_b51.sendall((json.dumps({"version": 1, "type": "register_req", "username": "HistBob51", "id": "reg-b51"}) + "\n").encode('utf-8'))
            recv_matching(s_b51, ["register_ack"], timeout=2.0)

            # Send 6 large direct messages (~400 bytes each). Total history payload would be ~2800 bytes.
            for i in range(6):
                large_text = f"LargeMsg51 #{i}: " + ("x" * 380)
                s_a51.sendall((json.dumps({"version": 1, "type": "msg_direct_req", "id": f"dm-51-{i}", "to": "HistBob51", "content": large_text}) + "\n").encode('utf-8'))
                recv_matching(s_a51, ["msg_direct_ack"], timeout=2.0)
                recv_matching(s_b51, ["msg_direct"], timeout=2.0)

            # Request all 6 messages with limit 10
            s_a51.sendall((json.dumps({"version": 1, "type": "history_req", "id": "h-51", "conversation_type": "direct", "with": "HistBob51", "limit": 10}) + "\n").encode('utf-8'))
            raw_h51_line = s_a51.recv_line(timeout=2.0)
            wire_len = len(raw_h51_line.encode('utf-8')) + 1 # including newline

            h51_data = json.loads(raw_h51_line)
            msgs_packed = len(h51_data.get("messages", []))

            if wire_len <= 2048 and msgs_packed < 6 and h51_data.get("has_more") is True:
                t51_details.append(f"51 (history response dynamically packed {msgs_packed} items into {wire_len} bytes <= 2048, has_more=true) OK")
            else:
                t51_passed = False
                t51_details.append(f"51 failed: wire_len={wire_len}, msgs_packed={msgs_packed}, has_more={h51_data.get('has_more')}")

            s_a51.close()
            s_b51.close()

            if t51_passed:
                results["Test 51: Oversized history wire limit bounding"] = "Passed"
                print(f"Test 51 PASSED: {'; '.join(t51_details)}")
            else:
                results["Test 51: Oversized history wire limit bounding"] = f"Failed ({'; '.join(t51_details)})"
                print(f"Test 51 FAILED: {'; '.join(t51_details)}")
        except Exception as e:
            results["Test 51: Oversized history wire limit bounding"] = f"Failed ({e})"
            print("Test 51 FAILED with exception:", e)

        # Test 52: Room name reuse historical isolation
        print("\n--- Test 52: Room name reuse historical isolation ---")
        try:
            t52_passed = True
            t52_details = []

            s_a52 = BufferedSocket()
            s_a52.connect((HOST, PORT))
            s_a52.sendall((json.dumps({"version": 1, "type": "register_req", "username": "HistAlice52", "id": "reg-a52"}) + "\n").encode('utf-8'))
            recv_matching(s_a52, ["register_ack"], timeout=2.0)

            s_b52 = BufferedSocket()
            s_b52.connect((HOST, PORT))
            s_b52.sendall((json.dumps({"version": 1, "type": "register_req", "username": "HistBob52", "id": "reg-b52"}) + "\n").encode('utf-8'))
            recv_matching(s_b52, ["register_ack"], timeout=2.0)

            # Incarnation 1: Alice creates #recycle52 and sends secret
            s_a52.sendall((json.dumps({"version": 1, "type": "room_create_req", "id": "c-52-1", "room": "#recycle52"}) + "\n").encode('utf-8'))
            recv_matching(s_a52, ["room_create_resp"], timeout=2.0)

            s_a52.sendall((json.dumps({"version": 1, "type": "room_msg_req", "id": "rm-52-1", "room": "#recycle52", "content": "Incarnation1Secret"}) + "\n").encode('utf-8'))
            recv_matching(s_a52, ["room_msg_ack"], timeout=2.0)

            # Alice leaves room -> room becomes empty and is auto-deleted
            s_a52.sendall((json.dumps({"version": 1, "type": "room_leave_req", "id": "lv-52-1", "room": "#recycle52"}) + "\n").encode('utf-8'))
            recv_matching(s_a52, ["room_leave_resp"], timeout=2.0)

            # Incarnation 2: Bob creates room #recycle52 (same room name, new instance ID!)
            s_b52.sendall((json.dumps({"version": 1, "type": "room_create_req", "id": "c-52-2", "room": "#recycle52"}) + "\n").encode('utf-8'))
            recv_matching(s_b52, ["room_create_resp"], timeout=2.0)

            # Bob queries room history for #recycle52
            s_b52.sendall((json.dumps({"version": 1, "type": "history_req", "id": "h-52", "conversation_type": "room", "room": "#recycle52"}) + "\n").encode('utf-8'))
            h52_resp = recv_matching(s_b52, ["history_resp"], timeout=2.0)
            h52_msgs = h52_resp.get("messages", []) if h52_resp else None

            if h52_msgs == []:
                t52_details.append("52 (re-created room name does not expose messages from previous room incarnation) OK")
            else:
                t52_passed = False
                t52_details.append(f"52 failed: messages={h52_msgs}")

            s_a52.close()
            s_b52.close()

            if t52_passed:
                results["Test 52: Room name reuse historical isolation"] = "Passed"
                print(f"Test 52 PASSED: {'; '.join(t52_details)}")
            else:
                results["Test 52: Room name reuse historical isolation"] = f"Failed ({'; '.join(t52_details)})"
                print(f"Test 52 FAILED: {'; '.join(t52_details)}")
        except Exception as e:
            results["Test 52: Room name reuse historical isolation"] = f"Failed ({e})"
            print("Test 52 FAILED with exception:", e)

        # Test 53: Simulated storage failure prevents false ACK and false broadcast
        print("\n--- Test 53: Storage failure handling and zero false ACK/broadcast ---")
        fail_server = None
        fail_db = os.path.abspath(f"test_fail_{os.getpid()}.db")
        try:
            t53_passed = True
            t53_details = []

            # Launch failure test server on port 8082 with CHAT_SIMULATE_STORAGE_FAILURE=1
            fail_server = start_server(port=8082, db_path=fail_db, env={"CHAT_SIMULATE_STORAGE_FAILURE": "1"})

            s_u1 = BufferedSocket()
            s_u1.connect((HOST, 8082))
            s_u1.sendall((json.dumps({"version": 1, "type": "register_req", "username": "FailUser1", "id": "reg-f1"}) + "\n").encode('utf-8'))
            recv_matching(s_u1, ["register_ack"], timeout=2.0)

            s_u2 = BufferedSocket()
            s_u2.connect((HOST, 8082))
            s_u2.sendall((json.dumps({"version": 1, "type": "register_req", "username": "FailUser2", "id": "reg-f2"}) + "\n").encode('utf-8'))
            recv_matching(s_u2, ["register_ack"], timeout=2.0)

            # Public message failure
            s_u1.sendall((json.dumps({"version": 1, "type": "msg_send", "id": "pub-fail", "content": "Should not be persisted"}) + "\n").encode('utf-8'))
            f_pub_err = recv_matching(s_u1, ["error"], timeout=2.0)
            u2_got = s_u2.recv_line(timeout=0.4)
            if f_pub_err and f_pub_err.get("code") == "ERR_STORAGE_ERROR" and (u2_got == "" or "msg_broadcast" not in u2_got):
                t53_details.append("53A (public message storage failure returns ERR_STORAGE_ERROR; zero broadcast) OK")
            else:
                t53_passed = False
                t53_details.append(f"53A failed: err={f_pub_err}, u2_got='{u2_got}'")

            # Direct message failure
            s_u1.sendall((json.dumps({"version": 1, "type": "msg_direct_req", "id": "dm-fail", "to": "FailUser2", "content": "Secret fail"}) + "\n").encode('utf-8'))
            f_dm_err = recv_matching(s_u1, ["error"], timeout=2.0)
            u2_got_dm = s_u2.recv_line(timeout=0.4)
            if f_dm_err and f_dm_err.get("code") == "ERR_STORAGE_ERROR" and (u2_got_dm == "" or "msg_direct" not in u2_got_dm):
                t53_details.append("53B (direct message storage failure returns ERR_STORAGE_ERROR; zero delivery) OK")
            else:
                t53_passed = False
                t53_details.append(f"53B failed: err={f_dm_err}, u2_got='{u2_got_dm}'")

            # Room message failure
            s_u1.sendall((json.dumps({"version": 1, "type": "room_create_req", "id": "c-f", "room": "#failroom"}) + "\n").encode('utf-8'))
            recv_matching(s_u1, ["room_create_resp"], timeout=2.0)
            s_u2.sendall((json.dumps({"version": 1, "type": "room_join_req", "id": "j-f", "room": "#failroom"}) + "\n").encode('utf-8'))
            recv_matching(s_u2, ["room_join_resp"], timeout=2.0)

            s_u1.sendall((json.dumps({"version": 1, "type": "room_msg_req", "id": "rm-fail", "room": "#failroom", "content": "Room fail"}) + "\n").encode('utf-8'))
            f_rm_err = recv_matching(s_u1, ["error"], timeout=2.0)
            u2_got_rm = s_u2.recv_line(timeout=0.4)
            if f_rm_err and f_rm_err.get("code") == "ERR_STORAGE_ERROR" and (u2_got_rm == "" or "room_msg" not in u2_got_rm):
                t53_details.append("53C (room message storage failure returns ERR_STORAGE_ERROR; zero room broadcast) OK")
            else:
                t53_passed = False
                t53_details.append(f"53C failed: err={f_rm_err}, u2_got='{u2_got_rm}'")

            s_u1.close()
            s_u2.close()

            if t53_passed:
                results["Test 53: Storage failure handling and zero false ACK/broadcast"] = "Passed"
                print(f"Test 53 PASSED: {'; '.join(t53_details)}")
            else:
                results["Test 53: Storage failure handling and zero false ACK/broadcast"] = f"Failed ({'; '.join(t53_details)})"
                print(f"Test 53 FAILED: {'; '.join(t53_details)}")
        except Exception as e:
            results["Test 53: Storage failure handling and zero false ACK/broadcast"] = f"Failed ({e})"
            print("Test 53 FAILED with exception:", e)
        finally:
            if fail_server:
                stop_server(fail_server)
            cleanup_db(fail_db)

        # Test 54: Concurrent public, direct, and room message storage integrity
        print("\n--- Test 54: Concurrent public, direct, and room message storage integrity ---")
        try:
            con_errors = []
            num_threads = 6

            def history_worker(tid):
                try:
                    s = BufferedSocket()
                    s.connect((HOST, PORT))
                    uname = f"ConHist54_{tid}"
                    s.sendall((json.dumps({"version": 1, "type": "register_req", "username": uname, "id": f"reg54-{tid}"}) + "\n").encode('utf-8'))
                    recv_matching(s, ["register_ack"], timeout=3.0)

                    # Public message
                    s.sendall((json.dumps({"version": 1, "type": "msg_send", "id": f"pub54-{tid}", "content": f"ConPub from {tid}"}) + "\n").encode('utf-8'))
                    ack_pub = recv_matching(s, ["msg_ack"], timeout=3.0)
                    if not ack_pub:
                        con_errors.append(f"Thread {tid} pub ack missing")

                    # Direct message to neighbor
                    target = f"ConHist54_{(tid + 1) % num_threads}"
                    s.sendall((json.dumps({"version": 1, "type": "msg_direct_req", "id": f"dm54-{tid}", "to": target, "content": f"ConDM from {tid}"}) + "\n").encode('utf-8'))
                    ack_dm = recv_matching(s, ["msg_direct_ack", "error"], timeout=3.0)
                    if not ack_dm:
                        con_errors.append(f"Thread {tid} dm ack missing")

                    # Room message
                    rname = "#con54_room"
                    if tid == 0:
                        s.sendall((json.dumps({"version": 1, "type": "room_create_req", "id": "c-con54", "room": rname}) + "\n").encode('utf-8'))
                        recv_matching(s, ["room_create_resp", "error"], timeout=3.0)
                    else:
                        time.sleep(0.05)
                        s.sendall((json.dumps({"version": 1, "type": "room_join_req", "id": f"j-con54-{tid}", "room": rname}) + "\n").encode('utf-8'))
                        recv_matching(s, ["room_join_resp", "error"], timeout=3.0)

                    s.sendall((json.dumps({"version": 1, "type": "room_msg_req", "id": f"rm54-{tid}", "room": rname, "content": f"ConRoom from {tid}"}) + "\n").encode('utf-8'))
                    recv_matching(s, ["room_msg_ack", "error"], timeout=3.0)

                    # History query
                    s.sendall((json.dumps({"version": 1, "type": "history_req", "id": f"h-con54-{tid}", "conversation_type": "public", "limit": 5}) + "\n").encode('utf-8'))
                    h_resp = recv_matching(s, ["history_resp"], timeout=3.0)
                    if not h_resp or h_resp.get("status") != "ok":
                        con_errors.append(f"Thread {tid} history query failed: {h_resp}")

                    s.close()
                except Exception as ex:
                    con_errors.append(f"Thread {tid} exception: {ex}")

            th_list = []
            for i in range(num_threads):
                t = threading.Thread(target=history_worker, args=(i,))
                th_list.append(t)
                t.start()

            for t in th_list:
                t.join(timeout=8.0)

            if len(con_errors) == 0:
                results["Test 54: Concurrent public, direct, and room message storage integrity"] = "Passed"
                print(f"Test 54 PASSED: All {num_threads} concurrent storage threads succeeded without errors.")
            else:
                results["Test 54: Concurrent public, direct, and room message storage integrity"] = f"Failed ({con_errors})"
                print(f"Test 54 FAILED: {con_errors}")
        except Exception as e:
            results["Test 54: Concurrent public, direct, and room message storage integrity"] = f"Failed ({e})"
            print("Test 54 FAILED with exception:", e)

        # Test 55: Disconnect resilience (delivery failure after DB commit does not erase message)
        print("\n--- Test 55: Disconnect resilience ---")
        try:
            t55_passed = True
            t55_details = []

            s_a55 = BufferedSocket()
            s_a55.connect((HOST, PORT))
            s_a55.sendall((json.dumps({"version": 1, "type": "register_req", "username": "HistAlice55", "id": "reg-a55"}) + "\n").encode('utf-8'))
            recv_matching(s_a55, ["register_ack"], timeout=2.0)

            s_b55 = BufferedSocket()
            s_b55.connect((HOST, PORT))
            s_b55.sendall((json.dumps({"version": 1, "type": "register_req", "username": "HistBob55", "id": "reg-b55"}) + "\n").encode('utf-8'))
            recv_matching(s_b55, ["register_ack"], timeout=2.0)

            # Alice sends DM to Bob while Bob is connected
            s_a55.sendall((json.dumps({"version": 1, "type": "msg_direct_req", "id": "dm-55", "to": "HistBob55", "content": "Persisted Despite Broken Recipient Socket"}) + "\n").encode('utf-8'))
            ack_55 = recv_matching(s_a55, ["msg_direct_ack"], timeout=2.0)
            recv_matching(s_b55, ["msg_direct"], timeout=2.0)

            # Bob aborts socket connection abruptly
            s_b55.set_linger_abort()
            s_b55.close()
            time.sleep(0.05)

            # Verify Alice can retrieve the persisted message from history even after Bob disconnected
            s_a55.sendall((json.dumps({"version": 1, "type": "history_req", "id": "h-55", "conversation_type": "direct", "with": "HistBob55"}) + "\n").encode('utf-8'))
            h55_resp = recv_matching(s_a55, ["history_resp"], timeout=2.0)
            h55_msgs = [m.get("content") for m in h55_resp.get("messages", [])] if h55_resp else []

            if ack_55 and ack_55.get("status") == "ok" and "Persisted Despite Broken Recipient Socket" in h55_msgs:
                t55_details.append("55 (message committed to SQLite survives recipient socket delivery abort) OK")
            else:
                t55_passed = False
                t55_details.append(f"55 failed: ack={ack_55}, msgs={h55_msgs}")

            s_a55.close()

            if t55_passed:
                results["Test 55: Disconnect resilience (delivery failure after DB commit does not erase message)"] = "Passed"
                print(f"Test 55 PASSED: {'; '.join(t55_details)}")
            else:
                results["Test 55: Disconnect resilience (delivery failure after DB commit does not erase message)"] = f"Failed ({'; '.join(t55_details)})"
                print(f"Test 55 FAILED: {'; '.join(t55_details)}")
        except Exception as e:
            results["Test 55: Disconnect resilience (delivery failure after DB commit does not erase message)"] = f"Failed ({e})"
            print("Test 55 FAILED with exception:", e)

    finally:
        stop_server(srv_struct)
        cleanup_db(TEST_DB)

    # Test 56: Interactive console client commands and advanced features
    print("\n--- Test 56: Interactive console client commands ---")
    client_server = None
    client_processes = []
    client_db = os.path.abspath(f"test_client_commands_{os.getpid()}.db")
    try:
        client_port = find_free_port()
        cleanup_db(client_db)
        client_server = start_server(client_port, client_db)

        def launch_client(name):
            process = subprocess.Popen(
                [CLIENT_EXE],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1
            )
            process.stdin.write(f"127.0.0.1\n{client_port}\n{name}\n")
            process.stdin.flush()
            reader = ProcessLineReader(process)
            client_processes.append(process)
            return process, reader

        def send_line(process, text):
            process.stdin.write(text + "\n")
            process.stdin.flush()

        alice_proc, alice_reader = launch_client("CliAlice")
        _, alice_collected = alice_reader.read_until("Registration successful! Welcome, CliAlice.", timeout=5.0)

        bob_proc, bob_reader = launch_client("CliBob")
        _, bob_collected = bob_reader.read_until("Registration successful! Welcome, CliBob.", timeout=5.0)
        alice_reader.read_until("CliBob has joined the chat.", timeout=3.0)

        failures = []
        passed = []

        # Public chat rendered by both the sender and the peer
        send_line(alice_proc, "hello bob")
        alice_echo, _ = alice_reader.read_until("[You] hello bob", timeout=3.0)
        bob_received, _ = bob_reader.read_until("[CliAlice] hello bob", timeout=3.0)
        if alice_echo and bob_received:
            passed.append("public chat sender echo + peer broadcast")
        else:
            failures.append(f"public chat (echo={alice_echo!r}, peer={bob_received!r})")

        # Online user list
        send_line(alice_proc, "/users")
        users_header, _ = alice_reader.read_until("Online users", timeout=3.0)
        users_bob, _ = alice_reader.read_until("CliBob", timeout=3.0)
        if users_header and users_bob:
            passed.append("online user list")
        else:
            failures.append(f"/users (header={users_header!r}, bob={users_bob!r})")

        # Direct messaging and direct history
        send_line(alice_proc, "/msg CliBob private hello")
        dm_echo, _ = alice_reader.read_until("[DM to CliBob] private hello", timeout=3.0)
        dm_received, _ = bob_reader.read_until("[DM from CliAlice] private hello", timeout=3.0)
        if dm_echo and dm_received:
            passed.append("private message delivery")
        else:
            failures.append(f"private message (echo={dm_echo!r}, received={dm_received!r})")

        send_line(alice_proc, "/history dm CliBob")
        dm_history_header, _ = alice_reader.read_until("History: direct with CliBob", timeout=3.0)
        dm_history_item, _ = alice_reader.read_until("private hello", timeout=3.0)
        if dm_history_header and dm_history_item:
            passed.append("direct-message history")
        else:
            failures.append(f"/history dm (header={dm_history_header!r}, item={dm_history_item!r})")

        # Room creation, join, message, member list, and room history
        send_line(alice_proc, "/create lobby56")
        room_created, _ = alice_reader.read_until("Created room 'lobby56'", timeout=3.0)
        send_line(bob_proc, "/join lobby56")
        room_joined, _ = bob_reader.read_until("Joined 'lobby56'", timeout=3.0)
        alice_reader.read_until("CliBob has joined lobby56", timeout=3.0)
        send_line(alice_proc, "/room lobby56 room hello")
        room_echo, _ = alice_reader.read_until("[You @lobby56] room hello", timeout=3.0)
        room_received, _ = bob_reader.read_until("[lobby56] CliAlice: room hello", timeout=3.0)
        if room_created and room_joined and room_echo and room_received:
            passed.append("room create/join/message")
        else:
            failures.append(
                f"room flow (created={room_created!r}, joined={room_joined!r}, "
                f"echo={room_echo!r}, received={room_received!r})"
            )

        send_line(alice_proc, "/members lobby56")
        members_header, _ = alice_reader.read_until("Members of 'lobby56'", timeout=3.0)
        members_bob, _ = alice_reader.read_until("CliBob", timeout=3.0)
        if members_header and members_bob:
            passed.append("room member list")
        else:
            failures.append(f"/members (header={members_header!r}, bob={members_bob!r})")

        send_line(bob_proc, "/history room lobby56")
        room_history_header, _ = bob_reader.read_until("History: room 'lobby56'", timeout=3.0)
        room_history_item, _ = bob_reader.read_until("room hello", timeout=3.0)
        if room_history_header and room_history_item:
            passed.append("room history")
        else:
            failures.append(f"/history room (header={room_history_header!r}, item={room_history_item!r})")

        send_line(alice_proc, "/history public")
        public_history_header, _ = alice_reader.read_until("History: public", timeout=3.0)
        public_history_item, _ = alice_reader.read_until("hello bob", timeout=3.0)
        if public_history_header and public_history_item:
            passed.append("public history")
        else:
            failures.append(f"/history public (header={public_history_header!r}, item={public_history_item!r})")

        send_line(alice_proc, "/help")
        help_line, _ = alice_reader.read_until("Available commands", timeout=3.0)
        if help_line:
            passed.append("/help")
        else:
            failures.append("/help")

        # Duplicate username rejection followed by a successful retry
        duplicate_proc, duplicate_reader = launch_client("CliAlice")
        duplicate_error, _ = duplicate_reader.read_until("already taken", timeout=5.0)
        send_line(duplicate_proc, "CliDuplicate")
        duplicate_retry, _ = duplicate_reader.read_until(
            "Registration successful! Welcome, CliDuplicate.", timeout=5.0)
        send_line(duplicate_proc, "/quit")
        try:
            duplicate_proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            duplicate_proc.kill()
        if duplicate_error and duplicate_retry:
            passed.append("duplicate username retry")
        else:
            failures.append(
                f"duplicate username retry (error={duplicate_error!r}, retry={duplicate_retry!r})"
            )

        # Graceful client exit
        send_line(alice_proc, "/quit")
        disconnect_line, _ = alice_reader.read_until("Disconnected from server.", timeout=4.0)
        try:
            alice_proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            alice_proc.kill()
        send_line(bob_proc, "/quit")
        try:
            bob_proc.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            bob_proc.kill()

        if disconnect_line and alice_proc.returncode == 0 and bob_proc.returncode == 0:
            passed.append("graceful /quit")
        else:
            failures.append(
                f"graceful /quit (line={disconnect_line!r}, "
                f"alice_rc={alice_proc.returncode}, bob_rc={bob_proc.returncode})"
            )

        if not failures:
            results["Test 56: Interactive client commands and advanced features"] = "Passed"
            print(f"Test 56 PASSED: {'; '.join(passed)}")
        else:
            results["Test 56: Interactive client commands and advanced features"] = f"Failed ({'; '.join(failures)})"
            print(f"Test 56 FAILED: {'; '.join(failures)}")
    except Exception as e:
        results["Test 56: Interactive client commands and advanced features"] = f"Failed ({e})"
        print("Test 56 FAILED with exception:", e)
    finally:
        for process in client_processes:
            if process.poll() is None:
                try:
                    process.terminate()
                    process.wait(timeout=2.0)
                except Exception:
                    try:
                        process.kill()
                    except Exception:
                        pass
        if client_server is not None:
            stop_server(client_server)
        cleanup_db(client_db)

    print("\n================ FINAL TEST SUMMARY ================")
    all_passed = True
    for test_name, status in results.items():
        print(f"[{status}] {test_name}")
        if status != "Passed":
            all_passed = False

    if all_passed:
        print(f"\nALL {len(results)} TESTS PASSED SUCCESSFULLY!")
        return 0
    else:
        print("\nSOME TESTS FAILED.")
        return 1

if __name__ == "__main__":
    sys.exit(run_all_tests())
