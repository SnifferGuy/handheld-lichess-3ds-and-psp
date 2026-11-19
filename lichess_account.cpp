#include "lichess_account.hpp"
#include "online_extra.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>
#include <cstring>

#define PICOJSON_ASSERT(test) do { if (!(test)) std::abort(); } while (false)
#define PICOJSON_USE_LOCALE 0
#include "picojson.hpp"

namespace {
constexpr size_t maxAccountBytes=262144, maxStreamBytes=32768;
bool millisecondsField(const picojson::value& value,uint32_t& result) {
    if (!value.is<double>()) return false;
    const double number=value.get<double>();
    if (!std::isfinite(number) || number<0 || number>2147483647 || std::floor(number)!=number)
        return false;
    result=uint32_t(number);
    return true;
}
bool knownStatus(std::string_view status) {
    constexpr std::string_view statuses[]={"created","started","aborted","mate","resign",
        "stalemate","timeout","draw","outoftime","cheat","noStart","unknownFinish",
        "insufficientMaterialClaim","variantEnd"};
    return std::find(std::begin(statuses),std::end(statuses),status)!=std::end(statuses);
}
bool validGameId(std::string_view id) {
    return id.size()==8 && id.find_first_not_of(
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")==std::string_view::npos;
}
bool applyUci(chess::Position& position,std::string_view uci,chess::Move& move) {
    if (uci.size()!=4 && uci.size()!=5) return false;
    const auto square=[](char file,char rank) {
        return file>='a' && file<='h' && rank>='1' && rank<='8'?(rank-'1')*8+file-'a':-1;
    };
    const int from=square(uci[0],uci[1]), to=square(uci[2],uci[3]);
    if (from<0 || to<0) return false;
    move={uint8_t(from),uint8_t(to),chess::Empty};
    if (uci.size()==5) {
        const auto promotion=std::string_view("nbrq").find(uci[4]);
        if (promotion==std::string_view::npos) return false;
        move.promotion=uint8_t(chess::Knight+promotion);
    }
    // The Board API can encode castling as a king move onto its own rook.
    const int back=position.turn==chess::White?0:56;
    const int king=position.turn==chess::White?chess::King:-chess::King;
    const int rook=position.turn==chess::White?chess::Rook:-chess::Rook;
    if (from==back+4 && (to==back || to==back+7) &&
        position.board[from]==king && position.board[to]==rook)
        move.to=uint8_t(back+(to==back?2:6));
    const auto legal=position.legal();
    if (std::find(legal.begin(),legal.end(),move)==legal.end()) return false;
    position=position.after(move);
    return true;
}
// PicoJSON uses recursive descent. Cap depth before parsing on a small worker stack.
bool boundedDepth(std::string_view body) {
    int depth=0;
    bool quoted=false, escape=false;
    for (const char ch:body) {
        if (quoted) {
            if (escape) escape=false;
            else if (ch=='\\') escape=true;
            else if (ch=='"') quoted=false;
        } else if (ch=='"') quoted=true;
        else if (ch=='{' || ch=='[') {
            if (++depth>16) return false;
        } else if (ch=='}' || ch==']') {
            if (--depth<0) return false;
        }
    }
    return depth==0 && !quoted;
}
bool parseObject(std::string_view json,picojson::value& object) {
    if (json.empty() || json.size()>maxAccountBytes || !boundedDepth(json)) return false;
    std::string error;
    const auto rest=picojson::parse(object,json.begin(),json.end(),&error);
    return error.empty() && object.is<picojson::object>() &&
        json.substr(size_t(rest-json.begin())).find_first_not_of(" \t\r\n")==std::string_view::npos;
}
}

namespace {
using WriteBody=size_t (*)(char*,size_t,size_t,void*);
size_t readBody(char* data,size_t size,size_t count,void* context) {
    auto& body=*static_cast<std::string*>(context);
    if (size && count>(maxAccountBytes-body.size())/size) return 0;
    const auto bytes=size*count;
    body.append(data,bytes);
    return bytes;
}
struct StreamBody {
    handheld::GameStreamDecoder decoder;
    handheld::LiveBoardReceiver receive;
    void* target;
    bool invalid=false;
};
size_t readStream(char* data,size_t size,size_t count,void* context) {
    auto& stream=*static_cast<StreamBody*>(context);
    const size_t bytes=size*count; // libcurl's write callback provides bounded chunks.
    stream.invalid=!stream.decoder.feed({data,bytes},stream.receive,stream.target);
    return stream.invalid?0:bytes;
}
}
#ifdef HANDHELD_CURL
#include <curl/curl.h>
namespace handheld { uint64_t milliseconds(); }
namespace {
struct TransferProgress {
    const std::atomic<bool>& cancelled;
    const std::atomic<bool>* interrupted;
    bool streaming;
    uint64_t lastActivity;
    curl_off_t downloaded=0;
};
int stopTransfer(void* context,curl_off_t,curl_off_t downloaded,curl_off_t,curl_off_t) {
    auto& progress=*static_cast<TransferProgress*>(context);
    if (progress.cancelled.load() || (progress.interrupted && progress.interrupted->load())) return 1;
    if (!progress.streaming) return 0;
    const auto now=handheld::milliseconds();
    if (progress.downloaded!=downloaded || now<progress.lastActivity) {
        progress.downloaded=downloaded;
        progress.lastActivity=now;
    }
    return now-progress.lastActivity>=45000?1:0;
}
}
#endif

namespace handheld {
bool boardActionAvailable(const LiveBoard& board,chess::Color player,BoardAction action) {
    if (board.finished || board.status!="started") return false;
    const auto opponent=player==chess::White?chess::Black:chess::White;
    switch (action) {
        case BoardAction::OfferDraw:
            return board.plies>=2 && !board.drawOffer[player] && !board.drawOffer[opponent];
        case BoardAction::AcceptDraw:
        case BoardAction::DeclineDraw: return board.drawOffer[opponent];
        case BoardAction::RequestTakeback:
            return !board.history.empty() && !board.takebackOffer[player] && !board.takebackOffer[opponent];
        case BoardAction::AcceptTakeback:
        case BoardAction::DeclineTakeback: return board.takebackOffer[opponent];
        case BoardAction::Resign: return true;
        default: return false;
    }
}
std::optional<uint32_t> OnlineRequestGate::begin(const LiveBoard& board,chess::Color player,
                                             chess::Move move,bool connected) {
    if (!connected || blocked() || board.finished || board.status!="started" ||
        board.position.turn!=player || serial==UINT32_MAX)
        return std::nullopt;
    const auto legal=board.position.legal();
    if (std::find(legal.begin(),legal.end(),move)==legal.end()) return std::nullopt;
    ++serial;
    delivery=PostResult::Sending;
    return serial;
}
std::optional<uint32_t> OnlineRequestGate::beginAction(const LiveBoard& board,chess::Color player,
                                                       BoardAction action,bool connected) {
    if (!connected || blocked() || serial==UINT32_MAX || !boardActionAvailable(board,player,action))
        return std::nullopt;
    ++serial;
    delivery=PostResult::Sending;
    return serial;
}
void OnlineRequestGate::complete(PostResult result) {
    if (delivery==PostResult::Sending && result!=PostResult::None && result!=PostResult::Sending)
        delivery=result;
}
std::optional<std::string> boardMovePath(std::string_view gameId,chess::Move move) {
    if (!validGameId(gameId) || move.from>=64 || move.to>=64 || move.from==move.to ||
        (move.promotion && (move.promotion<chess::Knight || move.promotion>chess::Queen))) return std::nullopt;
    std::string uci{char('a'+move.from%8),char('1'+move.from/8),
                    char('a'+move.to%8),char('1'+move.to/8)};
    if (move.promotion) uci+="  nbrq"[move.promotion];
    return "/api/board/game/"+std::string(gameId)+"/move/"+uci;
}
std::optional<std::string> boardActionPath(std::string_view gameId,BoardAction action) {
    if (!validGameId(gameId)) return std::nullopt;
    const char* suffix=nullptr;
    switch (action) {
        case BoardAction::OfferDraw:
        case BoardAction::AcceptDraw: suffix="/draw/yes"; break;
        case BoardAction::DeclineDraw: suffix="/draw/no"; break;
        case BoardAction::RequestTakeback:
        case BoardAction::AcceptTakeback: suffix="/takeback/yes"; break;
        case BoardAction::DeclineTakeback: suffix="/takeback/no"; break;
        case BoardAction::Resign: suffix="/resign"; break;
        default: return std::nullopt;
    }
    return "/api/board/game/"+std::string(gameId)+suffix;
}
PostResult postReply(long httpStatus,std::string_view body,bool transferCompleted) {
    if (httpStatus==400 || httpStatus==401 || httpStatus==403 || httpStatus==404)
        return PostResult::Rejected;
    if (httpStatus==429) return PostResult::RateLimited;
    if (transferCompleted && httpStatus==200) {
        picojson::value reply;
        if (parseObject(body,reply) && reply.get("ok").is<bool>() && reply.get("ok").get<bool>())
            return PostResult::Accepted;
    }
    return PostResult::Uncertain;
}
uint32_t LiveBoard::remainingAt(chess::Color player,uint64_t now) const {
    const uint64_t elapsed=now>=receivedAt?now-receivedAt:0;
    const auto remaining=remainingMs[player];
    if (!realtimeClock || !clockRunning || finished || position.turn!=player) return remaining;
    return elapsed>=remaining?0:uint32_t(remaining-elapsed);
}
std::string LiveBoard::resultLabel() const {
    if (!finished) return {};
    if (status=="aborted" || status=="noStart") return "Game aborted";
    if (!winner && status!="draw" && status!="stalemate" && status!="outoftime" &&
        status!="timeout" && status!="insufficientMaterialClaim") return "Game ended";
    const std::string outcome=winner?(*winner==chess::White?"White wins":"Black wins"):"Draw";
    if (status=="mate") return outcome+" / Checkmate";
    if (status=="resign") return outcome+" / Resigned";
    if (status=="outoftime") return outcome+" / On time";
    if (status=="stalemate") return outcome+" / Stalemate";
    return outcome;
}
std::string accountUsername(std::string_view json) {
    picojson::value profile;
    if (!parseObject(json,profile)) return {};
    const auto& fields=profile.get<picojson::object>();
    const auto username=fields.find("username");
    if (username==fields.end() || !username->second.is<std::string>()) return {};
    const auto& name=username->second.get<std::string>();
    if (name.empty() || name.size()>32 ||
        name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")
            !=std::string::npos) return {};
    return name;
}
std::optional<std::vector<ActiveGame>> parseActiveGames(std::string_view json) {
    picojson::value root;
    if (!parseObject(json,root)) return std::nullopt;
    const auto& entries=root.get("nowPlaying");
    if (!entries.is<picojson::array>()) return std::nullopt;
    std::vector<ActiveGame> games;
    for (const auto& entry:entries.get<picojson::array>()) {
        if (games.size()==activeGameLimit) break;
        if (!entry.is<picojson::object>()) return std::nullopt;
        const auto& opponent=entry.get("opponent");
        const auto& turn=entry.get("isMyTurn");
        if (!opponent.is<picojson::object>() || !turn.is<bool>()) return std::nullopt;
        const auto& username=opponent.get("username");
        if (!username.is<std::string>()) return std::nullopt;
        const auto& name=username.get<std::string>();
        const bool printable=!name.empty() && name.size()<=40 &&
            name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-")==
                std::string::npos;
        ActiveGame game;
        game.opponent=printable?name:"Opponent";
        game.yourTurn=turn.get<bool>();
        const auto& id=entry.get("gameId");
        if (id.is<std::string>() && validGameId(id.get<std::string>())) game.id=id.get<std::string>();
        const auto& color=entry.get("color");
        const auto& fen=entry.get("fen");
        const auto& variant=entry.get("variant");
        if (color.is<std::string>() && fen.is<std::string>() && variant.is<picojson::object>()) {
            const auto& key=variant.get("key");
            const auto& side=color.get<std::string>();
            if (key.is<std::string>() && key.get<std::string>()=="standard" &&
                (side=="white" || side=="black")) {
                game.player=side=="white"?chess::White:chess::Black;
                game.position=chess::Position::fromFen(fen.get<std::string>());
            }
        }
        games.push_back(std::move(game));
    }
    return games;
}
bool GameStreamDecoder::consume(LiveBoardReceiver receive,void* target) {
    if (line.find_first_not_of(" \t\r")==std::string::npos) return true;
    picojson::value event;
    if (!parseObject(line,event)) return false;
    const auto& type=event.get("type");
    if (!type.is<std::string>()) return false;
    const auto& kind=type.get<std::string>();
    const picojson::value* state=&event;
    if (kind=="gameFull") {
        if (started) return false;
        const auto& gameId=event.get("id");
        const auto& variant=event.get("variant");
        const auto& fen=event.get("initialFen");
        if (!gameId.is<std::string>() || gameId.get<std::string>()!=id ||
            !variant.is<picojson::object>() || !fen.is<std::string>()) return false;
        const auto& key=variant.get("key");
        if (!key.is<std::string>() || key.get<std::string>()!="standard") return false;
        const auto& start=fen.get<std::string>();
        if (start=="startpos") initial=chess::Position::start();
        else {
            const auto position=chess::Position::fromFen(start);
            if (!position) return false;
            initial=*position;
        }
        state=&event.get("state");
        if (!state->is<picojson::object>()) return false;
    } else if (!started) return false;
    else if (kind!="gameState") {
        if (kind=="chatLine" && chatReceiver) {
            const auto chat=parseChatLine(line);
            if (chat) chatReceiver(chatTarget,*chat);
        }
        return true;
    }

    const auto& moves=state->get("moves");
    const auto& status=state->get("status");
    if (!moves.is<std::string>() || !status.is<std::string>() ||
        !knownStatus(status.get<std::string>())) return false;
    const auto& sequence=moves.get<std::string>();
    std::string_view remaining=sequence;
    LiveBoard next;
    next.position=initial;
    next.initialPosition=initial;
    next.initialMs=board.initialMs;
    next.realtimeClock=board.realtimeClock;
    if (kind=="gameFull") {
        const auto& clock=event.get("clock");
        if (!clock.is<picojson::null>()) {
            uint32_t initialTime=0, increment=0;
            if (!clock.is<picojson::object>() || !millisecondsField(clock.get("initial"),initialTime) ||
                !millisecondsField(clock.get("increment"),increment)) return false;
            next.realtimeClock=true;
            next.initialMs=initialTime;
        }
    }
    if (started && remaining.substr(0,appliedMoves.size())==appliedMoves &&
        (remaining.size()==appliedMoves.size() || appliedMoves.empty() || remaining[appliedMoves.size()]==' ')) {
        next=board;
        remaining.remove_prefix(appliedMoves.size());
        if (!remaining.empty() && !appliedMoves.empty()) remaining.remove_prefix(1);
    }
    while (!remaining.empty()) {
        const auto split=remaining.find(' ');
        chess::Move move;
        if (!applyUci(next.position,remaining.substr(0,split),move)) return false;
        next.lastMove=move;
        ++next.plies;
        next.history.push_back(move);
        if (split==std::string_view::npos) break;
        remaining.remove_prefix(split+1);
        if (remaining.empty()) return false;
    }
    const auto& gameStatus=status.get<std::string>();
    next.finished=gameStatus!="started" && gameStatus!="created";
    next.status=gameStatus;
    if (!millisecondsField(state->get("wtime"),next.remainingMs[chess::White]) ||
        !millisecondsField(state->get("btime"),next.remainingMs[chess::Black]) ||
        !millisecondsField(state->get("winc"),next.incrementMs[chess::White]) ||
        !millisecondsField(state->get("binc"),next.incrementMs[chess::Black])) return false;
    const auto& fields=state->get<picojson::object>();
    const char* offerFields[]={"wdraw","bdraw","wtakeback","btakeback"};
    for (unsigned n=0;n<4;++n) {
        const auto found=fields.find(offerFields[n]);
        if (found!=fields.end() && !found->second.is<bool>()) return false;
        const bool offered=found!=fields.end() && found->second.get<bool>();
        (n<2?next.drawOffer[n]:next.takebackOffer[n-2])=offered;
    }
    next.winner.reset();
    const auto& winner=state->get("winner");
    if (!winner.is<picojson::null>()) {
        if (!next.finished || !winner.is<std::string>()) return false;
        const auto& color=winner.get<std::string>();
        if (color!="white" && color!="black") return false;
        next.winner=color=="white"?chess::White:chess::Black;
    }
    // Casual games wait for both first moves. Board API has no clock-running flag.
    next.clockRunning=next.realtimeClock && gameStatus=="started" && next.plies>=2;
    board=next;
    appliedMoves=sequence;
    started=true;
    receive(target,board);
    return true;
}
bool GameStreamDecoder::feed(std::string_view bytes,LiveBoardReceiver receive,void* target) {
    while (!bytes.empty()) {
        const auto newline=bytes.find('\n');
        const auto piece=bytes.substr(0,newline);
        if (piece.size()>maxStreamBytes-line.size()) return false;
        line.append(piece);
        if (newline==std::string_view::npos) break;
        if (!consume(receive,target)) return false;
        line.clear();
        bytes.remove_prefix(newline+1);
    }
    return true;
}
bool accountHttpsAvailable() {
#ifdef HANDHELD_CURL
    static const bool initialized=curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK;
    return initialized;
#else
    return false;
#endif
}
static AccountCheck lichessGet(const char* url,const std::string& token,const char* caPath,
                               const std::atomic<bool>& cancelled,WriteBody writer,void* body,
                               bool streaming=false,const std::atomic<bool>* interrupted=nullptr,
                               bool post=false,long* responseCode=nullptr,const char* postBody="") {
#ifdef HANDHELD_CURL
    if (!caPath || cancelled.load() || !accountHttpsAvailable())
        return AccountCheck::Failed;
    auto* curl=curl_easy_init();
    long status=0;
    CURLcode result=CURLE_FAILED_INIT;
    TransferProgress progress{cancelled,interrupted,streaming,milliseconds()};
    if (curl) {
        const auto authorization="Authorization: Bearer "+token;
        auto* headers=curl_slist_append(nullptr,authorization.c_str());
        if (headers) {
            curl_easy_setopt(curl,CURLOPT_URL,url);
            curl_easy_setopt(curl,CURLOPT_HTTPHEADER,headers);
            curl_easy_setopt(curl,CURLOPT_CAINFO,caPath);
            curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);
            curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
            curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,0L);
            curl_easy_setopt(curl,CURLOPT_FAILONERROR,1L);
            if (post) {
                curl_easy_setopt(curl,CURLOPT_POST,1L);
                curl_easy_setopt(curl,CURLOPT_POSTFIELDS,postBody);
                curl_easy_setopt(curl,CURLOPT_POSTFIELDSIZE,long(std::strlen(postBody)));
            }
            curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT_MS,5000L);
            curl_easy_setopt(curl,CURLOPT_TIMEOUT_MS,streaming?0L:10000L);
            curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
            curl_easy_setopt(curl,CURLOPT_USERAGENT,"LichessHandheld/0.1");
            curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,writer);
            curl_easy_setopt(curl,CURLOPT_WRITEDATA,body);
            curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);
            curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,stopTransfer);
            curl_easy_setopt(curl,CURLOPT_XFERINFODATA,&progress);
            result=curl_easy_perform(curl);
            curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
        }
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
    if (responseCode) *responseCode=status;
    if (result==CURLE_OK && status==200) return AccountCheck::Verified;
    if (status==401 || status==403) return AccountCheck::Rejected;
    if (status==429) return AccountCheck::RateLimited;
#else
    (void)token;
    (void)caPath;
    (void)cancelled;
    (void)url;
    (void)body;
    (void)writer;
    (void)streaming;
    (void)interrupted;
    (void)post;
    (void)responseCode;
    (void)postBody;
#endif
    return AccountCheck::Failed;
}
AccountProfile verifyLichessAccount(const std::string& token,const char* caPath,
                                    const std::atomic<bool>& cancelled) {
    std::string body;
    const auto status=lichessGet("https://lichess.org/api/account",token,caPath,cancelled,readBody,&body);
    if (status!=AccountCheck::Verified) return {status,{}};
    auto username=accountUsername(body);
    if (username.empty()) return {AccountCheck::InvalidReply,{}};
    return {status,std::move(username)};
}
ActiveGames fetchActiveGames(const std::string& token,const char* caPath,
                             const std::atomic<bool>& cancelled) {
    std::string body;
    const auto status=lichessGet("https://lichess.org/api/account/playing?nb=5",token,caPath,cancelled,
                                  readBody,&body);
    if (status!=AccountCheck::Verified) return {status,{}};
    auto games=parseActiveGames(body);
    if (!games) return {AccountCheck::InvalidReply,{}};
    return {status,std::move(*games)};
}
AccountCheck streamGameBoard(const std::string& gameId,const std::string& token,const char* caPath,
                             const std::atomic<bool>& cancelled,LiveBoardReceiver receive,void* target,
                             const std::atomic<bool>* interrupted,ChatReceiver chatReceiver,void* chatTarget) {
    if (!validGameId(gameId)) return AccountCheck::InvalidReply;
    const auto url="https://lichess.org/api/board/game/stream/"+gameId;
    StreamBody stream{GameStreamDecoder(gameId,chatReceiver,chatTarget),receive,target};
    const auto status=lichessGet(url.c_str(),token,caPath,cancelled,readStream,&stream,true,interrupted);
    if (stream.invalid) return AccountCheck::InvalidReply;
    if (status==AccountCheck::Verified && !stream.decoder.complete()) return AccountCheck::Failed;
    return status;
}
static PostResult postBoardPath(const std::optional<std::string>& path,const std::string& token,
                                const char* caPath,const std::atomic<bool>& cancelled) {
    if (!path || cancelled.load() || !accountHttpsAvailable()) return PostResult::Rejected;
    const auto url="https://lichess.org"+*path;
    std::string body;
    long code=0;
    const auto result=lichessGet(url.c_str(),token,caPath,cancelled,readBody,&body,false,nullptr,true,&code);
    return postReply(code,body,result==AccountCheck::Verified);
}
PostResult postBoardMove(const std::string& gameId,chess::Move move,const std::string& token,
                         const char* caPath,const std::atomic<bool>& cancelled) {
    return postBoardPath(boardMovePath(gameId,move),token,caPath,cancelled);
}
PostResult postBoardAction(const std::string& gameId,BoardAction action,const std::string& token,
                           const char* caPath,const std::atomic<bool>& cancelled) {
    return postBoardPath(boardActionPath(gameId,action),token,caPath,cancelled);
}
ChatHistory fetchPlayerChat(const std::string& id,const std::string& token,const char* caPath,
                             const std::atomic<bool>& cancelled) {
    ChatHistory reply;
    if (!validGameId(id)) { reply.status=AccountCheck::InvalidReply; return reply; }
    std::string body;
    reply.status=lichessGet(("https://lichess.org/api/board/game/"+id+"/chat").c_str(),token,caPath,cancelled,readBody,&body);
    if (reply.status!=AccountCheck::Verified) return reply;
    const auto history=parseChatHistory(body);
    if (!history) { reply.status=AccountCheck::InvalidReply; return reply; }
    return *history;
}
PostResult sendPlayerChat(const std::string& id,const std::string& text,const std::string& token,
                          const char* caPath,const std::atomic<bool>& cancelled) {
    if (!validGameId(id) || text.empty() || text.size()>140 || safeText(text)!=text) return PostResult::Rejected;
    std::string body;
    const auto encoded="room=player&text="+formEncode(text);
    long code=0;
    const auto result=lichessGet(("https://lichess.org/api/board/game/"+id+"/chat").c_str(),token,caPath,
        cancelled,readBody,&body,false,nullptr,true,&code,encoded.c_str());
    return postReply(code,body,result==AccountCheck::Verified);
}
namespace {
struct AccountStream { AccountEventDecoder decoder; AccountEventReceiver receive; void* target; bool valid=true; };
size_t accountBytes(char* bytes,size_t size,size_t count,void* context) {
    auto& stream=*static_cast<AccountStream*>(context);
    stream.valid=stream.decoder.feed({bytes,size*count},stream.receive,stream.target);
    return stream.valid?size*count:0;
}
}
AccountCheck streamAccountEvents(const std::string& token,const char* caPath,const std::atomic<bool>& cancelled,
                                 AccountEventReceiver receive,void* target) {
    AccountStream stream{{},receive,target,true};
    const auto status=lichessGet("https://lichess.org/api/stream/event",token,caPath,cancelled,accountBytes,&stream,true);
    return stream.valid?status:AccountCheck::InvalidReply;
}
namespace {
struct FollowingStream { FriendsDecoder decoder; bool valid=true; };
size_t followingBytes(char* bytes,size_t size,size_t count,void* context) {
    auto& stream=*static_cast<FollowingStream*>(context);
    stream.valid=stream.decoder.feed({bytes,size*count});
    return stream.valid?size*count:0;
}
FriendsList fetchFriends(const std::string& token,const char* caPath,const std::atomic<bool>& cancelled) {
    FollowingStream stream;
    const auto status=lichessGet("https://lichess.org/api/rel/following",token,caPath,cancelled,followingBytes,&stream);
    if (!stream.valid) return {AccountCheck::InvalidReply,{}};
    if (status!=AccountCheck::Verified) return {status,{}};
    auto list=stream.decoder.finish();
    for (size_t first=0;first<list.players.size() && !cancelled.load();first+=100) {
        const size_t last=std::min(first+100,list.players.size());
        std::string url="https://lichess.org/api/users/status?ids=";
        for (size_t i=first;i<last;++i) {
            if (i!=first) url+=',';
            url+=list.players[i].id;
        }
        std::string body;
        const auto presence=lichessGet(url.c_str(),token,caPath,cancelled,readBody,&body);
        if (presence==AccountCheck::RateLimited) { list.rateLimited=true; break; }
        if (presence!=AccountCheck::Verified || !applyPresence(body,list.players,first,last)) break;
    }
    return list;
}
}
FriendReply requestFriends(const FriendRequest& request,const std::string& token,const char* caPath,
                           const std::atomic<bool>& cancelled) {
    FriendReply reply;
    if (request.action!=FriendAction::Refresh) {
        const auto path=friendMutationPath(request);
        if (!path) { reply.mutation=AccountCheck::InvalidReply; return reply; }
        std::string body;
        const auto url="https://lichess.org"+*path;
        long code=0;
        reply.mutation=lichessGet(url.c_str(),token,caPath,cancelled,readBody,&body,false,nullptr,true,&code);
        reply.targetUnavailable=code==404;
        if (reply.mutation==AccountCheck::Rejected || reply.mutation==AccountCheck::RateLimited)
            return reply;
    }
    if (!cancelled.load()) reply.list=fetchFriends(token,caPath,cancelled);
    return reply;
}
LobbyReply sendLobbyRequest(const LobbyRequest& request,const std::string& token,const char* caPath,
                            const std::atomic<bool>& cancelled) {
    const auto data=lobbyRequest(request);
    if (!data) return {AccountCheck::InvalidReply,{}};
    std::string body;
    const auto status=lichessGet(("https://lichess.org"+data->first).c_str(),token,caPath,cancelled,readBody,&body,
        request.action==LobbyAction::Seek,nullptr,true,nullptr,data->second.c_str());
    LobbyReply reply{status,{}};
    if (status==AccountCheck::Verified && request.action==LobbyAction::Challenge) {
        picojson::value root;
        if (!parseObject(body,root)) return {AccountCheck::InvalidReply,{}};
        const auto& challenge=root.get("challenge").is<picojson::object>()?root.get("challenge"):root;
        if (!challenge.get("id").is<std::string>()) return {AccountCheck::InvalidReply,{}};
        reply.challengeId=challenge.get("id").get<std::string>();
        if (!validGameId(reply.challengeId)) return {AccountCheck::InvalidReply,{}};
    }
    return reply;
}
}
