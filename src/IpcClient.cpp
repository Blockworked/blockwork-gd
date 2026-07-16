#include "IpcClient.hpp"

#include <Geode/Geode.hpp>

// Geode's precompiled header already drags in <windows.h> -> the legacy
// <winsock.h> ahead of anything in this file, so explicitly including
// <winsock2.h>/<ws2tcpip.h> here only collides with types it already
// declared. The classic WinSock 1.1 API in winsock.h covers everything this
// client needs (socket/connect/send/closesocket/WSAStartup); the one
// WinSock2-only call avoided is inet_pton, replaced with inet_addr below.
#ifdef GEODE_IS_WINDOWS
#pragma comment(lib, "ws2_32.lib")
#endif

using namespace geode::prelude;

namespace macros {

IpcClient& IpcClient::get() {
    static IpcClient instance;
    return instance;
}

void IpcClient::start() {
    if (m_started) {
        return;
    }
    m_started = true;
    m_thread = std::thread([this] { threadLoop(); });
    m_thread.detach();
}

void IpcClient::sendCommand(std::string json) {
    {
        std::lock_guard lock(m_mutex);
        m_queue.push_back(std::move(json));
    }
    m_cv.notify_one();
}

#ifdef GEODE_IS_WINDOWS

void IpcClient::threadLoop() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        log::error("macros: WSAStartup failed");
        return;
    }

    while (true) {
        auto port = static_cast<uint16_t>(Mod::get()->getSettingValue<int64_t>("port"));

        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            closesocket(sock);
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        log::debug("macros: connected to 127.0.0.1:{}", port);

        bool connectionAlive = true;
        while (connectionAlive) {
            std::string cmd;
            {
                std::unique_lock lock(m_mutex);
                m_cv.wait(lock, [this] { return !m_queue.empty(); });
                cmd = std::move(m_queue.front());
                m_queue.pop_front();
            }

            std::string line = cmd + "\n";
            int sent = send(sock, line.c_str(), static_cast<int>(line.size()), 0);
            if (sent == SOCKET_ERROR) {
                log::debug("macros: send failed, will reconnect");
                connectionAlive = false;
            } else {
                log::debug("macros: sent {}", cmd);
            }
        }

        closesocket(sock);
    }
}

#else

void IpcClient::threadLoop() {
    log::warn("macros: IPC client is only implemented on Windows");
}

#endif

}
