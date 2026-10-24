
import json
import socket
import threading
import sys

HOST = "127.0.0.1"
PORT = 8080

if len(sys.argv) < 2:
    print("Usage: python structured_test_client.py <username>")
    sys.exit(1)

username = sys.argv[1]
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect((HOST, PORT))
sock.settimeout(None)

def receive_messages():
    buffer = b""
    try:
        while True:
            data = sock.recv(4096)
            if not data:
                print("\n[Disconnected from server]")
                break

            buffer += data

            while b"\n" in buffer:
                line, buffer = buffer.split(b"\n", 1)
                if line.strip():
                    try:
                        print("\n[RECEIVED]", json.dumps(
                            json.loads(line.decode("utf-8")),
                            indent=2
                        ))
                    except (json.JSONDecodeError, UnicodeDecodeError):
                        print("\n[RECEIVED RAW]", line.decode(
                            "utf-8", errors="replace"
                        ))
                    print("> ", end="", flush=True)
    except OSError as exc:
        print(f"\n[Receiver stopped] {exc}")

threading.Thread(target=receive_messages, daemon=True).start()

def send_message(message):
    payload = json.dumps(message, separators=(",", ":")) + "\n"
    sock.sendall(payload.encode("utf-8"))
    print("[SENT]", payload.strip())

try:
    send_message({
        "version": 1,
        "type": "register_req",
        "id": f"reg-{username}",
        "username": username
    })

    print(f"\nConnected as {username}.")
    print("Enter one JSON request per line.")
    print("Type exit to close this test client.")

    while True:
        raw = input("> ").strip()

        if raw.lower() == "exit":
            break

        if not raw:
            continue

        try:
            message = json.loads(raw)
            if not isinstance(message, dict):
                print("Enter a JSON object.")
                continue
            send_message(message)
        except json.JSONDecodeError as exc:
            print("Invalid JSON:", exc)

except (OSError, KeyboardInterrupt) as exc:
    print(f"\nClient stopped: {exc}")
finally:
    sock.close()
