#pragma once
#include "lichess_account.hpp"
#include <array>
#include <deque>
namespace handheld {
std::string safeText(std::string_view,size_t limit=140);
std::string formEncode(std::string_view);
struct ChatMessage {
    std::string user,text;
    bool operator==(const ChatMessage& other) const { return user==other.user && text==other.text; }
};
struct ChatHistory { AccountCheck status=AccountCheck::Failed; std::vector<ChatMessage> messages; size_t total=0; };
std::optional<ChatMessage> parseChatLine(std::string_view);
std::optional<ChatHistory> parseChatHistory(std::string_view);
enum class ChatFocus { Closed, Transcript, Composer, Phrases };
struct ChatState {
    std::deque<ChatMessage> messages;
    std::string draft, submitted;
    ChatFocus focus=ChatFocus::Closed;
    PostResult delivery=PostResult::None;
    unsigned unread=0;
    bool hideNotifications=false, loaded=false, fetching=false, refreshNeeded=false, restricted=false;
    size_t observedTotal=0;
    void receive(ChatMessage);
    void replace(ChatHistory);
    bool beginSend();
    void completeSend(PostResult);
};
constexpr const char* quickPhrases[]={"Good luck","Have fun","Good game","Thanks","Well played"};
// One network producer and one UI consumer. Full queues request an explicit history refresh.
struct ChatEvent { enum Kind { Line, History, Delivery } kind=Line; ChatMessage message; ChatHistory history; PostResult delivery=PostResult::None; };
class ChatInbox {
    std::vector<ChatEvent> entries=std::vector<ChatEvent>(128);
    std::atomic<unsigned> read{0},write{0};
public:
    std::atomic<bool> overflow{false};
    void push(ChatEvent);
    bool pop(ChatEvent&);
};
struct TextEditor {
    int selected=0;
    bool upper=false, symbols=false;
    char character(int) const;
    void insert(std::string&,size_t limit=140) const;
};
struct Challenge { std::string id,user; bool incoming=true; };
enum class AccountEventKind { Ready, GameStart, Challenge, ChallengeRemoved };
struct AccountEvent { AccountEventKind kind=AccountEventKind::Ready; ActiveGame game; Challenge challenge; };
using AccountEventReceiver=void (*)(void*,const AccountEvent&);
class AccountEventDecoder {
    std::string line;
    bool ready=false;
public:
    bool feed(std::string_view,AccountEventReceiver,void*);
};
bool validUsername(std::string_view);
std::string usernameKey(std::string_view);
enum class Presence { Unknown, Offline, Online, Playing };
struct Friend { std::string id,username; Presence presence=Presence::Unknown; };
struct FriendsList {
    AccountCheck status=AccountCheck::Failed;
    std::vector<Friend> players;
    bool truncated=false, rateLimited=false;
};
class FriendsDecoder {
    std::string line;
    std::vector<Friend> players;
    bool truncated=false;
    bool consume();
public:
    bool feed(std::string_view);
    FriendsList finish();
};
bool applyPresence(std::string_view,std::vector<Friend>&,size_t first,size_t last);
enum class FriendAction { Refresh, Follow, Unfollow };
struct FriendRequest { FriendAction action=FriendAction::Refresh; std::string username; };
struct FriendReply { AccountCheck mutation=AccountCheck::Verified; FriendsList list; bool targetUnavailable=false; };
std::optional<std::string> friendMutationPath(const FriendRequest&);
FriendReply requestFriends(const FriendRequest&,const std::string&,const char*,const std::atomic<bool>&);
struct LobbyReply { AccountCheck status=AccountCheck::Failed; std::string challengeId; };
enum class LobbyAction { Seek, Challenge, Accept, Decline, Cancel };
struct LobbyRequest { LobbyAction action=LobbyAction::Seek; std::string target; unsigned minutes=15,increment=10; int color=-1; };
std::optional<std::pair<std::string,std::string>> lobbyRequest(const LobbyRequest&);
ChatHistory fetchPlayerChat(const std::string&,const std::string&,const char*,const std::atomic<bool>&);
PostResult sendPlayerChat(const std::string&,const std::string&,const std::string&,const char*,const std::atomic<bool>&);
AccountCheck streamAccountEvents(const std::string&,const char*,const std::atomic<bool>&,AccountEventReceiver,void*);
LobbyReply sendLobbyRequest(const LobbyRequest&,const std::string&,const char*,const std::atomic<bool>&);
struct ExtraService {
    decltype(&fetchPlayerChat) history=fetchPlayerChat;
    decltype(&sendPlayerChat) send=sendPlayerChat;
    decltype(&streamAccountEvents) events=streamAccountEvents;
    decltype(&sendLobbyRequest) lobby=sendLobbyRequest;
    decltype(&requestFriends) friends=requestFriends;
};
}
