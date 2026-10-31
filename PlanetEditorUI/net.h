// 与游戏内 PlanetEditor 插件通信的最小 TCP 客户端（127.0.0.1:41729）
// 协议：一行一条 UTF-8 文本命令；响应以 \n 结尾，多行响应由调用方按前缀解析。
#pragma once

#include <string>
#include <vector>

class BridgeClient {
public:
    bool Connect(int port = 41729);
    void Close();
    bool Connected() const { return sock_ != (unsigned long long)-1; }

    void Send(const std::string& line);
    // 取走已经收到的完整行（非阻塞）
    std::vector<std::string> Poll();

    const std::string& LastError() const { return last_error_; }

private:
    unsigned long long sock_ = (unsigned long long)-1;
    std::string buf_;
    std::string last_error_;
};
