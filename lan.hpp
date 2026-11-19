#pragma once
#include "match.hpp"
#include <deque>

namespace chess {
constexpr unsigned lanPort=49373;
// Platform services. Polling must never wait for Wi-Fi association.
bool wifiReady(uint64_t now);
std::string wifiAddress();
uint64_t sessionSeed();
#if defined(_WIN32)
using Socket=uintptr_t;
#else
using Socket=int;
#endif
class Lan {
    static constexpr Socket invalid=static_cast<Socket>(-1);
    Socket listener=invalid, peer=invalid;
    Framer framer;
    std::deque<Bytes> output;
    size_t sent=0;
    uint64_t token=0, lastRx=0, nextPing=0, nextConnect=0, retryUntil=0;
    uint64_t clockTick=0;
    uint32_t nextId=1, waiting=0;
    bool connecting=false, closeAfterWrite=false;
    void sendMessage(const Bytes&);
    void disconnect();
    bool receive(const Bytes&);
    void startListener();
    void startClient();
    bool flush();
public:
    enum Role { Off, Host, Guest };
    Role role=Off;
    bool ready=false;
    Match match;
    std::string status, address;
    ~Lan() { stop(); }
    void begin(Role,Color,const std::string&,uint64_t);
    void stop();
    void tick(uint64_t);
    void retry(uint64_t);
    bool submit(Action,Move={});
    Color side() const { return role==Host?match.host:opposite(match.host); }
    bool acceptingInput() const { return ready && !waiting; }
    bool timedOut(uint64_t now) const { return role==Guest && !ready && now>=retryUntil; }
    void interrupt(uint64_t now);
};
}
