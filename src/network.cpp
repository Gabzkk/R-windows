// network.cpp - WinHTTP Encrypted HTTPS Transports & Raw TCP Failover
#include <windows.h>
#include <winhttp.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")

extern "C" {

BOOL HttpPostData(const wchar_t* host, int port, const wchar_t* endpoint, 
                  const unsigned char* inData, DWORD inLen, 
                  unsigned char* outData, DWORD* outLen) {
    HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) Kestrel7/2.0", 
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 
                                     WINHTTP_NO_PROXY_NAME, 
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return FALSE;
    
    // Set 10s timeouts
    DWORD timeout = 10000;
    WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    
    HINTERNET hConnect = WinHttpConnect(hSession, host, (INTERNET_PORT)port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return FALSE;
    }
    
    DWORD reqFlags = (port == 443) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", endpoint, 
                                           NULL, WINHTTP_NO_REFERER, 
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, reqFlags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return FALSE;
    }
    
    // Allow self-signed certificates in staging environments
    if (reqFlags & WINHTTP_FLAG_SECURE) {
        DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | 
                         SECURITY_FLAG_IGNORE_CERT_CN_INVALID | 
                         SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | 
                         SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    }
    
    LPCWSTR headers = L"Content-Type: application/octet-stream\r\nX-Agent: Kestrel-7\r\n";
    BOOL sent = WinHttpSendRequest(hRequest, headers, (DWORD)wcslen(headers), 
                                   (LPVOID)inData, inLen, inLen, 0);
    if (!sent) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return FALSE;
    }
    
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return FALSE;
    }
    
    DWORD bytesRead = 0;
    DWORD totalRead = 0;
    DWORD maxOut = outLen ? *outLen : 0;
    
    while (outData && maxOut > 0 && WinHttpReadData(hRequest, outData + totalRead, maxOut - totalRead - 1, &bytesRead) && bytesRead > 0) {
        totalRead += bytesRead;
        if (totalRead >= maxOut - 1) break;
    }
    
    if (outData && maxOut > 0) {
        outData[totalRead] = 0;
        if (outLen) *outLen = totalRead;
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return TRUE;
}

BOOL SocketPostData(const char* host, int port, 
                    const unsigned char* inData, DWORD inLen, 
                    unsigned char* outData, DWORD* outLen) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return FALSE;
    
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return FALSE;
    }
    
    struct sockaddr_in srv;
    srv.sin_family = AF_INET;
    srv.sin_port = htons((u_short)port);
    inet_pton(AF_INET, host, &srv.sin_addr);
    
    // Set 5s socket timeout
    DWORD timeout = 5000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    
    if (connect(sock, (struct sockaddr*)&srv, sizeof(srv)) != 0) {
        closesocket(sock);
        WSACleanup();
        return FALSE;
    }
    
    send(sock, (const char*)inData, (int)inLen, 0);
    
    if (outData && outLen && *outLen > 0) {
        int r = recv(sock, (char*)outData, (int)(*outLen - 1), 0);
        if (r > 0) {
            outData[r] = 0;
            *outLen = (DWORD)r;
        } else {
            *outLen = 0;
        }
    }
    
    closesocket(sock);
    WSACleanup();
    return TRUE;
}

}
