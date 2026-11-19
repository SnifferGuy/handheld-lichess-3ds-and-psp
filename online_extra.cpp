#include "online_extra.hpp"
#include "service_json.hpp"
#include <algorithm>
namespace handheld {
std::string safeText(std::string_view text,size_t limit) {
    std::string out;
    while (!text.empty() && out.size()<limit) {
        const unsigned char c=text.front(); text.remove_prefix(1);
        if (c>=32 && c<127) out+=char(c);
        else if (c=='\n' || c=='\r' || c=='\t') out+=' ';
        else {
            unsigned n=c>=0xf0 && c<=0xf4?3:c>=0xe0 && c<=0xef?2:c>=0xc2 && c<=0xdf?1:0;
            while (n-- && !text.empty() && (static_cast<unsigned char>(text.front())&0xc0)==0x80) text.remove_prefix(1);
            out+='?';
        }
    }
    return out;
}
std::string formEncode(std::string_view value) {
    std::string out;
    for (unsigned char c:value) {
        if ((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='-' || c=='_' || c=='.') out+=char(c);
        else { out+='%'; out+="0123456789ABCDEF"[c>>4]; out+="0123456789ABCDEF"[c&15]; }
    }
    return out;
}
std::optional<ChatMessage> parseChatLine(std::string_view json) {
    picojson::value root;
    if (!serviceJson(json,root) || !root.is<picojson::object>()) return std::nullopt;
    const auto& room=root.get("room"), user=root.get("username"), text=root.get("text");
    if (!room.is<std::string>() || room.get<std::string>()!="player" || !user.is<std::string>() || !text.is<std::string>()) return std::nullopt;
    return ChatMessage{safeText(user.get<std::string>(),40),safeText(text.get<std::string>())};
}
std::optional<ChatHistory> parseChatHistory(std::string_view json) {
    picojson::value root;
    if (!serviceJson(json,root) || !root.is<picojson::array>()) return std::nullopt;
    ChatHistory history;
    const auto& rows=root.get<picojson::array>();
    history.total=rows.size(); history.status=AccountCheck::Verified;
    for (size_t n=rows.size()>100?rows.size()-100:0;n<rows.size();++n) {
        const auto& row=rows[n];
        if (!row.is<picojson::object>() || !row.get("user").is<std::string>() || !row.get("text").is<std::string>()) return std::nullopt;
        history.messages.push_back({safeText(row.get("user").get<std::string>(),40),safeText(row.get("text").get<std::string>())});
    }
    return history;
}
void ChatState::receive(ChatMessage message) {
    if (messages.size()==100) messages.pop_front();
    messages.push_back(std::move(message)); ++observedTotal;
    if (focus==ChatFocus::Closed && !hideNotifications) unread=std::min(99u,unread+1);
}
void ChatState::replace(ChatHistory history) {
    fetching=false;
    if (history.status!=AccountCheck::Verified) { restricted=history.status==AccountCheck::Rejected; refreshNeeded=!restricted; return; }
    restricted=false;
    if (loaded && focus==ChatFocus::Closed && !hideNotifications && history.total>observedTotal)
        unread=unsigned(std::min<size_t>(99,unread+history.total-observedTotal));
    messages.assign(history.messages.begin(),history.messages.end());
    observedTotal=history.total; loaded=true; refreshNeeded=false;
}
bool ChatState::beginSend() {
    if (restricted || delivery==PostResult::Sending || draft.empty() || draft.size()>140 || safeText(draft)!=draft) return false;
    submitted=draft; delivery=PostResult::Sending; return true;
}
void ChatState::completeSend(PostResult result) {
    delivery=result;
    if (result==PostResult::Accepted && draft==submitted) draft.clear();
    submitted.clear();
}
void ChatInbox::push(ChatEvent event) {
    const auto w=write.load(std::memory_order_relaxed);
    const auto next=unsigned((w+1)%entries.size());
    if (next==read.load(std::memory_order_acquire)) { overflow.store(true); return; }
    entries[w]=std::move(event); write.store(unsigned(next),std::memory_order_release);
}
bool ChatInbox::pop(ChatEvent& event) {
    const auto r=read.load(std::memory_order_relaxed);
    if (r==write.load(std::memory_order_acquire)) return false;
    event=std::move(entries[r]); read.store((r+1)%entries.size(),std::memory_order_release); return true;
}
char TextEditor::character(int index) const {
    constexpr std::string_view letters="qwertyuiopasdfghjkl;zxcvbnm,./0123456789";
    constexpr std::string_view punctuation="@#$%&*()-_+=:;/\\\"'[]{}<>~`^|.,!?0123456789";
    if (index<0 || index>=40) return 0;
    char c=(symbols?punctuation:letters)[size_t(index)];
    if (upper && c>='a' && c<='z') c=char(c-'a'+'A');
    return c;
}
void TextEditor::insert(std::string& value,size_t limit) const {
    if (selected<40 && value.size()<limit) value+=character(selected);
    else if (selected==42 && value.size()<limit) value+=' ';
    else if (selected==43 && !value.empty()) value.pop_back();
}
bool validUsername(std::string_view name) {
    return !name.empty() && name.size()<=32 && name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string_view::npos;
}
bool AccountEventDecoder::feed(std::string_view bytes,AccountEventReceiver receive,void* target) {
    if (!ready) { ready=true; receive(target,AccountEvent{}); }
    while (!bytes.empty()) {
        const auto split=bytes.find('\n'); const auto part=bytes.substr(0,split);
        if (line.size()+part.size()>32768) return false;
        line.append(part);
        if (split==std::string_view::npos) break;
        bytes.remove_prefix(split+1);
        if (line.find_first_not_of(" \r\t")!=std::string::npos) {
            picojson::value root;
            if (!serviceJson(line,root) || !root.is<picojson::object>() || !root.get("type").is<std::string>()) return false;
            const auto& kind=root.get("type").get<std::string>();
            AccountEvent event;
            if (kind=="gameStart") {
                const auto& game=root.get("game");
                if (!game.is<picojson::object>() || !game.get("color").is<std::string>()) return false;
                const auto& variant=game.get("variant");
                const auto& compat=game.get("compat");
                if ((variant.is<picojson::object>() && variant.get("key").is<std::string>() && variant.get("key").get<std::string>()!="standard") ||
                    (compat.is<picojson::object>() && compat.get("board").is<bool>() && !compat.get("board").get<bool>())) { line.clear(); continue; }
                const auto& idValue=game.get("gameId").is<std::string>()?game.get("gameId"):game.get("id");
                if (!idValue.is<std::string>()) return false;
                const auto& id=idValue.get<std::string>();
                if (!boardMovePath(id,{12,28,0})) return false;
                const auto& color=game.get("color").get<std::string>();
                if (color!="white" && color!="black") return false;
                event.kind=AccountEventKind::GameStart; event.game.id=id;
                event.game.player=color=="white"?chess::White:chess::Black;
                event.game.position=chess::Position::start(); event.game.opponent="Opponent";
                const auto& opponent=game.get("opponent");
                if (opponent.is<picojson::object>() && opponent.get("username").is<std::string>()) event.game.opponent=safeText(opponent.get("username").get<std::string>(),40);
                receive(target,event);
            } else if (kind=="challenge" || kind=="challengeCanceled" || kind=="challengeDeclined") {
                const auto& challenge=root.get("challenge");
                if (!challenge.is<picojson::object>() || !challenge.get("id").is<std::string>()) return false;
                const auto& id=challenge.get("id").get<std::string>();
                if (!boardMovePath(id,{12,28,0})) return false;
                event.challenge.id=id;
                event.kind=kind=="challenge"?AccountEventKind::Challenge:AccountEventKind::ChallengeRemoved;
                if (kind=="challenge") {
                    const auto& compat=root.get("compat");
                    if (compat.is<picojson::object>() && compat.get("board").is<bool>() && !compat.get("board").get<bool>()) { line.clear(); continue; }
                    const auto& variant=challenge.get("variant");
                    if (!variant.is<picojson::object>() || !variant.get("key").is<std::string>() || variant.get("key").get<std::string>()!="standard") { line.clear(); continue; }
                    const auto& challenger=challenge.get("challenger");
                    if (!challenger.is<picojson::object>() || !challenger.get("name").is<std::string>()) return false;
                    event.challenge.user=safeText(challenger.get("name").get<std::string>(),40);
                    const auto& direction=challenge.get("direction");
                    event.challenge.incoming=!direction.is<std::string>() || direction.get<std::string>()!="out";
                }
                receive(target,event);
            }
        }
        line.clear();
    }
    return true;
}
std::string usernameKey(std::string_view name) {
    std::string key(name);
    for (auto& c:key) if (c>='A' && c<='Z') c=char(c-'A'+'a');
    return key;
}
bool FriendsDecoder::consume() {
    if (line.find_first_not_of(" \t\r")==std::string::npos) return true;
    picojson::value player;
    if (!serviceJson(line,player) || !player.is<picojson::object>() ||
        !player.get("id").is<std::string>() || !player.get("username").is<std::string>()) return false;
    const auto id=usernameKey(player.get("id").get<std::string>());
    const auto& name=player.get("username").get<std::string>();
    if (!validUsername(id) || usernameKey(name)!=id) return false;
    if (std::any_of(players.begin(),players.end(),[&](const Friend& p){return p.id==id;})) return true;
    if (players.size()==1000) { truncated=true; return true; }
    players.push_back({id,name});
    return true;
}
bool FriendsDecoder::feed(std::string_view bytes) {
    while (!bytes.empty()) {
        const auto newline=bytes.find('\n');
        const auto piece=bytes.substr(0,newline);
        if (piece.size()>32768-line.size()) return false;
        line.append(piece);
        if (newline==std::string_view::npos) break;
        if (!consume()) return false;
        line.clear();
        bytes.remove_prefix(newline+1);
    }
    return true;
}
FriendsList FriendsDecoder::finish() {
    if (!consume()) return {AccountCheck::InvalidReply,{}};
    std::sort(players.begin(),players.end(),[](const Friend& a,const Friend& b){return a.id<b.id;});
    return {AccountCheck::Verified,std::move(players),truncated};
}
bool applyPresence(std::string_view body,std::vector<Friend>& players,size_t first,size_t last) {
    picojson::value root;
    if (!serviceJson(body,root) || !root.is<picojson::array>()) return false;
    const auto& statuses=root.get<picojson::array>();
    if (statuses.size()>100 || first>last || last>players.size()) return false;
    for (const auto& status:statuses) {
        if (!status.is<picojson::object>() || !status.get("id").is<std::string>()) return false;
        for (const char* flag:{"online","playing"})
            if (!status.get(flag).is<picojson::null>() && !status.get(flag).is<bool>()) return false;
    }
    for (const auto& status:statuses) {
        const auto id=usernameKey(status.get("id").get<std::string>());
        for (size_t i=first;i<last;++i) {
            if (players[i].id!=id) continue;
            const auto& playing=status.get("playing");
            const auto& online=status.get("online");
            players[i].presence=playing.is<bool>() && playing.get<bool>()?Presence::Playing:
                online.is<bool>() && online.get<bool>()?Presence::Online:Presence::Offline;
            break;
        }
    }
    return true;
}
std::optional<std::string> friendMutationPath(const FriendRequest& request) {
    if (request.action==FriendAction::Refresh || !validUsername(request.username)) return std::nullopt;
    return std::string(request.action==FriendAction::Follow?"/api/rel/follow/":"/api/rel/unfollow/")+usernameKey(request.username);
}
std::optional<std::pair<std::string,std::string>> lobbyRequest(const LobbyRequest& request) {
    std::string path,body;
    if (request.action==LobbyAction::Seek || request.action==LobbyAction::Challenge) {
        if (!((request.minutes==15 && request.increment==10) || (request.minutes==10 && !request.increment) || (request.minutes==30 && !request.increment)) || request.color<-1 || request.color>1) return std::nullopt;
        body="rated=false&variant=standard";
        if (request.color>=0) body+=std::string("&color=")+(request.color==0?"white":"black");
        if (request.action==LobbyAction::Seek) {
            path="/api/board/seek"; body+="&time="+std::to_string(request.minutes)+"&increment="+std::to_string(request.increment);
        } else {
            if (!validUsername(request.target)) return std::nullopt;
            path="/api/challenge/"+request.target;
            body+="&clock.limit="+std::to_string(request.minutes*60)+"&clock.increment="+std::to_string(request.increment);
        }
    } else {
        if (!boardMovePath(request.target,{12,28,0})) return std::nullopt;
        path="/api/challenge/"+request.target+(request.action==LobbyAction::Accept?"/accept":request.action==LobbyAction::Decline?"/decline":"/cancel");
    }
    return std::make_pair(path,body);
}
}
