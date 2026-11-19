#pragma once
#include "chess.hpp"
#include <atomic>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace handheld {
enum class AccountCheck { Verified, Rejected, RateLimited, InvalidReply, Failed };
struct AccountProfile {
    AccountCheck status=AccountCheck::Failed;
    std::string username;
};
struct ActiveGame {
    std::string opponent;
    std::string id;
    bool yourTurn=false;
    chess::Color player=chess::White;
    std::optional<chess::Position> position;
};
constexpr unsigned activeGameLimit=5;
struct ActiveGames {
    AccountCheck status=AccountCheck::Failed;
    std::vector<ActiveGame> games;
};
struct LiveBoard {
    chess::Position position;
    chess::Position initialPosition=chess::Position::start();
    uint32_t initialMs=0;
    std::optional<chess::Move> lastMove;
    std::array<uint32_t,2> remainingMs{}, incrementMs{};
    std::array<bool,2> drawOffer{}, takebackOffer{};
    uint64_t receivedAt=0;
    uint32_t synchronizedRequest=0; // Local stream generation, not a chess revision.
    unsigned plies=0;
    std::vector<chess::Move> history;
    bool realtimeClock=false, clockRunning=false;
    bool finished=false;
    std::string status;
    std::optional<chess::Color> winner;
    // A display estimate only: reaching zero never adjudicates the game locally.
    uint32_t remainingAt(chess::Color player,uint64_t now) const;
    std::string resultLabel() const;
};
enum class PostResult { None, Sending, Accepted, Rejected, RateLimited, Uncertain };
enum class BoardAction { Move, OfferDraw, AcceptDraw, DeclineDraw, RequestTakeback,
                         AcceptTakeback, DeclineTakeback, Resign };
bool boardActionAvailable(const LiveBoard&,chess::Color player,BoardAction);
// A POST reply alone never unlocks the next move; require a fresh server snapshot too.
class OnlineRequestGate {
    uint32_t serial=0, synchronized=0;
    PostResult delivery=PostResult::None;
public:
    uint32_t requestId() const { return serial; }
    PostResult result() const { return delivery; }
    bool blocked() const { return serial && (delivery==PostResult::Sending || synchronized<serial); }
    std::optional<uint32_t> begin(const LiveBoard&,chess::Color,chess::Move,bool connected);
    std::optional<uint32_t> beginAction(const LiveBoard&,chess::Color,BoardAction,bool connected);
    void complete(PostResult result);
    void synchronize(uint32_t request) { if (request>synchronized) synchronized=request; }
};
using LiveBoardReceiver=void (*)(void*,const LiveBoard&);
struct ChatMessage;
using ChatReceiver=void (*)(void*,const ChatMessage&);
class GameStreamDecoder {
    std::string id, line, appliedMoves;
    chess::Position initial;
    LiveBoard board;
    bool started=false;
    ChatReceiver chatReceiver=nullptr;
    void* chatTarget=nullptr;
    bool consume(LiveBoardReceiver,void*);
public:
    explicit GameStreamDecoder(std::string_view gameId,ChatReceiver receiveChat=nullptr,void* target=nullptr)
        :id(gameId),chatReceiver(receiveChat),chatTarget(target) {}
    bool feed(std::string_view bytes,LiveBoardReceiver receive,void* target);
    bool complete() const { return started && board.finished && line.empty(); }
};
bool accountHttpsAvailable();
std::string accountUsername(std::string_view json);
std::optional<std::vector<ActiveGame>> parseActiveGames(std::string_view json);
AccountProfile verifyLichessAccount(const std::string& token,const char* caPath,
                                    const std::atomic<bool>& cancelled);
ActiveGames fetchActiveGames(const std::string& token,const char* caPath,
                             const std::atomic<bool>& cancelled);
AccountCheck streamGameBoard(const std::string& gameId,const std::string& token,const char* caPath,
                             const std::atomic<bool>& cancelled,LiveBoardReceiver receive,void* target,
                             const std::atomic<bool>* interrupted=nullptr,ChatReceiver chatReceiver=nullptr,void* chatTarget=nullptr);
std::optional<std::string> boardMovePath(std::string_view gameId,chess::Move move);
std::optional<std::string> boardActionPath(std::string_view gameId,BoardAction);
PostResult postReply(long httpStatus,std::string_view body,bool transferCompleted);
PostResult postBoardMove(const std::string& gameId,chess::Move move,const std::string& token,
                            const char* caPath,const std::atomic<bool>& cancelled);
PostResult postBoardAction(const std::string& gameId,BoardAction,const std::string& token,
                          const char* caPath,const std::atomic<bool>& cancelled);
// Platform-independent service boundary; desktop fixtures supply a simulated server.
struct LichessService {
    decltype(&accountHttpsAvailable) available=accountHttpsAvailable;
    decltype(&verifyLichessAccount) verify=verifyLichessAccount;
    decltype(&fetchActiveGames) games=fetchActiveGames;
    decltype(&streamGameBoard) stream=streamGameBoard;
    decltype(&postBoardMove) move=postBoardMove;
    decltype(&postBoardAction) action=postBoardAction;
};
}
