#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace dxxviewer {

// Minimal WebSocket client for hsbWebSocketHub (ws://host:port/ws or /), used
// only to subscribe to one topic and receive its broadcast text. Not a
// general-purpose WebSocket implementation: no TLS, client role only, and it
// handles exactly what the hub sends - the {command,ok,...} control replies
// (subscribe ack) plus unwrapped topic broadcasts - along with ping/close.
// See hsbWebSocketHub/USER_GUIDE_EN.md for the envelope protocol.
//
// Runs its own background thread (connect, reconnect-with-retry, receive
// loop) and delivers each message/status change to the FLTK main thread via
// Fl::awake - callers must have called Fl::lock() once at startup.
class HubClient {
public:
    // onMapText fires with the raw DXX text every time a message arrives on
    // `topic`. onConnectionChanged fires with true right after the
    // subscribe handshake succeeds and with false when that connection is
    // lost (before each reconnect attempt) - so a caller can reflect live
    // hub status (e.g. in a window title) without polling.
    HubClient(std::string host, unsigned short port, std::string topic,
               std::function<void(std::string)> onMapText,
               std::function<void(bool)> onConnectionChanged);
    ~HubClient();

    HubClient(const HubClient&) = delete;
    HubClient& operator=(const HubClient&) = delete;

private:
    void run();

    std::string m_host;
    unsigned short m_port;
    std::string m_topic;
    std::function<void(std::string)> m_onMapText;
    std::function<void(bool)> m_onConnectionChanged;
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
};

} // namespace dxxviewer
