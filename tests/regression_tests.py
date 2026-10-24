import socket
import subprocess
import time
import sys
import os
import struct
import threading
import json

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

def start_server():
    global SERVER_EXE
    p = subprocess.Popen([SERVER_EXE], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
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
            "server/src/main.cpp", "server/src/server.cpp", "server/src/client_manager.cpp", "server/src/logger.cpp"
        ]
        if sys.platform == "win32":
            server_cmd += ["-lws2_32", "-o", "chat_server.exe"]
        else:
            server_cmd += ["-pthread", "-o", "chat_server"]

        client_cmd = [
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Icommon/include", "-Iclient/include",
            "client/src/main.cpp", "client/src/client.cpp", "client/src/sender.cpp", "client/src/receiver.cpp"
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
    finally:
        stop_server(srv_struct)

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
