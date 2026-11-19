#include "lan.hpp"
#include <algorithm>
#include <cerrno>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace chess {
namespace {
constexpr Socket bad=static_cast<Socket>(-1);
#ifdef _WIN32
void closeSocket(Socket s) { if (s!=bad) closesocket(s); }
bool nonblocking(Socket s) { unsigned long enabled=1; return ioctlsocket(s,FIONBIO,&enabled)==0; }
bool again() {
    const int e=WSAGetLastError();
    return e==WSAEWOULDBLOCK || e==WSAEINPROGRESS || e==WSAEALREADY || e==WSAEINTR;
}
#else
void closeSocket(Socket s) { if (s!=bad) close(s); }
bool nonblocking(Socket s) {
#ifdef __PSP__
    const int yes=1;
    return setsockopt(s,SOL_SOCKET,SO_NONBLOCK,&yes,sizeof(yes))==0;
#else
    const int flags=fcntl(s,F_GETFL,0);
    return flags>=0 && fcntl(s,F_SETFL,flags|O_NONBLOCK)==0;
#endif
}
bool again() { return errno==EWOULDBLOCK || errno==EAGAIN || errno==EINPROGRESS ||
    errno==EALREADY || errno==EINTR; }
#endif
Socket openSocket() {
    const Socket s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if (s!=bad && !nonblocking(s)) { closeSocket(s); return bad; }
    return s;
}
sockaddr_in endpoint(const std::string& address) {
    sockaddr_in a{};
    a.sin_family=AF_INET;
    a.sin_port=htons(lanPort);
    a.sin_addr.s_addr=address.empty()?htonl(INADDR_ANY):inet_addr(address.c_str());
    return a;
}
int connectStatus(Socket s) {
    fd_set out,error;
    FD_ZERO(&out); FD_SET(s,&out);
    FD_ZERO(&error); FD_SET(s,&error);
    timeval timeout{};
    const int count=select(
#ifdef _WIN32
        0,
#else
        s+1,
#endif
        nullptr,&out,&error,&timeout);
    if (count<0) return again()?0:-1;
    if (!count) return 0;
    int value=0;
#ifdef _WIN32
    int length=sizeof(value);
#else
    socklen_t length=sizeof(value);
#endif
    return getsockopt(s,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&value),&length)==0 && !value &&
           FD_ISSET(s,&out)?1:-1;
}
}
void Lan::disconnect() {
    closeSocket(peer);
    peer=invalid;
    ready=connecting=closeAfterWrite=false;
    framer.pending.clear();
    output.clear();
    sent=0;
    waiting=0;
}
void Lan::stop() {
    disconnect();
    closeSocket(listener);
    listener=invalid;
    role=Off;
    token=0;
}
void Lan::begin(Role mode,Color host,const std::string& ip,uint64_t now) {
    stop();
    role=mode;
    match=Match{};
    match.host=host;
    if (role==Guest) {
        match.round=0;
        token=sessionSeed();
        if (!token) token=1;
    }
    address=ip;
    nextConnect=clockTick=now;
    retryUntil=now+20000;
    nextId=1;
    status="Connecting to Wi-Fi";
}
void Lan::retry(uint64_t now) {
    if (role!=Guest) return;
    disconnect();
    retryUntil=now+120000;
    nextConnect=now;
}
void Lan::interrupt(uint64_t now) {
    if (role==Off) return;
    clockTick=now;
    const bool live=ready;
    disconnect();
    closeSocket(listener);
    listener=invalid;
    nextConnect=now;
    if (role==Guest && live) retryUntil=now+120000;
}
void Lan::sendMessage(const Bytes& payload) {
    if (output.size()>=4) { disconnect(); return; }
    auto bytes=frame(payload);
    if (!bytes.empty()) output.push_back(std::move(bytes));
}
bool Lan::flush() {
    if (output.empty()) return true;
    auto& b=output.front();
    const int count=::send(peer,reinterpret_cast<const char*>(b.data()+sent),
                           int(std::min<size_t>(4096,b.size()-sent)),0);
    if (count<=0) return count<0 && again();
    sent+=count;
    if (sent==b.size()) { output.pop_front(); sent=0; }
    return true;
}
bool Lan::receive(const Bytes& payload) {
    const auto type=kind(payload);
    if (role==Host && !ready) {
        uint64_t requested=0;
        if (!readHello(payload,requested) || !requested || (token && token!=requested)) {
            sendMessage(message(Kind::Reject,type==Kind{}?1:2));
            closeAfterWrite=true;
            return true;
        }
        token=requested;
        ready=true;
        sendMessage(stateMessage(match,token));
        return true;
    }
    if (role==Guest && type==Kind::Reject && payload.size()==3) {
        status=payload[2]==1?"Different game version":"Host session unavailable";
        retryUntil=0;
        return false;
    }
    if (role==Guest && type==Kind::State) {
        if (!readState(payload,match,token)) return false;
        ready=true;
        if (waiting && match.lastGuestRequest>=waiting) waiting=0;
        return true;
    }
    if (!ready) return false;
    if (type==Kind::Ping) return payload.size()==3;
    if (role==Host && type==Kind::Request) {
        Request request;
        if (!readRequest(payload,request)) return false;
        match.apply(opposite(match.host),request,true);
        sendMessage(stateMessage(match,token));
        return true;
    }
    return false;
}
void Lan::startListener() {
    listener=openSocket();
    if (listener==invalid) return;
    const int yes=1;
    setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,reinterpret_cast<const char*>(&yes),sizeof(yes));
    const auto a=endpoint({});
    if (bind(listener,reinterpret_cast<const sockaddr*>(&a),sizeof(a))!=0 || listen(listener,1)!=0) {
        closeSocket(listener);
        listener=invalid;
    }
}
void Lan::startClient() {
    peer=openSocket();
    if (peer==invalid) return;
    const auto a=endpoint(address);
    if (connect(peer,reinterpret_cast<const sockaddr*>(&a),sizeof(a))==0) {
        sendMessage(hello(token));
    } else if (again()) {
        connecting=true;
    } else {
        disconnect();
    }
}
void Lan::tick(uint64_t now) {
    if (role==Off) return;
    const auto elapsed=now>=clockTick?now-clockTick:0;
    clockTick=now;
    if (ready && match.elapse(elapsed,role==Host)) sendMessage(stateMessage(match,token));
    if (!wifiReady(now)) {
        if (listener!=invalid || peer!=invalid) interrupt(now);
        if (!timedOut(now) || status=="Connecting to Wi-Fi") status="Connecting to Wi-Fi";
        return;
    }
    if (role==Host) {
        address=wifiAddress();
        if (listener==invalid && now>=nextConnect) {
            startListener();
            nextConnect=now+1000;
        }
        if (peer==invalid && listener!=invalid) {
            peer=accept(listener,nullptr,nullptr);
            if (peer!=invalid) {
                if (!nonblocking(peer)) disconnect();
                lastRx=now;
            } else if (!again()) {
                closeSocket(listener);
                listener=invalid;
            }
        }
        status=ready?"Connected":token?"Waiting for reconnect":
            listener==invalid?"Cannot open LAN listener":"Waiting for a player";
    } else {
        if (peer==invalid && now<retryUntil && now>=nextConnect) {
            startClient();
            nextConnect=now+1000;
            lastRx=now;
        }
        if (now<retryUntil || ready) status=ready?"Connected":match.round?"Reconnecting":"Connecting to host";
    }
    if (peer==invalid) return;
    const auto lost=[&] {
        if (role==Guest && ready) retryUntil=now+120000;
        disconnect();
    };
    if (now-lastRx>8000 || (!ready && role==Guest && now>=retryUntil)) { lost(); return; }
    if (connecting) {
        const int connected=connectStatus(peer);
        if (connected<0) { lost(); return; }
        if (!connected) return;
        connecting=false;
        sendMessage(hello(token));
    }
    if (ready && output.empty() && now>=nextPing) {
        nextPing=now+2000;
        sendMessage(message(Kind::Ping));
    }
    if (!flush()) { lost(); return; }
    if (closeAfterWrite) {
        if (output.empty()) disconnect();
        return;
    }
    uint8_t buf[4096];
    const int n=recv(peer,reinterpret_cast<char*>(buf),sizeof(buf),0);
    if (n<0 && again()) return;
    if (n<=0) { lost(); return; }
    std::vector<Bytes> payloads;
    if (!framer.feed(buf,n,payloads)) { lost(); return; }
    for (const auto& payload:payloads) {
        if (!receive(payload)) { lost(); return; }
        lastRx=now;
        if (peer==invalid || closeAfterWrite) break;
    }
}
bool Lan::submit(Action action,Move move) {
    if (!acceptingInput()) return false;
    Request request{match.revision,nextId++,action,move};
    if (role==Guest) {
        waiting=request.id;
        sendMessage(requestMessage(request));
        return true;
    }
    if (!match.apply(match.host,request,false)) return false;
    sendMessage(stateMessage(match,token));
    return true;
}
}
