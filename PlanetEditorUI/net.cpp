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

    // 必须先设非阻塞。游戏没开时 connect() 在部分机器上要等 2 秒甚至更久才超时，
    // 阻塞版本会把 ImGui 主线程整个冻住（表现为「没连接时特别卡」）。
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS && err != WSAEALREADY) {
            last_error_ = "connect failed (game not running?)";
            closesocket(s);
            return false;
        }
        // 本机回环正常时 1ms 内就能连上；60ms 足够，绝不让界面等
        fd_set wfd, efd;
        FD_ZERO(&wfd); FD_ZERO(&efd);
        FD_SET(s, &wfd); FD_SET(s, &efd);
        timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 60000;
        int n = select(0, nullptr, &wfd, &efd, &tv);
        if (n <= 0 || FD_ISSET(s, &efd)) {
            last_error_ = "connect timeout (game not running?)";
            closesocket(s);
            return false;
        }
        int soerr = 0;
        int len = sizeof(soerr);
        if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soerr, &len) != 0 || soerr != 0) {
            last_error_ = "connect refused (game not running?)";
            closesocket(s);
            return false;
        }
    }

    // 保持非阻塞，Poll() / Send() 都按非阻塞处理
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
    const char* p = out.c_str();
    int left = (int)out.size();
    int tries = 0;
    while (left > 0 && tries++ < 32) {
        int sent = send((SOCKET)sock_, p, left, 0);
        if (sent > 0) { p += sent; left -= sent; continue; }
        if (sent == 0) { Close(); return; }
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
            // 发送缓冲暂时满了，等可写，别误判成掉线
            fd_set wfd;
            FD_ZERO(&wfd); FD_SET((SOCKET)sock_, &wfd);
            timeval tv; tv.tv_sec = 0; tv.tv_usec = 50000;
            if (select(0, nullptr, &wfd, nullptr, &tv) > 0) continue;
        }
        Close();
        return;
    }
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
