#include "HubClient.h"

// winsock2.h must be included before windows.h ever is in this translation
// unit (FL/Fl.H may pull windows.h in) or the old winsock.h it drags in
// conflicts with winsock2.h.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <FL/Fl.H>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>

#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif

namespace dxxviewer {

namespace {

// A recv() timeout short enough that Stop() (via m_stop) is noticed quickly,
// both while waiting for hub traffic and while waiting to retry a connection.
constexpr int kSocketTimeoutMs = 300;
constexpr int kReconnectDelayMs = 2000;

std::string base64Encode(const unsigned char* data, size_t len) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 2 < len; i += 3) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += table[(n >> 6) & 0x3F];
        out += table[n & 0x3F];
    }
    if (len - i == 1) {
        uint32_t n = data[i] << 16;
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += "==";
    } else if (len - i == 2) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
        out += table[(n >> 18) & 0x3F];
        out += table[(n >> 12) & 0x3F];
        out += table[(n >> 6) & 0x3F];
        out += '=';
    }
    return out;
}

std::string makeWebSocketKey() {
    unsigned char raw[16];
    std::random_device rd;
    for (auto& b : raw) b = static_cast<unsigned char>(rd() & 0xFF);
    return base64Encode(raw, sizeof(raw));
}

// Heuristic match for the hub's control-reply envelope (subscribe/publish
// ack: {"id":...,"command":...,"ok":...}) vs. a topic broadcast, which is
// always the bare `data` value with no such wrapper - same distinction
// wsget makes (see hsbClEventsSend/AGENTS.md).
bool isControlReply(const std::string& msg) {
    return msg.find("\"command\"") != std::string::npos &&
           msg.find("\"ok\"") != std::string::npos;
}

// Decodes a single JSON string literal ("...") into its raw text. Only what
// a producer publishing DXX text as `data` can actually produce - not a
// general JSON parser. Returns false if `msg` isn't a quoted string.
bool decodeJsonStringLiteral(const std::string& msg, std::string& out) {
    size_t begin = msg.find_first_not_of(" \t\r\n");
    size_t end = msg.find_last_not_of(" \t\r\n");
    if (begin == std::string::npos || end - begin < 1) return false;
    if (msg[begin] != '"' || msg[end] != '"') return false;

    out.clear();
    for (size_t i = begin + 1; i < end; ++i) {
        char c = msg[i];
        if (c != '\\' || i + 1 >= end) { out += c; continue; }
        char esc = msg[++i];
        switch (esc) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                if (i + 4 >= end) break;
                unsigned code = 0;
                for (int k = 0; k < 4; ++k) {
                    char h = msg[i + 1 + k];
                    code <<= 4;
                    if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
                    else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                }
                i += 4;
                // Encode as UTF-8 (BMP code points only - real DXX text
                // never needs surrogate pairs).
                if (code < 0x80) {
                    out += static_cast<char>(code);
                } else if (code < 0x800) {
                    out += static_cast<char>(0xC0 | (code >> 6));
                    out += static_cast<char>(0x80 | (code & 0x3F));
                } else {
                    out += static_cast<char>(0xE0 | (code >> 12));
                    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (code & 0x3F));
                }
                break;
            }
            default: out += esc; break;
        }
    }
    return true;
}

std::string base64Decode(const std::string& in) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (c == '=') break;
        const char* p = std::strchr(table, static_cast<char>(c));
        if (!p) continue;
        val = (val << 6) + static_cast<int>(p - table);
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

// Extracts one string field's raw value from a flat, single-level JSON
// object - matched to exactly the {filename, size, content_base64} shape
// the toolkit's `cb64` emits (dotnet/TOOLKIT.md), the same way `cb64dec`
// reads it back. Not a general JSON parser.
bool extractJsonObjectStringField(const std::string& json, const std::string& field, std::string& out) {
    size_t pos = json.find("\"" + field + "\"");
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return false;
    pos = json.find('"', pos);
    if (pos == std::string::npos) return false;

    out.clear();
    for (size_t i = pos + 1; i < json.size() && json[i] != '"'; ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) { out += json[++i]; continue; }
        out += json[i];
    }
    return true;
}

bool sendAll(SOCKET sock, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = send(sock, data + sent, static_cast<int>(len - sent), 0);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

// Loops recv() past SO_RCVTIMEO wakeups so a slow/idle connection still lets
// Stop() be noticed within kSocketTimeoutMs, without treating a timeout as a
// real error.
bool recvExact(SOCKET sock, char* buf, size_t n, const std::atomic<bool>& stop) {
    size_t got = 0;
    while (got < n) {
        if (stop.load()) return false;
        int r = recv(sock, buf + got, static_cast<int>(n - got), 0);
        if (r > 0) { got += static_cast<size_t>(r); continue; }
        if (r == 0) return false; // peer closed
        if (WSAGetLastError() == WSAETIMEDOUT || WSAGetLastError() == WSAEWOULDBLOCK) continue;
        return false;
    }
    return true;
}

void sendFrame(SOCKET sock, uint8_t opcode, const std::string& payload) {
    std::string frame;
    frame.push_back(static_cast<char>(0x80 | (opcode & 0x0F))); // FIN=1

    size_t len = payload.size();
    if (len <= 125) {
        frame.push_back(static_cast<char>(0x80 | len)); // MASK=1
    } else if (len <= 0xFFFF) {
        frame.push_back(static_cast<char>(0x80 | 126));
        frame.push_back(static_cast<char>((len >> 8) & 0xFF));
        frame.push_back(static_cast<char>(len & 0xFF));
    } else {
        frame.push_back(static_cast<char>(0x80 | 127));
        for (int shift = 56; shift >= 0; shift -= 8)
            frame.push_back(static_cast<char>((len >> shift) & 0xFF));
    }

    unsigned char maskKey[4];
    std::random_device rd;
    for (auto& b : maskKey) b = static_cast<unsigned char>(rd() & 0xFF);
    frame.append(reinterpret_cast<char*>(maskKey), 4);

    size_t base = frame.size();
    frame.resize(base + len);
    for (size_t i = 0; i < len; ++i)
        frame[base + i] = static_cast<char>(payload[i] ^ maskKey[i % 4]);

    sendAll(sock, frame.data(), frame.size());
}

// Reads and reassembles one complete (possibly fragmented) WebSocket
// message, answering pings and treating a close frame as end-of-connection.
// Returns false on any error, close, or Stop() request.
bool readMessage(SOCKET sock, const std::atomic<bool>& stop, std::string& outMessage) {
    outMessage.clear();
    bool haveOpcode = false;

    for (;;) {
        unsigned char hdr[2];
        if (!recvExact(sock, reinterpret_cast<char*>(hdr), 2, stop)) return false;

        bool fin = (hdr[0] & 0x80) != 0;
        uint8_t opcode = hdr[0] & 0x0F;
        bool masked = (hdr[1] & 0x80) != 0;
        uint64_t len = hdr[1] & 0x7F;

        if (len == 126) {
            unsigned char ext[2];
            if (!recvExact(sock, reinterpret_cast<char*>(ext), 2, stop)) return false;
            len = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
        } else if (len == 127) {
            unsigned char ext[8];
            if (!recvExact(sock, reinterpret_cast<char*>(ext), 8, stop)) return false;
            len = 0;
            for (int i = 0; i < 8; ++i) len = (len << 8) | ext[i];
        }

        unsigned char maskKey[4] = {0, 0, 0, 0};
        if (masked && !recvExact(sock, reinterpret_cast<char*>(maskKey), 4, stop)) return false;

        std::string payload(static_cast<size_t>(len), '\0');
        if (len && !recvExact(sock, payload.data(), static_cast<size_t>(len), stop)) return false;
        if (masked)
            for (size_t i = 0; i < payload.size(); ++i)
                payload[i] = static_cast<char>(payload[i] ^ maskKey[i % 4]);

        if (opcode == 0x8) return false; // close
        if (opcode == 0x9) { sendFrame(sock, 0xA, payload); continue; } // ping -> pong
        if (opcode == 0xA) continue; // pong

        if (opcode != 0x0) haveOpcode = true; // 0x1 text (only text expected from the hub)
        outMessage += payload;
        if (fin) return haveOpcode;
    }
}

// Runs `fn` on the FLTK main thread via Fl::awake, from any thread.
void postToMain(std::function<void()> fn) {
    auto* payload = new std::function<void()>(std::move(fn));
    Fl::awake([](void* v) {
        auto* p = static_cast<std::function<void()>*>(v);
        (*p)();
        delete p;
    }, payload);
}

} // anonymous namespace

HubClient::HubClient(std::string host, unsigned short port, std::string topic,
                       std::function<void(std::string, std::string)> onMapText,
                       std::function<void(bool)> onConnectionChanged)
    : m_host(std::move(host)), m_port(port), m_topic(std::move(topic)),
      m_onMapText(std::move(onMapText)),
      m_onConnectionChanged(std::move(onConnectionChanged)) {
    m_thread = std::thread([this] { run(); });
}

HubClient::~HubClient() {
    m_stop = true;
    if (m_thread.joinable()) m_thread.join();
}

void HubClient::run() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return;

    while (!m_stop.load()) {
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) break;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(m_port);
        inet_pton(AF_INET, m_host.c_str(), &addr.sin_addr);

        bool ok = connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;

        if (ok) {
            DWORD timeout = kSocketTimeoutMs;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

            std::string key = makeWebSocketKey();
            std::string request =
                "GET /ws HTTP/1.1\r\n"
                "Host: " + m_host + ":" + std::to_string(m_port) + "\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Key: " + key + "\r\n"
                "Sec-WebSocket-Version: 13\r\n"
                "\r\n";
            ok = sendAll(sock, request.data(), request.size());
        }

        if (ok) {
            // Read the HTTP response line-by-line (no leftover WS frame
            // bytes to preserve: the hub always ends the handshake at
            // \r\n\r\n before writing anything else).
            std::string statusLine;
            char c = 0;
            std::string headerBuf;
            bool headerDone = false;
            while (!headerDone && !m_stop.load()) {
                if (!recvExact(sock, &c, 1, m_stop)) { ok = false; break; }
                headerBuf += c;
                if (headerBuf.size() >= 4 &&
                    headerBuf.compare(headerBuf.size() - 4, 4, "\r\n\r\n") == 0)
                    headerDone = true;
            }
            ok = ok && headerDone && headerBuf.compare(0, 9, "HTTP/1.1 ") == 0
                     && headerBuf.compare(9, 3, "101") == 0;
        }

        if (ok) {
            std::string subscribe =
                "{\"id\":\"1\",\"command\":\"subscribe\",\"topic\":\"" + m_topic + "\"}";
            sendFrame(sock, 0x1, subscribe);
            postToMain([this] { m_onConnectionChanged(true); });

            std::string message;
            while (!m_stop.load() && readMessage(sock, m_stop, message)) {
                if (!isControlReply(message)) {
                    size_t firstCh = message.find_first_not_of(" \t\r\n");
                    std::string text, filename;
                    bool decoded = false;

                    if (firstCh != std::string::npos && message[firstCh] == '{') {
                        // The toolkit's `cb64 | sendws` shape: a JSON object
                        // {filename, size, content_base64} - see dotnet/TOOLKIT.md.
                        std::string b64;
                        if (extractJsonObjectStringField(message, "content_base64", b64)) {
                            text = base64Decode(b64);
                            extractJsonObjectStringField(message, "filename", filename);
                            decoded = true;
                        }
                    } else if (firstCh != std::string::npos && message[firstCh] == '"') {
                        // A producer publishing the raw DXX text directly as a
                        // JSON string (e.g. a plain `sendws --topic map` line).
                        decoded = decodeJsonStringLiteral(message, text);
                    }

                    if (decoded) {
                        postToMain([this, text = std::move(text), filename = std::move(filename)]() mutable {
                            m_onMapText(std::move(text), std::move(filename));
                        });
                    } else {
                        std::fprintf(stderr, "dxxviewer: ignoring unrecognized map payload shape on topic '%s'\n",
                                     m_topic.c_str());
                    }
                }
            }

            postToMain([this] { m_onConnectionChanged(false); });
        }

        closesocket(sock);
        if (m_stop.load()) break;

        for (int waited = 0; waited < kReconnectDelayMs && !m_stop.load(); waited += 100)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    WSACleanup();
}

} // namespace dxxviewer
