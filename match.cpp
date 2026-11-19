#include "match.hpp"
#include <algorithm>
#include <utility>

namespace chess {
void Match::setClock(uint32_t initial, uint32_t increment) {
    initialMs=initial;
    incrementMs=increment;
    remaining.fill(initial);
    flagFall=false;
}
bool Match::elapse(uint64_t elapsed, bool authoritative) {
    if (!initialMs || game.result!=Result::Playing) return false;
    auto& time=remaining[game.pos.turn];
    time-=uint32_t(std::min<uint64_t>(time,elapsed));
    if (time || !authoritative) return false;
    const auto winner=opposite(game.pos.turn);
    const bool hasPiece=std::any_of(game.pos.board.begin(),game.pos.board.end(),[winner](int piece) {
        return winner==White?(piece>0 && piece!=King):(piece<0 && piece!=-King);
    });
    if (!hasPiece || game.pos.dead()) game.result=Result::Draw;
    else game.resign(game.pos.turn);
    flagFall=true;
    offer=takeback=-1;
    ++revision;
    return true;
}
bool Match::undoTo(size_t plies) {
    if (plies>=game.history.size() || clocksBeforeMove.size()!=game.history.size()) return false;
    GameState restored;
    for (size_t i=0;i<plies;++i)
        if (!restored.play(game.history[i])) return false;
    remaining=clocksBeforeMove[plies];
    clocksBeforeMove.resize(plies);
    game=std::move(restored);
    offer=takeback=-1;
    flagFall=false;
    ++revision;
    return true;
}
bool Match::apply(Color actor, Request r, bool guest) {
    if (guest && (!r.id || r.id<=lastGuestRequest)) return false;
    if (guest) lastGuestRequest=r.id;
    if (r.revision!=revision) return false;
    const bool playing=game.result==Result::Playing;
    switch(r.action) {
        case Action::Move:
            if (actor!=game.pos.turn || !game.play(r.move)) return false;
            clocksBeforeMove.push_back(remaining);
            if (initialMs) remaining[actor]+=incrementMs;
            takeback=-1;
            if (offer!=actor) offer=-1;
            break;
        case Action::Claim:
        case Action::ClaimWithMove:
            if (actor!=game.pos.turn ||
                !game.canClaim(r.action==Action::ClaimWithMove?&r.move:nullptr)) return false;
            game.result=Result::Draw;
            break;
        case Action::Offer:
            if (!playing || offer>=0 || game.history.size()<2) return false;
            offer=actor;
            break;
        case Action::Accept:
            if (!playing || offer!=opposite(actor)) return false;
            game.result=Result::Draw;
            break;
        case Action::Takeback:
            if (!playing || game.history.empty() || takeback>=0) return false;
            takeback=actor;
            break;
        case Action::AcceptTakeback:
            if (!playing || takeback!=opposite(actor)) return false;
            return undoTo(game.history.size()-1);
        case Action::DeclineTakeback:
            if (!playing || takeback!=opposite(actor)) return false;
            takeback=-1;
            break;
        case Action::DeclineDraw:
            if (!playing || offer!=opposite(actor)) return false;
            offer=-1;
            break;
        case Action::Resign:
            if (!playing) return false;
            game.resign(actor);
            break;
        case Action::Rematch: {
            const unsigned bit=1u<<actor;
            if (playing || (rematch&bit)) return false;
            rematch|=bit;
            if (rematch==3) {
                ++round;
                host=opposite(host);
                game=GameState{};
                clocksBeforeMove.clear();
                setClock(initialMs,incrementMs);
                rematch=0;
            }
            break;
        }
        default: return false;
    }
    if (game.result!=Result::Playing) offer=takeback=-1;
    ++revision;
    return true;
}

namespace {
constexpr uint8_t version=3;
void put(Bytes& out,uint64_t n,int bytes) {
    for (int i=bytes-1;i>=0;--i) out.push_back(uint8_t(n>>(8*i)));
}
struct Reader {
    const Bytes& data;
    size_t at=2;
    uint64_t get(int n) {
        uint64_t value=0;
        while (n--) value=(value<<8)|data[at++];
        return value;
    }
    Move move() {
        const auto from=uint8_t(get(1)), to=uint8_t(get(1)), promo=uint8_t(get(1));
        return {from,to,promo};
    }
};
void putMove(Bytes& b,Move m) { b.insert(b.end(),{m.from,m.to,m.promotion}); }
}
Kind kind(const Bytes& b) {
    if (b.size()<2 || b[0]!=version) return Kind{};
    return static_cast<Kind>(b[1]);
}
Bytes message(Kind k,uint8_t arg) { return {version,uint8_t(k),arg}; }
Bytes hello(uint64_t token) {
    Bytes b{version,uint8_t(Kind::Hello)};
    put(b,token,8);
    return b;
}
bool readHello(const Bytes& b,uint64_t& token) {
    if (kind(b)!=Kind::Hello || b.size()!=10) return false;
    token=Reader{b}.get(8);
    return true;
}
Bytes requestMessage(Request r) {
    Bytes b{version,uint8_t(Kind::Request)};
    put(b,r.revision,4); put(b,r.id,4); put(b,uint8_t(r.action),1);
    putMove(b,r.move);
    return b;
}
bool readRequest(const Bytes& b,Request& out) {
    if (kind(b)!=Kind::Request || b.size()!=14) return false;
    Reader r{b};
    out.revision=uint32_t(r.get(4)); out.id=uint32_t(r.get(4));
    const auto action=r.get(1);
    if (!out.id || action>uint8_t(Action::DeclineDraw)) return false;
    out.action=static_cast<Action>(action);
    out.move=r.move();
    return true;
}
Bytes stateMessage(const Match& m,uint64_t token) {
    Bytes b{version,uint8_t(Kind::State)};
    b.reserve(29+m.game.history.size()*3);
    put(b,token,8);
    put(b,m.revision,4); put(b,m.round,4); put(b,m.lastGuestRequest,4);
    b.insert(b.end(),{uint8_t(m.host),uint8_t(m.offer+1),m.rematch,uint8_t(m.game.result)});
    put(b,m.initialMs,4); put(b,m.incrementMs,4);
    put(b,m.remaining[White],4); put(b,m.remaining[Black],4);
    put(b,m.flagFall,1);
    put(b,m.takeback+1,1);
    put(b,m.game.history.size(),2);
    for (auto move:m.game.history) putMove(b,move);
    return b;
}
bool readState(const Bytes& b,Match& out,uint64_t& token) {
    if (kind(b)!=Kind::State || b.size()<46) return false;
    Reader r{b};
    const auto receivedToken=r.get(8);
    if (!receivedToken || (token && token!=receivedToken)) return false;
    Match next;
    next.revision=uint32_t(r.get(4)); next.round=uint32_t(r.get(4));
    next.lastGuestRequest=uint32_t(r.get(4));
    const auto host=r.get(1), offer=r.get(1), rematch=r.get(1), result=r.get(1);
    next.initialMs=uint32_t(r.get(4)); next.incrementMs=uint32_t(r.get(4));
    next.remaining[White]=uint32_t(r.get(4)); next.remaining[Black]=uint32_t(r.get(4));
    const auto flag=r.get(1), takeback=r.get(1), count=r.get(2);
    if (takeback>2 || (takeback && (result || !count))) return false;
    next.takeback=int8_t(takeback)-1;
    if (flag>1 || (flag && (!result || !next.initialMs)) ||
        next.initialMs>1800000 || next.incrementMs>10000 ||
        (!next.initialMs && (next.incrementMs || next.remaining[White] || next.remaining[Black]))) return false;
    next.flagFall=flag!=0;
    if (!next.round || next.revision<out.revision || next.round<out.round ||
        host>1 || offer>2 || rematch>2 || result>3 || count>maxPlies ||
        b.size()!=r.at+count*3 || (result==0 && rematch) || (result!=0 && offer)) return false;
    next.host=static_cast<Color>(host); next.offer=int8_t(offer)-1; next.rematch=uint8_t(rematch);
    size_t prefix=0;
    if (token && next.round==out.round) {
        if (next.host!=out.host || next.lastGuestRequest<out.lastGuestRequest) return false;
        const auto movesStart=r.at;
        if (out.game.history.size()<=count) {
            for (auto previous:out.game.history) {
                if (!(r.move()==previous)) break;
                ++prefix;
            }
        }
        if (prefix==out.game.history.size()) next.game=out.game;
        else {
            if (next.revision<=out.revision) return false;
            prefix=0;
            r.at=movesStart;
        }
    }
    for (size_t i=prefix;i<count;++i)
        if (!next.game.play(r.move())) return false;
    if (next.game.result!=Result::Playing && uint8_t(next.game.result)!=result) return false;
    next.game.result=static_cast<Result>(result);
    out=std::move(next);
    token=receivedToken;
    return true;
}
Bytes frame(const Bytes& payload) {
    if (payload.size()<2 || payload.size()>maxPayload) return {};
    Bytes out;
    out.reserve(4+payload.size());
    put(out,payload.size(),4);
    out.insert(out.end(),payload.begin(),payload.end());
    return out;
}
bool Framer::feed(const uint8_t* data,size_t size,std::vector<Bytes>& frames) {
    while (size) {
        const size_t needed=pending.size()<4?4:
            4+(uint32_t(pending[0])<<24 | uint32_t(pending[1])<<16 |
                uint32_t(pending[2])<<8 | pending[3]);
        if (needed>maxPayload+4 || (pending.size()>=4 && needed<6)) return false;
        const size_t chunk=std::min(size,needed-pending.size());
        pending.insert(pending.end(),data,data+chunk);
        data+=chunk; size-=chunk;
        if (needed>4 && pending.size()==needed) {
            frames.emplace_back(pending.begin()+4,pending.end());
            pending.clear();
        }
    }
    // Reject a bad header even if it was the last fragment.
    if (pending.size()==4) {
        const auto n=uint32_t(pending[0])<<24 | uint32_t(pending[1])<<16 |
            uint32_t(pending[2])<<8 | pending[3];
        return n>=2 && n<=maxPayload;
    }
    return true;
}
bool validAddress(const std::string& s) {
    size_t at=0;
    for (int octet=0;octet<4;++octet) {
        int value=0, digits=0;
        const size_t start=at;
        while (at<s.size() && s[at]>='0' && s[at]<='9') {
            if (++digits>3) return false;
            value=value*10+s[at++]-'0';
        }
        if (!digits || value>255 || (digits>1 && s[start]=='0')) return false;
        if (octet==3) return at==s.size() && s!="0.0.0.0" && s!="255.255.255.255";
        if (at==s.size() || s[at++]!='.') return false;
    }
    return false;
}
}
