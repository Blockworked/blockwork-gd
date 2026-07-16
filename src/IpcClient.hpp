#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace macros {

// Persistent background TCP client to the Macros app's loopback control
// interface (127.0.0.1:<port>, newline-delimited JSON). Reconnects with a
// fixed backoff if the Macros app isn't running yet or drops the connection.
// `sendCommand` is safe to call from the game thread: it only queues, the
// background thread owns all socket I/O.
class IpcClient {
public:
    static IpcClient& get();

    void start();
    void sendCommand(std::string json);

private:
    IpcClient() = default;
    void threadLoop();

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::string> m_queue;
    std::thread m_thread;
    bool m_started = false;
};

}
