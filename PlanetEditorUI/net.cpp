#include "net.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

static bool g_wsa = false;

bool BridgeClient::Connect(int port) {
    Close();
    if (!g_wsa) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            last_error_ = "WSAStartup failed";
            return false;
        }
        g_wsa = true;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        last_error_ = "socket() failed";
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
        last_error_ = "connect failed (game not running?)";
        closesocket(s);
        return false;
    }

    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    sock_ = (unsigned long long)s;
    buf_.clear();
    last_error_.clear();
    return true;
}

void BridgeClient::Close() {
    if (sock_ != (unsigned long long)-1) {
        closesocket((SOCKET)sock_);
        sock_ = (unsigned long long)-1;
    }
    buf_.clear();
}

void BridgeClient::Send(const std::string& line) {
    if (!Connected()) return;
    std::string out = line + "\n";
    int sent = send((SOCKET)sock_, out.c_str(), (int)out.size(), 0);
    if (sent <= 0) Close();
}

std::vector<std::string> BridgeClient::Poll() {
    std::vector<std::string> lines;
    if (!Connected()) return lines;

    char tmp[4096];
    for (;;) {
        int n = recv((SOCKET)sock_, tmp, sizeof(tmp), 0);
        if (n > 0) {
            buf_.append(tmp, (size_t)n);
            continue;
        }
        if (n == 0) { Close(); break; }
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) { Close(); }
        break;
    }

    size_t pos;
    while ((pos = buf_.find('\n')) != std::string::npos) {
        std::string line = buf_.substr(0, pos);
        buf_.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}
