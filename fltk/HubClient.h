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
    // onMapText fires with (dxxText, filename) every time a message arrives
    // on `topic`. `filename` is whatever the producer attached (e.g. the
    // "filename" field of the toolkit's `cb64 | sendws` shape - see
    // dotnet/TOOLKIT.md) or empty if the producer sent none (e.g. a plain
    // `sendws --topic map` line publishing the DXX text as a bare JSON
    // string). onConnectionChanged fires with true right after the
    // subscribe handshake succeeds and with false when that connection is
    // lost (before each reconnect attempt) - so a caller can reflect live
    // hub status (e.g. in a window title) without polling.
    HubClient(std::string host, unsigned short port, std::string topic,
               std::function<void(std::string dxxText, std::string filename)> onMapText,
               std::function<void(bool)> onConnectionChanged);

    // Raw mode: delivers every non-control-reply broadcast on `topic`
    // verbatim, with none of the DXX {filename,content_base64}/bare-string
    // shape detection the constructor above does - for JSON topics like
    // "element_commands" that aren't DXX text at all. onConnectionChanged
    // behaves exactly as above.
    HubClient(std::string host, unsigned short port, std::string topic,
               std::function<void(std::string rawMessage)> onRawMessage,
               std::function<void(bool)> onConnectionChanged);
    ~HubClient();

    HubClient(const HubClient&) = delete;
    HubClient& operator=(const HubClient&) = delete;

private:
    void run();

    std::string m_host;
    unsigned short m_port;
    std::string m_topic;
    bool m_rawMode = false;
    std::function<void(std::string, std::string)> m_onMapText;
    std::function<void(std::string)> m_onRawMessage;
    std::function<void(bool)> m_onConnectionChanged;
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
};

// One-shot publish: connects, completes the WS handshake, sends a
// {"command":"publish","topic":...,"data":<jsonData>} envelope as a single
// text frame, then disconnects - no persistent subscribe/receive loop, since
// a publish is one-way (used by "Draw in AutoCAD" to send curve geometry to
// hsbWebSocketHub's "acad_geometry" topic instead of WM_COPYDATA - see
// FltkMainWindow.cpp's sendGeometryToHost). Blocking; returns true only if
// the connect+handshake+send all succeeded (does not wait for or verify the
// hub's own publish ack).
bool PublishToHub(const std::string& host, unsigned short port,
                    const std::string& topic, const std::string& jsonData);

} // namespace dxxviewer
