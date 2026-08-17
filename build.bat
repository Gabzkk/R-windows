@echo off
rem ===================================================================
rem  Kestrel-7 Agent Build Script (MSVC x64 Native Tools Command Prompt)
rem ===================================================================

if not exist bin mkdir bin

echo [*] Compiling Kestrel-7 Enterprise Agent (Release x64)...
cl.exe /EHsc /MT /O2 /GS- /DNDEBUG /D_WIN32_WINNT=0x0A00 ^
    src\main.cpp src\crypto.cpp src\network.cpp src\evasion.cpp src\persistence.cpp src\utils.cpp ^
    /Fe:bin\kestrel7_agent.exe ^
    /link ws2_32.lib winhttp.lib advapi32.lib bcrypt.lib psapi.lib user32.lib /SUBSYSTEM:WINDOWS

if %ERRORLEVEL% EQU 0 (
    echo [+] Compilation succeeded: bin\kestrel7_agent.exe
) else (
    echo [-] Build failed. Please verify MSVC environment.
)
