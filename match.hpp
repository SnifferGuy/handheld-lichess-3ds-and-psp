#pragma once
#include "chess.hpp"

namespace chess {
enum class Action : uint8_t { Move, Claim, ClaimWithMove, Offer, Accept, Resign, Rematch, Takeback, AcceptTakeback, DeclineTakeback, DeclineDraw };
struct Request { uint32_t revision=0, id=0; Action action{}; Move move{}; };
struct Match {
    GameState game;
    uint32_t revision=0, round=1;
    Color host=White;
    int8_t offer=-1;
    int8_t takeback=-1;
    uint8_t rematch=0;
    uint32_t lastGuestRequest=0;
    uint32_t initialMs=0, incrementMs=0;
    std::array<uint32_t,2> remaining{};
    bool flagFall=false;
    std::vector<std::array<uint32_t,2>> clocksBeforeMove;
    bool undoTo(size_t plies);
    void setClock(uint32_t initial, uint32_t increment);
    bool elapse(uint64_t elapsed, bool authoritative);
    bool apply(Color actor, Request request, bool guest);
};
using Bytes=std::vector<uint8_t>;
constexpr unsigned maxPayload=61000;
constexpr unsigned maxPlies=20000;
enum class Kind : uint8_t { Hello=1, State, Request, Ping, Reject };
Bytes hello(uint64_t token);
bool readHello(const Bytes&, uint64_t& token);
Bytes stateMessage(const Match&, uint64_t token);
bool readState(const Bytes&, Match&, uint64_t& token);
Bytes requestMessage(Request);
bool readRequest(const Bytes&, Request&);
Bytes message(Kind, uint8_t argument=0);
Kind kind(const Bytes&);
Bytes frame(const Bytes& payload);
struct Framer {
    Bytes pending;
    bool feed(const uint8_t* data, size_t size, std::vector<Bytes>& frames);
};
bool validAddress(const std::string& address);
}
