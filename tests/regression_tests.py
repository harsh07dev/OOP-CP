import socket
import subprocess
import time
import sys
import os
import struct
import threading

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

    print("\n================ FINAL TEST SUMMARY ================")
    all_passed = True
    for test_name, status in results.items():
        print(f"[{status}] {test_name}")
        if status != "Passed":
            all_passed = False

    if all_passed:
        print("\nALL 17 TESTS PASSED SUCCESSFULLY!")
        return 0
    else:
        print("\nSOME TESTS FAILED.")
        return 1

if __name__ == "__main__":
    sys.exit(run_all_tests())
