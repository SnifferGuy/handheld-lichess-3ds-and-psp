#include "app.hpp"
#include "credential.hpp"
#include "screen.hpp"
#include <algorithm>
#include <new>

using namespace chess;
namespace handheld {
namespace {
constexpr char appTitle[]="Lichess Handheld";
constexpr int menuTop=50, menuRow=29;
constexpr int activeFooter=198;
constexpr int friendsView=4, addFriendView=5, friendActionsView=6, unfollowView=7;
constexpr int friendsLobbyOption=6, friendControls=4;
constexpr int findOpponentOption=1;
constexpr int controlsTop(bool dual) { return dual?104:132; }
constexpr int controlsRow(bool dual) { return dual?29:20; }
constexpr int visibleActions(bool dual) { return dual?4:6; }
int firstVisible(int selected,int rows,int count) {
    return std::clamp(selected-rows+1,0,std::max(0,count-rows));
}
constexpr int promotions[]={Queen,Rook,Bishop,Knight};
constexpr uint32_t clockMinutes[]={15,10,30,0};
constexpr const char* clockNames[]={"15+10","10+0","30+0","Untimed"};
constexpr const char* levels[]={"Easy","Normal","Hard"};
constexpr const char* names[]={"Queen","Rook","Bishop","Knight"};
constexpr const char* resultOptions[]={"Rematch","New opponent","Analysis board"};
constexpr int resultTop=132, resultRow=34;
const char* name(Color c) { return c==White?"White":"Black"; }
bool pressed(Input i,Key k) { return (i.keys&k)!=0; }
void cursorMove(int& item,int count,Input i) {
    if (pressed(i,Up)) item=(item+count-1)%count;
    if (pressed(i,Down)) item=(item+1)%count;
}
void outline(int x,int y,int w,int h,Ink ink) {
    rect(x,y,w,2,ink); rect(x,y+h-2,w,2,ink);
    rect(x,y,2,h,ink); rect(x+w-2,y,2,h,ink);
}
void captureRing(int centerX,int centerY,int radius,Ink ink) {
    const int inner=(radius-2)*(radius-2);
    const int outer=radius*radius;
    for (int y=-radius;y<=radius;++y) {
        int edge=radius;
        while (edge*edge+y*y>outer) --edge;
        int gap=0;
        while (gap*gap+y*y<inner) ++gap;
        if (!gap) rect(centerX-edge,centerY+y,edge*2+1,1,ink);
        else {
            rect(centerX-edge,centerY+y,edge-gap+1,1,ink);
            rect(centerX+gap,centerY+y,edge-gap+1,1,ink);
        }
    }
}
void button(int x,int y,int w,int h,std::string_view label,const Theme& theme,
            bool focused,bool mainAction=false) {
    roundedRect(x,y,w,h,focused?theme.accent:theme.border);
    roundedRect(x+1,y+1,w-2,h-2,mainAction?primary:focused?theme.focus:theme.surface);
    text(x+10,y+(h-14)/2,label,mainAction?primaryText:theme.text,2);
}
}
void drawLivePlayerPanels(const ActiveGame& game,const LiveBoard* board,ThemeId theme,bool dual,uint64_t now,int top) {
    const auto& t=palette(theme);
    const bool finished=board && board->finished;
    const auto turn=board?board->position.turn:game.yourTurn?game.player:opposite(game.player);
    for (int n=0;n<2;++n) {
        const auto player=n?opposite(game.player):game.player;
        const bool active=!finished && turn==player;
        const auto seconds=board?(uint64_t(board->remainingAt(player,now))+999)/1000:0;
        const bool timed=board && board->realtimeClock;
        const bool urgent=timed && active && seconds<=30;
        const std::string clock=!board || !timed?"--:--":
            std::to_string(seconds/60)+":"+(seconds%60<10?"0":"")+std::to_string(seconds%60);
        const auto digits=(urgent?"! ":"")+clock;
        const int x=16+(dual?n*147:0), y=top+(dual?0:n*44), w=dual?141:192, h=dual?60:40;
        const auto accent=finished?t.secondary:active?t.active:t.waiting;
        roundedRect(x,y,w,h,t.surface);
        rect(x+2,y+4,urgent?4:2,h-8,accent);
        setTextRight(x+w-8);
        const int colorX=x+w-8-textWidth(name(player));
        text(colorX,y+3,name(player),t.secondary);
        setTextRight(colorX-7);
        text(x+9,y+3,n?game.opponent:std::string("You"),t.text);
        setTextRight(x+w-8);
        const int clockX=dual?x+9:x+w-8-textWidth(digits,4);
        text(clockX,y+(dual?18:14),digits,accent,4);
        if (!dual) setTextRight(clockX-5);
        text(x+9,y+(dual?43:26),finished?"Finished":active?(n?"Their turn":"Your turn"):"Waiting",accent);
    }
    setTextRight(dual?304:208);
}
Match& App::match() { return onlineAnalysis?completedOnline:lan.role==Lan::Off?solo:lan.match; }
const Match& App::match() const { return onlineAnalysis?completedOnline:lan.role==Lan::Off?solo:lan.match; }
Color App::side() const { return onlineAnalysis?analysisColor:lan.role==Lan::Off?color:lan.side(); }
bool App::canMove() const {
    return match().game.result==Result::Playing && side()==match().game.pos.turn &&
        (lan.role==Lan::Off || lan.acceptingInput());
}
bool App::onlineConnected() const {
    return page==LichessPreview && checkingAccount &&
        streamSynchronized && liveBoard && !liveBoard->finished && !onlineRequest.blocked() &&
        liveBoard->status=="started";
}
bool App::canPlayOnline() const {
    return onlineConnected() && !done.load(std::memory_order_acquire) &&
        liveBoard->position.turn==accountGames.games[previewIndex].player;
}
void App::submitOnlineMove(Move move) {
    submitOnlineAction(BoardAction::Move,move);
}
void App::submitOnlineAction(BoardAction action,Move move) {
    if (!onlineConnected() || done.load(std::memory_order_acquire)) return;
    auto* queued=new(std::nothrow) PendingRequest{move,0,action};
    if (!queued) { notice="Could not queue this request."; return; }
    const auto player=accountGames.games[previewIndex].player;
    const auto request=action==BoardAction::Move?onlineRequest.begin(*liveBoard,player,move,true):
        onlineRequest.beginAction(*liveBoard,player,action,true);
    if (!request) { delete queued; return; }
    queued->id=*request;
    pendingRequest.store(queued);
    interruptStream.store(true);
    clearSelection();
    onlineMenu=false;
    notice="Sending request...";
}
std::vector<std::pair<std::string,BoardAction>> App::onlineActionList() const {
    std::vector<std::pair<std::string,BoardAction>> rows;
    if (!onlineConnected()) return rows;
    const auto player=accountGames.games[previewIndex].player;
    const std::pair<const char*,BoardAction> choices[]={
        {"Offer draw",BoardAction::OfferDraw},{"Accept draw",BoardAction::AcceptDraw},
        {"Decline draw",BoardAction::DeclineDraw},{"Request takeback",BoardAction::RequestTakeback},
        {"Accept takeback",BoardAction::AcceptTakeback},{"Decline takeback",BoardAction::DeclineTakeback},
        {"Resign",BoardAction::Resign}};
    for (const auto& row:choices)
        if (boardActionAvailable(*liveBoard,player,row.second)) rows.emplace_back(row.first,row.second);
    return rows;
}
App::~App() { leave(); }
void App::clearSelection() {
    selected=-1;
    promotion.clear();
    claimMove=false;
    confirmingResign=false;
}
void App::leave() {
    stopLobby();
    stopAccountCheck();
    liveBoard.reset();
    resetReviewSearch();
    if (working) {
        cancelled.store(true);
        joinWorker();
        working=false;
    }
    lan.stop();
    page=Home;
    reviewing=false;
    onlineAnalysis=false;
    variation.reset();
    variationPositions.clear();
    confirmingSignOut=false;
    resultOption=0;
    actions=false;
    connected=false;
    option=0;
    clearSelection();
    notice.clear();
}
void App::aiEntry(void* arg) {
    auto& app=*static_cast<App*>(arg);
    app.choice=choose(app.aiPosition,app.difficulty,milliseconds,app.cancelled);
    app.done.store(true,std::memory_order_release);
}
void App::pollAI() {
    if (lan.role!=Lan::Off) return;
    if (working && done.load(std::memory_order_acquire)) {
        joinWorker();
        working=false;
        if (choice.valid && solo.game.result==Result::Playing && solo.game.pos.turn!=color) {
            const auto action=choice.claim?(choice.claimWithMove?Action::ClaimWithMove:Action::Claim):Action::Move;
            solo.apply(opposite(color),{solo.revision,0,action,choice.move},false);
        }
    }
    if (working || aiFailed || solo.game.result!=Result::Playing || solo.game.pos.turn==color) return;
    aiPosition=solo.game;
    cancelled.store(false);
    done.store(false);
    working=launchWorker(aiEntry,this);
    if (!working) { aiFailed=true; notice="Could not start computer"; }
}
std::string App::address() const {
    std::string ip;
    for (auto n:octets) {
        if (!ip.empty()) ip+='.';
        ip+=std::to_string(n);
    }
    return ip;
}
std::vector<std::string> App::menuItems() const {
    if (page==Home) return {"Play computer","Host LAN match","Join LAN match","Lichess setup","Appearance","Quit"};
    if (page==Appearance) return {"Dark","Light","Back"};
    if (page==LichessSetup) {
        if (confirmingSignOut) return {"Cancel","Remove credential"};
        if (lichess.available()) return {"Verify account","Active games","Online lobby","Local sign out","Back"};
        return {"Check card","Local sign out","Back"};
    }
    if (page==LichessGames) {
        std::vector<std::string> rows;
        for (int n=0;n<activeGameRows();++n) rows.push_back(accountGames.games[n].opponent);
        rows.emplace_back("Refresh");
        rows.emplace_back("Back");
        return rows;
    }
    if (page==LichessPreview) {
        std::vector<std::string> rows{"Return to board"};
        for (const auto& row:onlineActionList()) rows.push_back(row.first);
        rows.emplace_back("Reconnect");
        rows.emplace_back("Back to games");
        return rows;
    }
    if (page==Computer) return {std::string("Difficulty: ")+levels[int(difficulty)],
        std::string("Play as: ")+name(color),std::string("Clock: ")+clockNames[clockPreset],"Start game","Back"};
    if (page==HostSetup) return {std::string("Play as: ")+name(color),std::string("Clock: ")+clockNames[clockPreset],"Start hosting","Back"};
    return {"Host IP: "+address(),"Connect","Back"};
}
int App::activeGameRows() const {
    return checkingAccount || wifiDeadline?0:int(accountGames.games.size());
}
void App::checkCredential() {
    std::string token;
    switch (readCredential(credentialPath,token)) {
        case CredentialStatus::Missing: notice="No sign-in file. Use the Windows helper."; break;
        case CredentialStatus::Unreadable: notice="Cannot read the sign-in file."; break;
        case CredentialStatus::Invalid: notice="Invalid sign-in file. Run the helper again."; break;
        case CredentialStatus::Present: notice="Sign-in file found on the card."; break;
    }
}
void App::eventsEntry(void* arg) {
    auto& app=*static_cast<App*>(arg);
    const auto profile=app.lichess.verify(app.eventToken,app.caPath,app.eventsCancelled);
    if (profile.status==AccountCheck::Verified) {
        app.eventUsername=profile.username;
        app.eventsResult=app.extra.events(app.eventToken,app.caPath,app.eventsCancelled,receiveAccountEvent,&app);
    } else app.eventsResult=profile.status;
    app.eventsDone.store(true,std::memory_order_release);
}
void App::receiveAccountEvent(void* arg,const AccountEvent& event) {
    auto& app=*static_cast<App*>(arg);
    if (event.kind==AccountEventKind::Ready) app.eventsReady.store(true);
    if (event.kind==AccountEventKind::Challenge) {
        if (usernameKey(event.challenge.user)==usernameKey(app.eventUsername)) return;
    }
    const unsigned w=app.eventWrite.load(), next=(w+1)%32;
    if (next==app.eventRead.load(std::memory_order_acquire)) return;
    app.eventQueue[w]=event; app.eventWrite.store(next,std::memory_order_release);
}
void App::stopLobby() {
    if (page==LichessLobby) stopAccountCheck();
    if (eventsRunning) {
        eventsCancelled.store(true); joinWorker(1); eventsRunning=false;
    }
    eventToken.clear(); eventsReady.store(false); lobbyPending=false;
    friendCommand.reset(); friendReply={}; friendsList={};
}
void App::enterLobby() {
    stopAccountCheck(); stopLobby();
    page=LichessLobby; lobbyView=0; option=0; challenges.clear(); announcedGame.reset();
    eventRead.store(0); eventWrite.store(0); eventsRetryAt=0;
    eventsDeadline=milliseconds()+20000; notice="Connecting to Lichess...";
}
void App::openOnlineGame(ActiveGame game) {
    stopLobby(); stopAccountCheck();
    accountGames={AccountCheck::Verified,{std::move(game)}};
    previewIndex=0; page=LichessPreview; onlineMenu=false; onlineRequest={};
    requestReply.store(PostResult::None); liveBoard.reset();
    const bool mute=chat.hideNotifications; chat=ChatState{}; chat.hideNotifications=mute;
    ChatEvent discarded; while (chatInbox.pop(discarded)) {}
    cursor=-1; clearSelection(); startAccountCheck();
}
void App::queueLobby(LobbyRequest request) {
    if (checkingAccount || lobbyPending || milliseconds()<accountRetryAt) { notice="Please wait before another request."; return; }
    if (request.action==LobbyAction::Seek) option=0;
    lobbyCommand=std::move(request); lobbyPending=true; notice="Waiting for account stream...";
}
void App::queueFriends(FriendRequest request) {
    if (checkingAccount || lobbyPending || milliseconds()<accountRetryAt) {
        notice="Please wait before another request.";
        return;
    }
    friendCommand=std::move(request);
    lobbyPending=true;
    notice="Waiting for account stream...";
}
void App::applyFriendsReply(uint64_t now) {
    const auto mutation=friendReply.mutation;
    const auto status=friendReply.list.status;
    if (status==AccountCheck::Verified) {
        std::string selected;
        if (friendOption>=friendControls && friendOption-friendControls<int(friendsList.players.size()))
            selected=friendsList.players[friendOption-friendControls].id;
        friendsList=std::move(friendReply.list);
        const auto found=std::find_if(friendsList.players.begin(),friendsList.players.end(),
            [&](const Friend& player){return player.id==selected;});
        if (friendOption>=friendControls)
            friendOption=found==friendsList.players.end()?0:friendControls+int(found-friendsList.players.begin());
    }
    if (mutation==AccountCheck::RateLimited || status==AccountCheck::RateLimited || friendsList.rateLimited) {
        accountRetryAt=now+60000;
        friendsList.rateLimited=false;
        notice="Rate limited. Wait one minute.";
    } else if (mutation==AccountCheck::Rejected || status==AccountCheck::Rejected)
        notice="Run sign-in helper with -Replace.";
    else if (friendReply.targetUnavailable) notice="Lichess player unavailable.";
    else if (status!=AccountCheck::Verified)
        notice=friendCommand->action==FriendAction::Refresh?"Could not refresh friends.":"Check the list with Refresh.";
    else if (mutation!=AccountCheck::Verified && !friendsList.truncated) {
        const auto id=usernameKey(friendCommand->username);
        const bool followed=std::any_of(friendsList.players.begin(),friendsList.players.end(),
            [&](const Friend& player){return player.id==id;});
        notice=followed?"List refreshed. Player is followed.":"List refreshed. Player is not followed.";
    }
    else if (friendsList.truncated) notice="Showing up to 1000 friends.";
    else notice=friendsList.players.empty()?"No friends yet. Add by username.":"Friends up to date.";
    friendCommand.reset();
    friendReply={};
}
void App::updateFriends(Input input,bool dual) {
    const int count=friendControls+int(friendsList.players.size());
    const int first=firstVisible(friendOption,5,count);
    if (pressed(input,Back)) { lobbyView=0; option=friendsLobbyOption; return; }
    if (pressed(input,Menu)) { queueFriends({}); return; }
    if (dual && input.x>=16 && input.x<304 && input.y>=196 && input.y<224 && count>5) {
        friendOption=std::clamp(friendOption+(input.x<160?-5:5),0,count-1);
        return;
    }
    cursorMove(friendOption,count,input);
    const bool hit=dual && input.x>=16 && input.x<304 && input.y>=menuTop &&
        input.y<menuTop+5*menuRow && (input.y-menuTop)%menuRow<28;
    if (hit) {
        const int row=first+(input.y-menuTop)/menuRow;
        if (row>=count) return;
        friendOption=row;
    }
    if (!hit && !pressed(input,Confirm)) return;
    switch (friendOption) {
        case 0: lobbyView=addFriendView; editor={}; friendName.clear(); notice.clear(); break;
        case 1: queueFriends({}); break;
        case 2: lobbyView=1; option=0; break;
        case 3: lobbyView=0; option=friendsLobbyOption; break;
        default:
            selectedFriend=friendsList.players[friendOption-friendControls].username;
            lobbyView=friendActionsView; option=0; notice.clear();
            break;
    }
}
void App::drawFriends(bool dual) const {
    const auto& t=palette(themeId);
    const int width=dual?288:448;
    text(16,12,"Friends",t.text,2);
    const int count=friendControls+int(friendsList.players.size());
    const int first=firstVisible(friendOption,5,count);
    const std::string controls[]={"Add by username","Refresh","Incoming ("+std::to_string(challenges.size())+")","Back"};
    constexpr const char* statusNames[]={"Status unavailable","Offline","Online","Playing"};
    for (int row=first;row<count && row<first+5;++row) {
        const int y=menuTop+(row-first)*menuRow;
        if (row<friendControls) button(16,y,width,28,controls[row],t,row==friendOption);
        else {
            const auto& player=friendsList.players[row-friendControls];
            const auto* status=statusNames[int(player.presence)];
            const int statusX=16+width-10-textWidth(status);
            button(16,y,width,28,"",t,row==friendOption);
            text(26,y+7,player.username,t.text,textWidth(player.username,2)<=statusX-36?2:1);
            text(statusX,y+8,status,t.secondary);
        }
    }
    if (dual && count>5) {
        button(16,196,140,28,"Page up",t,false);
        button(164,196,140,28,"Page down",t,false);
    }
    text(16,226,notice,t.heading);
}
void App::tickLobby() {
    const auto now=milliseconds();
    if (!eventsRunning && now>=eventsRetryAt) {
        if (wifiReady(now) && readCredential(credentialPath,eventToken)==CredentialStatus::Present) {
            eventsCancelled.store(false); eventsDone.store(false); eventsReady.store(false);
            eventsRunning=launchWorker(eventsEntry,this,1);
            if (!eventsRunning) { eventsRetryAt=now+3000; notice="Could not start account stream."; }
        } else if (now>=eventsDeadline) {
            notice="Check Wi-Fi and sign-in file."; eventsRetryAt=now+3000; eventsDeadline=now+20000;
        }
    }
    while (eventRead.load()!=eventWrite.load(std::memory_order_acquire)) {
        const unsigned r=eventRead.load(); const auto event=eventQueue[r];
        eventRead.store((r+1)%32,std::memory_order_release);
        if (event.kind==AccountEventKind::Ready && notice=="Connecting to Lichess...") notice="Ready for online play.";
        if (event.kind==AccountEventKind::GameStart) announcedGame=event.game;
        else if (event.kind==AccountEventKind::Challenge && event.challenge.incoming) {
            const auto found=std::find_if(challenges.begin(),challenges.end(),[&](const Challenge& c){return c.id==event.challenge.id;});
            if (found==challenges.end() && challenges.size()<16) challenges.push_back(event.challenge);
        } else if (event.kind==AccountEventKind::ChallengeRemoved) {
            challenges.erase(std::remove_if(challenges.begin(),challenges.end(),[&](const Challenge& c){return c.id==event.challenge.id;}),challenges.end());
            if (outgoingChallenge==event.challenge.id) outgoingChallenge.clear();
            if (selectedChallenge==event.challenge.id) { lobbyView=1; option=0; }
        }
    }
    if (announcedGame) { auto game=*announcedGame; announcedGame.reset(); openOnlineGame(std::move(game)); return; }
    if (eventsRunning && eventsDone.load(std::memory_order_acquire)) {
        joinWorker(1); eventsRunning=false; eventsReady.store(false);
        eventsRetryAt=eventsResult==AccountCheck::Rejected?UINT64_MAX:now+(eventsResult==AccountCheck::RateLimited?60000:3000);
        notice=eventsResult==AccountCheck::Rejected?"Sign-in rejected. Run the helper again.":"Account stream reconnecting...";
    }
    if (checkingAccount && done.load(std::memory_order_acquire)) {
        stopAccountCheck();
        if (friendCommand) applyFriendsReply(now);
        else if (lobbyReply.status==AccountCheck::Verified) {
            if (!lobbyReply.challengeId.empty()) outgoingChallenge=lobbyReply.challengeId;
            switch (lobbyCommand.action) {
                case LobbyAction::Seek: notice="Search ended. Find opponent to retry."; break;
                case LobbyAction::Challenge: notice="Challenge sent. Waiting for opponent."; break;
                case LobbyAction::Accept: notice="Challenge accepted. Opening game..."; break;
                case LobbyAction::Decline: notice="Challenge declined."; break;
                case LobbyAction::Cancel: notice="Challenge canceled."; break;
            }
            if (lobbyCommand.action==LobbyAction::Decline || lobbyCommand.action==LobbyAction::Accept) { lobbyView=1; option=0; }
        } else if (lobbyReply.status==AccountCheck::RateLimited) { accountRetryAt=now+60000; notice="Rate limited. Wait one minute."; }
        else notice="Online request failed or expired.";
    }
    if (lobbyPending && !checkingAccount && eventsReady.load() && now>=accountRetryAt) {
        if (readCredential(credentialPath,accountToken)!=CredentialStatus::Present) {
            lobbyPending=false; friendCommand.reset(); notice="Sign-in file missing."; return;
        }
        cancelled.store(false); done.store(false); checkingAccount=launchWorker(accountEntry,this);
        lobbyPending=false;
        notice=checkingAccount?(friendCommand?"Updating friends...":"Waiting for opponent..."):"Could not start request.";
        if (!checkingAccount) friendCommand.reset();
    }
}
std::vector<std::string> App::lobbyItems() const {
    if (friendCommand || lobbyView==friendsView) return {};
    if (checkingAccount || lobbyPending) return {lobbyCommand.action==LobbyAction::Seek?"Cancel search":"Cancel request"};
    if (lobbyView==1) {
        std::vector<std::string> rows;
        for (const auto& challenge:challenges) rows.push_back(challenge.user);
        rows.emplace_back("Back"); return rows;
    }
    if (lobbyView==2) return {"Accept challenge","Decline challenge","Back"};
    if (lobbyView==friendActionsView) return {"Challenge","Unfollow","Back"};
    if (lobbyView==unfollowView) return {"Cancel","Confirm unfollow"};
    return {std::string("Clock: ")+clockNames[lobbyClock],"Find opponent","Challenge player",
        "Incoming ("+std::to_string(challenges.size())+")","Active games","Cancel challenge","Friends","Back"};
}
void App::updateLobby(Input i,bool dual) {
    const bool updatingFriends=friendCommand.has_value();
    const bool searching=!updatingFriends && (checkingAccount || lobbyPending) && lobbyCommand.action==LobbyAction::Seek;
    const auto before=lobbyItems(); const auto focus=option<int(before.size())?before[option]:std::string{};
    tickLobby(); if (page!=LichessLobby) return;
    if (updatingFriends) {
        if (friendCommand && pressed(i,Back)) {
            const auto action=friendCommand->action;
            lobbyPending=false;
            stopAccountCheck();
            friendCommand.reset(); friendReply={};
            notice=action==FriendAction::Refresh?"Refresh canceled.":"Change unconfirmed. Refresh list.";
        }
        return;
    }
    if (lobbyView==friendsView) { updateFriends(i,dual); return; }
    if ((lobbyView==3 || lobbyView==addFriendView) && !checkingAccount && !lobbyPending) {
        const bool adding=lobbyView==addFriendView;
        auto& username=adding?friendName:challengeName;
        if (pressed(i,Back)) { lobbyView=adding?friendsView:0; option=2; return; }
        if (updateEditor(i,dual,username,32)) {
            if (!validUsername(username)) notice="Enter a valid Lichess username.";
            else if (adding) {
                if (eventsReady.load() && usernameKey(username)==usernameKey(eventUsername)) notice="You cannot follow yourself.";
                else { lobbyView=friendsView; queueFriends({FriendAction::Follow,username}); }
            } else { lobbyView=0; queueLobby({LobbyAction::Challenge,username,clockMinutes[lobbyClock],lobbyClock==0?10u:0u,-1}); }
        } else if ((editor.selected==45 && pressed(i,Confirm)) || (dual && i.x>=160 && i.x<300 && i.y>=210 && i.y<238)) {
            lobbyView=adding?friendsView:0; option=2;
        }
        return;
    }
    const auto rows=lobbyItems();
    if (rows!=before) {
        const auto found=std::find(rows.begin(),rows.end(),focus);
        option=searching && lobbyView==0?findOpponentOption:found==rows.end()?0:int(found-rows.begin());
        return;
    }
    const bool back=pressed(i,Back);
    if (back && !checkingAccount && !lobbyPending) {
        if (lobbyView==unfollowView) { lobbyView=friendActionsView; option=1; }
        else if (lobbyView==friendActionsView) { lobbyView=friendsView; option=0; }
        else if (lobbyView) { lobbyView=0; option=0; }
        else returnToLichessSetup();
        return;
    }
    const int first=firstVisible(option,5,int(rows.size())); cursorMove(option,int(rows.size()),i);
    const bool hit=dual && i.x>=16 && i.x<304 && i.y>=50 && i.y<195 && (i.y-50)%29<28;
    if (hit) option=first+(i.y-50)/29;
    if (!back && (option>=int(rows.size()) || (!hit && !pressed(i,Confirm)))) return;
    if (checkingAccount || lobbyPending) {
        lobbyPending=false; stopAccountCheck();
        option=searching && lobbyView==0?findOpponentOption:0;
        notice=searching?"Search canceled.":"Request canceled.";
        return;
    }
    if (lobbyView==1) {
        if (option==int(challenges.size())) { lobbyView=0; option=3; }
        else { selectedChallenge=challenges[option].id; lobbyView=2; option=0; }
    } else if (lobbyView==2) {
        if (option==2) { lobbyView=1; option=0; }
        else queueLobby({option==0?LobbyAction::Accept:LobbyAction::Decline,selectedChallenge,15,10,-1});
    } else if (lobbyView==friendActionsView) {
        if (option==0) {
            lobbyView=0; option=friendsLobbyOption;
            queueLobby({LobbyAction::Challenge,selectedFriend,clockMinutes[lobbyClock],lobbyClock==0?10u:0u,-1});
        } else if (option==1) { lobbyView=unfollowView; option=0; }
        else { lobbyView=friendsView; option=0; }
    } else if (lobbyView==unfollowView) {
        if (option==0) { lobbyView=friendActionsView; option=1; }
        else { lobbyView=friendsView; option=0; queueFriends({FriendAction::Unfollow,selectedFriend}); }
    } else if (option==0) lobbyClock=(lobbyClock+1)%3;
    else if (option==findOpponentOption) queueLobby({LobbyAction::Seek,{},clockMinutes[lobbyClock],lobbyClock==0?10u:0u,-1});
    else if (option==2) { lobbyView=3; editor={}; }
    else if (option==3) { lobbyView=1; option=0; }
    else if (option==4) { stopLobby(); page=LichessGames; startAccountCheck(); }
    else if (option==5) {
        if (outgoingChallenge.empty()) notice="No outgoing challenge.";
        else { queueLobby({LobbyAction::Cancel,outgoingChallenge,15,10,-1}); outgoingChallenge.clear(); }
    } else if (option==friendsLobbyOption) {
        lobbyView=friendsView; friendOption=0; queueFriends({});
    } else returnToLichessSetup();
}
void App::drawLobby(bool dual) const {
    const auto& t=palette(themeId); const int width=dual?288:448;
    if (lobbyView==friendsView) { drawFriends(dual); return; }
    const std::string title=lobbyView==addFriendView?"Add by username":
        lobbyView==friendActionsView || lobbyView==unfollowView?selectedFriend:"Lichess online";
    text(16,12,title,t.text,textWidth(title,2)<=width?2:1);
    if (lobbyView==3 && !checkingAccount && !lobbyPending) { drawEditor(dual,challengeName,"Challenge"); return; }
    if (lobbyView==addFriendView) {
        drawEditor(dual,friendName,"Follow",notice);
        return;
    }
    const auto rows=lobbyItems(); const int first=firstVisible(option,5,int(rows.size()));
    for (int n=first;n<int(rows.size()) && n<first+5;++n) button(16,50+(n-first)*29,width,28,rows[n],t,n==option);
    text(16,207,eventsReady.load()?"Account: "+eventUsername:"Connecting to account stream...",t.secondary);
    text(16,226,notice,t.heading);
}
void App::accountEntry(void* arg) {
    auto& app=*static_cast<App*>(arg);
    if (app.page==LichessLobby) {
        if (app.friendCommand) app.friendReply=app.extra.friends(*app.friendCommand,app.accountToken,app.caPath,app.cancelled);
        else app.lobbyReply=app.extra.lobby(app.lobbyCommand,app.accountToken,app.caPath,app.cancelled);
    }
    else if (app.page==LichessPreview) {
        const auto& id=app.accountGames.games[app.previewIndex].id;
        while (!app.cancelled.load()) {
            auto* queued=app.pendingRequest.exchange(nullptr);
            if (queued) {
                const auto request=*queued; delete queued;
                const auto result=request.action==BoardAction::Move?
                    app.lichess.move(id,request.move,app.accountToken,app.caPath,app.cancelled):
                    app.lichess.action(id,request.action,app.accountToken,app.caPath,app.cancelled);
                app.workerStreamRequest=request.id;
                app.requestReply.store(result);
                if (result==PostResult::RateLimited) { app.streamResult=AccountCheck::RateLimited; break; }
                continue;
            }
            if (auto* request=app.pendingChat.exchange(nullptr)) {
                const bool sending=request->send;
                PostResult result=PostResult::None;
                if (sending) {
                    result=app.extra.send(id,request->text,app.accountToken,app.caPath,app.cancelled);
                    ChatEvent event; event.kind=ChatEvent::Delivery; event.delivery=result;
                    app.chatInbox.push(std::move(event));
                }
                delete request;
                if (result==PostResult::RateLimited) { app.streamResult=AccountCheck::RateLimited; break; }
                ChatEvent event; event.kind=ChatEvent::History;
                event.history=app.extra.history(id,app.accountToken,app.caPath,app.cancelled);
                const auto status=event.history.status;
                app.chatInbox.push(std::move(event));
                if (status==AccountCheck::RateLimited) { app.streamResult=status; break; }
                continue;
            }
            app.interruptStream.store(false);
            if (app.pendingRequest.load() || app.pendingChat.load()) continue;
            app.streamResult=app.lichess.stream(id,app.accountToken,app.caPath,app.cancelled,
                receiveLiveBoard,&app,&app.interruptStream,receiveChat,&app);
            if (app.cancelled.load() || (!app.pendingRequest.load() && !app.pendingChat.load())) break;
        }
    } else if (app.page==LichessGames) app.accountGames=app.lichess.games(app.accountToken,app.caPath,app.cancelled);
    else app.accountResult=app.lichess.verify(app.accountToken,app.caPath,app.cancelled);
    app.done.store(true,std::memory_order_release);
}
void App::receiveLiveBoard(void* arg,const LiveBoard& board) {
    auto& app=*static_cast<App*>(arg);
    if (auto* update=new(std::nothrow) LiveBoard(board)) {
        update->receivedAt=milliseconds();
        update->synchronizedRequest=app.workerStreamRequest;
        delete app.incomingBoard.exchange(update);
    }
}
void App::receiveChat(void* arg,const ChatMessage& message) {
    auto& app=*static_cast<App*>(arg);
    ChatEvent event; event.message=message;
    app.chatInbox.push(std::move(event));
}
bool App::queueChat(bool send) {
    if (milliseconds()<accountRetryAt || pendingChat.load() || chat.fetching) return false;
    if (send && !chat.beginSend()) { notice=chat.restricted?"Chat is unavailable for this account.":"Check the draft or wait for delivery."; return false; }
    auto* request=new(std::nothrow) PendingChat{send,send?chat.submitted:std::string{}};
    if (!request) { if (send) chat.completeSend(PostResult::Rejected); return false; }
    chat.fetching=true;
    pendingChat.store(request);
    interruptStream.store(true);
    if (!checkingAccount && !wifiDeadline) startAccountCheck();
    return true;
}
void App::tickChat() {
    ChatEvent event;
    while (chatInbox.pop(event)) {
        if (event.kind==ChatEvent::Line) chat.receive(std::move(event.message));
        else if (event.kind==ChatEvent::History) chat.replace(std::move(event.history));
        else { chat.completeSend(event.delivery); chat.fetching=false; }
    }
    if (chatInbox.overflow.exchange(false)) {
        chat.refreshNeeded=true; chat.fetching=false;
        if (chat.delivery==PostResult::Sending) chat.completeSend(PostResult::Uncertain);
    }
}
bool App::updateEditor(Input i,bool dual,std::string& value,size_t limit) {
    const int keyW=dual?28:18, keyH=dual?28:30, left=dual?20:16, top=64;
    int selectedKey=editor.selected;
    if (selectedKey<40) {
        int x=selectedKey%10,y=selectedKey/10;
        if (pressed(i,Left)) x=(x+9)%10;
        if (pressed(i,Right)) x=(x+1)%10;
        if (pressed(i,Up)) y=(y+5)%6;
        if (pressed(i,Down)) ++y;
        selectedKey=y<4?y*10+x:y==4?40+std::min(3,x/3):44+std::min(1,x/5);
    } else {
        const int row=selectedKey<44?4:5, count=row==4?4:2;
        int x=selectedKey-(row==4?40:44);
        if (pressed(i,Left)) x=(x+count-1)%count;
        if (pressed(i,Right)) x=(x+1)%count;
        if (pressed(i,Up)) selectedKey=row==4?30+x*3:40+x*2;
        else if (pressed(i,Down)) selectedKey=row==4?44+x/2:x*5;
        else selectedKey=(row==4?40:44)+x;
    }
    bool hit=false;
    if (dual && i.x>=left && i.x<left+keyW*10 && i.y>=top && i.y<top+4*keyH) {
        selectedKey=((i.y-top)/keyH)*10+(i.x-left)/keyW; hit=true;
    } else if (dual && i.x>=20 && i.x<300 && i.y>=180 && i.y<208) {
        selectedKey=40+(i.x-20)/70; hit=true;
    } else if (dual && i.x>=20 && i.x<300 && i.y>=210 && i.y<238) {
        selectedKey=44+(i.x>=160); hit=true;
    }
    editor.selected=selectedKey;
    if (!hit && !pressed(i,Confirm)) return false;
    if (selectedKey==44) return true;
    if (selectedKey==45) return false;
    if (selectedKey==40) editor.upper=!editor.upper;
    else if (selectedKey==41) editor.symbols=!editor.symbols;
    else editor.insert(value,limit);
    return false;
}
void App::drawEditor(bool dual,const std::string& value,const char* sendLabel,std::string_view hint) const {
    const auto& t=palette(themeId);
    const int keyW=dual?28:18,keyH=dual?28:30,left=dual?20:16;
    const size_t visible=dual?45:27;
    text(16,36,value.size()>visible?"..."+value.substr(value.size()-visible):value,t.text);
    text(16,49,hint.empty()?std::to_string(value.size())+" characters":std::string(hint),t.secondary);
    for (int n=0;n<40;++n) {
        const int x=left+(n%10)*keyW,y=64+(n/10)*keyH;
        roundedRect(x,y,keyW-1,keyH-1,editor.selected==n?t.focus:t.surface);
        if (editor.selected==n) outline(x,y,keyW-1,keyH-1,t.accent);
        text(x+(dual?9:5),y+7,std::string(1,editor.character(n)),t.text,dual?2:1);
    }
    const char* labels[]={"Shift","Symbols","Space","Del"};
    const int controlsY=dual?180:188,controlW=dual?70:48;
    for (int n=0;n<4;++n) {
        roundedRect(left+n*controlW,controlsY,controlW-1,28,editor.selected==40+n?t.focus:t.surface);
        text(left+n*controlW+3,controlsY+8,labels[n],t.text);
        if (editor.selected==40+n) outline(left+n*controlW,controlsY,controlW-1,28,t.accent);
    }
    const int y=dual?210:221,w=dual?140:96;
    button(left,y,w-2,28,sendLabel,t,editor.selected==44,true);
    button(left+w,y,w-2,28,"Back",t,editor.selected==45);
}
bool App::updateChat(Input i,bool dual) {
    const int bubble=dual?278:180;
    if (pressed(i,Chat) || (dual && i.x>=bubble && i.x<bubble+26 && i.y>=4 && i.y<28 && chat.focus==ChatFocus::Closed)) {
        if (chat.focus!=ChatFocus::Closed) chat.focus=ChatFocus::Closed;
        else {
            chat.focus=ChatFocus::Transcript; chat.unread=0; chatOption=0;
            if (!chat.loaded || chat.refreshNeeded || (liveBoard && liveBoard->finished)) queueChat(false);
        }
        return true;
    }
    if (chat.focus==ChatFocus::Closed) return false;
    chat.unread=0;
    if (pressed(i,Back)) {
        chat.focus=chat.focus==ChatFocus::Transcript?ChatFocus::Closed:ChatFocus::Transcript;
        return true;
    }
    if (chat.focus==ChatFocus::Composer) {
        if (updateEditor(i,dual,chat.draft,140)) queueChat(true);
        else if (editor.selected==45 && pressed(i,Confirm)) chat.focus=ChatFocus::Transcript;
        if (dual && i.x>=160 && i.x<300 && i.y>=210 && i.y<238) chat.focus=ChatFocus::Transcript;
        return true;
    }
    if (chat.focus==ChatFocus::Phrases) {
        cursorMove(chatOption,5,i);
        const bool hit=dual && i.x>=16 && i.x<304 && i.y>=38 && i.y<38+5*29;
        if (hit) chatOption=(i.y-38)/29;
        if (hit || pressed(i,Confirm)) { chat.draft=quickPhrases[chatOption]; chat.focus=ChatFocus::Composer; editor.selected=44; }
        return true;
    }
    if (pressed(i,Menu)) { chat.hideNotifications=!chat.hideNotifications; if (chat.hideNotifications) chat.unread=0; }
    if (pressed(i,HistoryPrev)) chatScroll=std::min(chatScroll+3,800);
    if (pressed(i,HistoryNext)) chatScroll=std::max(0,chatScroll-3);
    cursorMove(chatOption,4,i);
    if (pressed(i,Left)) chatOption=(chatOption+3)%4;
    if (pressed(i,Right)) chatOption=(chatOption+1)%4;
    const int y=dual?176:202,w=dual?144:96;
    const bool hit=dual && i.x>=16 && i.x<304 && i.y>=y && i.y<y+60 && (i.y-y)%30<28;
    if (hit) chatOption=(i.y-y)/30*2+(i.x>=16+w);
    if (hit || pressed(i,Confirm)) {
        if (chatOption==0) { chat.focus=ChatFocus::Composer; editor.selected=0; }
        else if (chatOption==1) { chat.focus=ChatFocus::Phrases; chatOption=0; }
        else if (chatOption==2) queueChat(false);
        else chat.focus=ChatFocus::Closed;
    }
    return true;
}
void App::drawChat(bool dual) const {
    const auto& t=palette(themeId);
    const int width=dual?288:192,half=width/2;
    for (int n=0;n<2;++n) {
        const auto player=n?opposite(accountGames.games[previewIndex].player):accountGames.games[previewIndex].player;
        const auto seconds=liveBoard?(uint64_t(liveBoard->remainingAt(player,milliseconds()))+999)/1000:0;
        const auto time=liveBoard && liveBoard->realtimeClock?std::to_string(seconds/60)+":"+(seconds%60<10?"0":"")+std::to_string(seconds%60):"--:--";
        const bool active=liveBoard && !liveBoard->finished && liveBoard->position.turn==player;
        const auto ink=liveBoard && liveBoard->finished?t.secondary:active?t.active:t.waiting;
        roundedRect(16+n*half,4,half-2,28,t.surface);
        setTextRight(16+(n+1)*half-4);
        text(20+n*half,10,std::string(active?"> ":"")+(n?"Opp ":"You ")+time,ink,2);
    }
    setTextRight(16+width);
    if (chat.focus==ChatFocus::Composer) {
        drawEditor(dual,chat.draft,"Send");
        const char* status=chat.restricted?"Chat unavailable":chat.delivery==PostResult::Sending?"Sending...":
            chat.delivery==PostResult::Uncertain?"Uncertain delivery; draft kept":chat.delivery==PostResult::Rejected ||
            chat.delivery==PostResult::RateLimited?"Send failed; draft kept":chat.delivery==PostResult::Accepted?"Sent":nullptr;
        if (status) { rect(16,48,width,14,t.background); text(16,49,status,t.secondary); }
        return;
    }
    if (chat.focus==ChatFocus::Phrases) {
        for (int n=0;n<5;++n) button(16,38+n*29,width,28,quickPhrases[n],t,chatOption==n);
        text(16,198,"Choose a phrase, then press Send.",t.secondary); return;
    }
    std::vector<std::string> lines;
    for (const auto& message:chat.messages) {
        lines.push_back(message.user+":");
        std::string line;
        for (char c:message.text) {
            if (!line.empty() && textWidth(line+c)>width-10) { lines.push_back(line); line.clear(); }
            line+=c;
        }
        if (!line.empty()) lines.push_back(line);
    }
    const int count=dual?8:10, end=std::max(0,int(lines.size())-chatScroll), first=std::max(0,end-count);
    for (int n=first;n<end;++n) text(20,38+(n-first)*14,lines[n],t.text);
    std::string status=chat.restricted?"Chat unavailable":chat.fetching?"Updating chat...":chat.refreshNeeded?"Refresh needed":
        chat.hideNotifications?"Notifications hidden":"L / R: scroll  Start: mute";
    if (chat.delivery==PostResult::Uncertain) status="Delivery uncertain; draft kept";
    else if (chat.delivery==PostResult::Rejected || chat.delivery==PostResult::RateLimited) status="Send failed; draft kept";
    text(16,dual?158:184,status,t.secondary);
    const char* labels[]={"Write","Quick phrases","Refresh","Back"};
    const int y=dual?176:202;
    for (int n=0;n<4;++n) button(16+(n%2)*half,y+(n/2)*30,half-2,28,labels[n],t,chatOption==n);
}
void App::startAccountCheck() {
    if (checkingAccount || wifiDeadline) {
        notice=checkingAccount?"Checking with Lichess...":"Connecting to Wi-Fi...";
        return;
    }
    if (page==LichessGames) {
        accountGames={};
        option=0;
    }
    if (page==LichessPreview) { streamSynchronized=false; streamRetryAt=0; }
    if (!lichess.available()) checkCredential();
    else if (milliseconds()<accountRetryAt) notice="Please wait before retrying.";
    else if (readCredential(credentialPath,accountToken)==CredentialStatus::Present) {
        wifiDeadline=milliseconds()+20000;
        notice="Connecting to Wi-Fi...";
    } else checkCredential();
}
void App::returnToLichessSetup() {
    stopLobby();
    stopAccountCheck();
    accountGames={};
    page=LichessSetup;
    option=1;
    notice.clear();
}
void App::stopAccountCheck() {
    wifiDeadline=0;
    if (checkingAccount) {
        cancelled.store(true);
        joinWorker();
        checkingAccount=false;
    }
    accountToken.clear();
    if (auto* unsent=pendingChat.exchange(nullptr)) {
        if (unsent->send) chat.completeSend(PostResult::Rejected);
        delete unsent;
        chat.fetching=false;
    }
    if (auto* unsent=pendingRequest.exchange(nullptr)) {
        delete unsent;
        onlineRequest.complete(PostResult::Rejected);
    }
    delete incomingBoard.exchange(nullptr);
}
void App::tickAccount() {
    const auto now=milliseconds();
    if (page==LichessPreview && !checkingAccount && !wifiDeadline && streamRetryAt && now>=streamRetryAt) startAccountCheck();
    if (wifiDeadline && now>=wifiDeadline) {
        stopAccountCheck();
        if (page==LichessPreview) streamRetryAt=now+5000;
        notice="Wi-Fi connection timed out.";
    } else if (wifiDeadline && wifiReady(now)) {
        wifiDeadline=0;
        cancelled.store(false);
        interruptStream.store(false);
        workerStreamRequest=onlineRequest.requestId();
        done.store(false);
        checkingAccount=launchWorker(accountEntry,this);
        notice=checkingAccount?(page==LichessPreview?"Connecting to game...":"Checking with Lichess..."):
            "Could not start Lichess request.";
        if (!checkingAccount) accountToken.clear();
    }
    const bool complete=checkingAccount && done.load(std::memory_order_acquire);
    if (page==LichessPreview) {
        tickChat();
        const auto reply=requestReply.exchange(PostResult::None);
        if (reply!=PostResult::None) onlineRequest.complete(reply);
        if (auto* update=incomingBoard.exchange(nullptr)) {
            const bool recovered=!streamSynchronized && liveBoard.has_value();
            if (update->finished && (!liveBoard || !liveBoard->finished)) chat.focus=ChatFocus::Closed;
            if (!liveBoard || update->finished || liveBoard->position.identity()!=update->position.identity())
                clearSelection();
            liveBoard=*update;
            delete update;
            onlineRequest.synchronize(liveBoard->synchronizedRequest);
            streamSynchronized=true;
            streamFailures=0;
            notice=liveBoard->finished?"Game ended.":"Live board connected.";
            if (onlineRequest.requestId() && !onlineRequest.blocked()) notice="Server board synchronized.";
            if (recovered && chat.loaded && !chat.fetching) queueChat(false);
        }
        if (!complete) return;
        stopAccountCheck();
        switch (streamResult) {
            case AccountCheck::Verified: notice="Game ended."; break;
            case AccountCheck::Rejected: notice="Lichess refused access to this game."; break;
            case AccountCheck::InvalidReply: notice="Could not read the live board."; break;
            case AccountCheck::RateLimited:
                accountRetryAt=now+60000;
                streamRetryAt=accountRetryAt;
                notice="Lichess rate limit. Retry in one minute.";
                break;
            case AccountCheck::Failed:
                if (!liveBoard || !liveBoard->finished) streamRetryAt=now+std::min<uint64_t>(30000,2000ull<<std::min(4u,streamFailures++));
                notice="Connection lost. Retrying automatically."; break;
        }
        return;
    }
    if (!complete) return;
    stopAccountCheck();
    const bool listing=page==LichessGames;
    switch (listing?accountGames.status:accountResult.status) {
        case AccountCheck::Verified:
            if (listing) option=0;
            notice=listing?(accountGames.games.empty()?"No active games.":"Select a game to play."):
                "Verified as "+accountResult.username;
            break;
        case AccountCheck::InvalidReply: notice="Lichess returned an invalid response."; break;
        case AccountCheck::Rejected: notice="Lichess rejected the credential."; break;
        case AccountCheck::RateLimited:
            accountRetryAt=now+60000;
            notice="Lichess rate limit. Retry in one minute.";
            break;
        case AccountCheck::Failed: notice="HTTPS check failed. Check Wi-Fi and CA file."; break;
    }
}
void App::onResume() {
    clockTick=milliseconds(); lan.interrupt(clockTick);
    if (page==LichessPreview) { clearSelection(); stopAccountCheck(); startAccountCheck(); }
    else if (page==LichessLobby) { stopLobby(); enterLobby(); }
}
void App::activate(int item) {
    notice.clear();
    if (page==Home) {
        if (item==5) { quit=true; return; }
        constexpr Page destinations[]={Computer,HostSetup,JoinSetup,LichessSetup,Appearance};
        page=destinations[item];
        option=0;
        if (page==LichessSetup) checkCredential();
        return;
    }
    if (page==Appearance) {
        if (item==2) leave();
        else setTheme(item==0?ThemeId::Dark:ThemeId::Light);
        return;
    }
    if (page==LichessGames) {
        const int games=activeGameRows();
        if (item<games) {
            if (!accountGames.games[item].position) notice="No standard board preview for this game.";
            else {
                previewIndex=item;
                page=LichessPreview;
                option=0;
                liveBoard.reset();
                chat=ChatState{};
                ChatEvent discarded; while (chatInbox.pop(discarded)) {}
                onlineRequest={};
                requestReply.store(PostResult::None);
                onlineMenu=false;
                cursor=-1;
                clearSelection();
                if (!accountGames.games[item].id.empty()) startAccountCheck();
            }
        } else if (item==games) startAccountCheck();
        else returnToLichessSetup();
        return;
    }
    if (page==LichessPreview) {
        if (item==0) { onlineMenu=false; return; }
        const auto rows=onlineActionList();
        if (item<=int(rows.size())) {
            if (rows[item-1].second==BoardAction::Resign) {
                confirmingResign=true;
                actionOption=0;
            } else submitOnlineAction(rows[item-1].second);
            return;
        }
        onlineMenu=false;
        if (item==int(rows.size())+1) {
            option=0;
            startAccountCheck();
            return;
        }
        stopAccountCheck();
        liveBoard.reset();
        page=LichessGames;
        option=previewIndex;
        clearSelection();
        notice="Select a game to play.";
        return;
    }
    if (page==LichessSetup) {
        if (confirmingSignOut) {
            confirmingSignOut=false;
            option=0;
            if (item==0) checkCredential();
            else if (credentialPath && std::remove(credentialPath)==0) notice="Credential removed from this card.";
            else notice="No credential removed. Check your card.";
        } else if (item==0) startAccountCheck();
        else if (item==1 && lichess.available()) {
            stopAccountCheck();
            page=LichessGames;
            option=0;
            startAccountCheck();
        } else if (item==2 && lichess.available()) {
            enterLobby();
        } else if (item==(lichess.available()?3:1)) {
            stopAccountCheck();
            confirmingSignOut=true;
            option=0;
            notice="Remove this card's sign-in file?";
        } else leave();
        return;
    }
    if (page==Computer && item==0) { difficulty=Difficulty((int(difficulty)+1)%3); return; }
    if ((page==Computer && item==1) || (page==HostSetup && item==0)) { color=opposite(color); return; }
    if (page==JoinSetup && item==0) { edit=(edit+1)%4; return; }
    if ((page==Computer && item==2) || (page==HostSetup && item==1)) {
        clockPreset=(clockPreset+1)%4;
        return;
    }
    if ((page==Computer && item==4) || (page==HostSetup && item==3) || (page==JoinSetup && item==2)) { leave(); return; }
    if (page==JoinSetup && !validAddress(address())) { notice="Enter a valid host IP"; return; }
    solo=Match{};
    solo.host=color;
    aiFailed=false;
    cursor=selected=-1;
    revision=round=0;
    if (page==HostSetup) lan.begin(Lan::Host,color,{},milliseconds());
    if (page==JoinSetup) lan.begin(Lan::Guest,White,address(),milliseconds());
    clockTick=milliseconds();
    if (page!=JoinSetup) match().setClock(clockMinutes[clockPreset]*60000,clockPreset==0?10000:0);
    page=Game;
    actions=false;
}
void App::submit(Action action,Move move) {
    if (action==Action::ClaimWithMove && !match().game.canClaim(&move)) {
        clearSelection();
        notice="That move cannot claim a draw";
        return;
    }
    const bool local=lan.role==Lan::Off;
    bool ok=false;
    if (local && action==Action::Rematch && solo.game.result!=Result::Playing) {
        color=opposite(color);
        solo=Match{};
        solo.host=color;
        solo.setClock(clockMinutes[clockPreset]*60000,clockPreset==0?10000:0);
        resultOption=0;
        aiFailed=false;
        ok=true;
    } else if (local && action==Action::Takeback) {
        size_t plies=solo.game.history.size();
        while (plies && Color((plies-1)%2)!=color) --plies;
        ok=plies && solo.undoTo(plies-1);
    } else if (local) {
        ok=solo.apply(color,{solo.revision,0,action,move},false);
    } else {
        ok=lan.submit(action,move);
    }
    clearSelection();
    actions=false;
    notice=ok?"":"Action unavailable";
}
void App::click(int square) {
    const bool online=page==LichessPreview;
    if (online?!canPlayOnline():!variation && !canMove()) return;
    const auto& p=online?liveBoard->position:variation?*variation:match().game.pos;
    const auto mover=online?accountGames.games[previewIndex].player:variation?p.turn:side();
    cursor=square;
    if (p.board[square]*(mover==White?1:-1)>0) {
        selected=square==selected?-1:square;
        return;
    }
    if (selected<0) return;
    for (auto m:p.legal()) {
        if (m.from!=selected || m.to!=square) continue;
        if (m.promotion) promotion.push_back(m);
        else { playSelectedMove(m); return; }
    }
    promoOption=0;
}
void App::playSelectedMove(Move move) {
    if (page==LichessPreview) submitOnlineMove(move);
    else if (variation) {
        resetReviewSearch();
        *variation=variation->after(move);
        variationPositions.push_back(variation->identity());
        clearSelection();
    } else {
        submit(claimMove?Action::ClaimWithMove:Action::Move,move);
    }
}
std::vector<std::pair<std::string,Action>> App::actionList() const {
    std::vector<std::pair<std::string,Action>> options;
    const auto& m=match();
    const bool network=lan.role!=Lan::Off;
    const bool active=!network || lan.acceptingInput();
    if (active && m.game.result==Result::Playing) {
        if (canMove()) {
            if (m.game.canClaim()) options.emplace_back("Claim draw",Action::Claim);
            options.emplace_back("Claim with move",Action::ClaimWithMove);
        }
        if (network && m.game.history.size()>=2 && m.offer<0) options.emplace_back("Offer draw",Action::Offer);
        if (network && m.offer==opposite(side())) {
            options.emplace_back("Accept draw",Action::Accept);
            options.emplace_back("Decline draw",Action::DeclineDraw);
        }
        if (!network && m.game.history.size()>size_t(color)) options.emplace_back("Take back",Action::Takeback);
        if (network && !m.game.history.empty() && m.takeback<0) options.emplace_back("Request takeback",Action::Takeback);
        if (network && m.takeback==opposite(side())) {
            options.emplace_back("Accept takeback",Action::AcceptTakeback);
            options.emplace_back("Decline takeback",Action::DeclineTakeback);
        }
        options.emplace_back("Resign",Action::Resign);
    }
    return options;
}
void App::resetReviewSearch() {
    if (analyzing) {
        cancelled.store(true);
        joinWorker();
        analyzing=false;
    }
    reviewChoice.reset();
}
void App::beginOnlineAnalysis() {
    if (!liveBoard || !liveBoard->finished) return;
    stopAccountCheck(); tickChat();
    completedOnline=Match{};
    completedInitial=liveBoard->initialPosition;
    auto& game=completedOnline.game;
    game.pos=completedInitial; game.positions={game.pos.identity()}; game.history=liveBoard->history;
    for (auto move:game.history) { game.pos=game.pos.after(move); game.positions.push_back(game.pos.identity()); }
    game.result=liveBoard->winner?(*liveBoard->winner==White?Result::WhiteWins:Result::BlackWins):Result::Draw;
    completedOnline.initialMs=liveBoard->initialMs;
    completedOnline.remaining=liveBoard->remainingMs;
    completedOnline.flagFall=liveBoard->status=="outoftime";
    analysisColor=accountGames.games[previewIndex].player;
    reviewPly=game.history.size(); reviewPosition=game.pos;
    onlineAnalysis=true; reviewing=true; variation.reset(); variationPositions.clear();
    chat.focus=ChatFocus::Closed; page=Game; clearSelection(); resetReviewSearch();
}
void App::updateReviewSearch(Input i,bool dual) {
    if (analyzing && done.load(std::memory_order_acquire)) {
        joinWorker();
        analyzing=false;
        reviewChoice=choice;
    }
    const bool tapped=dual && i.x>=16 && i.x<304 && i.y>=52 && i.y<79;
    if (analyzing || (!tapped && !pressed(i,Menu))) return;
    aiPosition=GameState{};
    aiPosition.pos=variation?*variation:reviewPosition;
    const auto& recordedPositions=match().game.positions;
    const auto count=std::min(reviewPly+1,recordedPositions.size());
    aiPosition.positions.assign(recordedPositions.begin(),recordedPositions.begin()+count);
    aiPosition.positions.insert(aiPosition.positions.end(),variationPositions.begin(),variationPositions.end());
    aiPosition.updateResult();
    reviewChoice.reset();
    cancelled.store(false);
    done.store(false);
    analyzing=launchWorker(aiEntry,this);
    if (!analyzing) reviewChoice=Choice{};
}
void App::updateResults(Input i,bool dual) {
    const auto& history=match().game.history;
    if (reviewing) {
        updateReviewSearch(i,dual);
        if (variation) {
            if (updatePromotion(i,dual)) return;
            if (pressed(i,Back) || (dual && i.x>=16 && i.x<304 && i.y>=174 && i.y<204)) {
                resetReviewSearch();
                variation.reset();
                variationPositions.clear();
                clearSelection();
            } else {
                updateBoardInput(i);
            }
            return;
        }
        if (pressed(i,Back)) {
            resetReviewSearch();
            reviewing=false;
            return;
        }
        const bool previous=pressed(i,Left) ||
            (dual && i.x>=16 && i.x<156 && i.y>=134 && i.y<164);
        const bool next=pressed(i,Right) ||
            (dual && i.x>=164 && i.x<304 && i.y>=134 && i.y<164);
        if (previous && reviewPly) {
            resetReviewSearch();
            --reviewPly;
            reviewPosition=onlineAnalysis?completedInitial:Position::start();
            for (size_t n=0;n<reviewPly;++n) reviewPosition=reviewPosition.after(history[n]);
        } else if (next && reviewPly<history.size()) {
            resetReviewSearch();
            reviewPosition=reviewPosition.after(history[reviewPly++]);
        }
        if (pressed(i,Confirm) || (dual && i.x>=16 && i.x<304 && i.y>=207 && i.y<235)) {
            variation=reviewPosition;
            variationPositions.clear();
            cursor=-1;
        }
        if (dual && i.x>=16 && i.x<304 && i.y>=174 && i.y<204) {
            resetReviewSearch();
            reviewing=false;
        }
        return;
    }
    if (pressed(i,Back)) {
        leave();
        return;
    }
    cursorMove(resultOption,3,i);
    const bool touch=dual && i.x>=16 && i.x<304 &&
        i.y>=resultTop && i.y<resultTop+3*resultRow &&
        (i.y-resultTop)%resultRow<29;
    if (touch) resultOption=(i.y-resultTop)/resultRow;
    if (!touch && !pressed(i,Confirm)) return;
    if (resultOption==0) {
        if (lan.role==Lan::Off || (lan.acceptingInput() && !(match().rematch&(1u<<side()))))
            submit(Action::Rematch);
    } else if (resultOption==1) {
        const auto role=lan.role;
        leave();
        page=role==Lan::Off?Computer:role==Lan::Host?HostSetup:JoinSetup;
    } else {
        reviewing=true;
        reviewPly=history.size();
        reviewPosition=match().game.pos;
    }
}
void App::setTheme(ThemeId id) {
    themeId=id;
    appearanceSaved=saveTheme(appearancePath,id);
}
void App::update(Input i,bool dual) {
    if (pressed(i,AppearanceKey)) {
        setTheme(themeId==ThemeId::Dark?ThemeId::Light:ThemeId::Dark);
        return;
    }
    if (page==LichessLobby) { updateLobby(i,dual); return; }
    if (page==LichessPreview) { updateOnline(i,dual); return; }
    if (onlineAnalysis) {
        updateResults(i,dual);
        if (!reviewing) { onlineAnalysis=false; page=LichessPreview; clearSelection(); }
        return;
    }
    if (page!=Game) {
        if (page==LichessSetup || page==LichessGames || page==LichessPreview) tickAccount();
        if (pressed(i,Back) && page==LichessGames) { returnToLichessSetup(); return; }
        if (pressed(i,Back) && confirmingSignOut) {
            confirmingSignOut=false;
            option=0;
            checkCredential();
            return;
        }
        if (pressed(i,Back)) { if (page==Home) quit=true; else leave(); return; }
        const auto items=menuItems();
        cursorMove(option,int(items.size()),i);
        if (page==JoinSetup && option==0) {
            const int step=pressed(i,Fast)?10:1;
            if (pressed(i,Left)) octets[edit]=(octets[edit]+256-step)%256;
            if (pressed(i,Right)) octets[edit]=(octets[edit]+step)%256;
        }
        int hit=-1;
        if (dual && i.x>=16 && i.x<304) {
            const int rows=page==LichessGames?activeGameRows():int(items.size());
            if (i.y>=menuTop && i.y<menuTop+menuRow*rows) hit=(i.y-menuTop)/menuRow;
            if (page==LichessGames && i.y>=activeFooter && i.y<activeFooter+28 &&
                (i.x<156 || i.x>=164)) hit=rows+(i.x>=164?1:0);
        }
        if (hit>=0) {
            option=hit;
            activate(option);
        } else if (pressed(i,Confirm)) activate(option);
        return;
    }
    const auto now=milliseconds();
    lan.tick(now);
    const auto elapsed=now>=clockTick?now-clockTick:0;
    clockTick=now;
    if (lan.role==Lan::Off && !actions) solo.elapse(elapsed,true);
    if (lan.role==Lan::Off && (actions || solo.game.result!=Result::Playing) && working) {
        cancelled.store(true);
        joinWorker();
        working=false;
    }
    if (!actions) pollAI();
    const auto& m=match();
    if (m.revision!=revision || m.round!=round || connected!=lan.ready) {
        if (m.round!=round) {
            resetReviewSearch();
            cursor=-1;
            reviewing=false;
            variation.reset();
            variationPositions.clear();
            resultOption=0;
        }
        if (m.revision!=revision) historyOffset=0;
        round=m.round; revision=m.revision; connected=lan.ready;
        clearSelection();
    }
    if (m.game.result!=Result::Playing) {
        actions=false;
        updateResults(i,dual);
        return;
    }
    if (lan.timedOut(now)) {
        const bool hit=dual && i.x>=16 && i.x<304;
        if (pressed(i,Back) || (hit && i.y>=174 && i.y<202)) leave();
        else if (pressed(i,Confirm) || (hit && i.y>=145 && i.y<173)) lan.retry(now);
        return;
    }
    const int controlX=16, controlY=controlsTop(dual), rowHeight=controlsRow(dual);
    if (confirmingResign) {
        if (pressed(i,Back) || pressed(i,Menu)) {
            confirmingResign=false;
            actionOption=0;
            return;
        }
        cursorMove(actionOption,2,i);
        const bool touch=dual && i.x>=controlX && i.x<304 &&
            i.y>=controlY && i.y<controlY+2*rowHeight;
        if (touch) actionOption=(i.y-controlY)/rowHeight;
        if (touch || pressed(i,Confirm)) {
            confirmingResign=false;
            if (actionOption==1) submit(Action::Resign);
        }
        return;
    }
    if (updatePromotion(i,dual)) return;
    if (pressed(i,Menu) || (!actions && dual && i.x>=controlX && i.x<304 && i.y>=controlY && i.y<controlY+rowHeight)) {
        actions=!actions;
        actionOption=0;
        return;
    }
    if (actions) {
        const auto options=actionList();
        const int size=int(options.size());
        actionOption=std::min(actionOption,size+1);
        cursorMove(actionOption,size+2,i);
        if (pressed(i,Back)) { actions=false; return; }
        const int first=firstVisible(actionOption,visibleActions(dual),size+2);
        const bool hit=dual && i.x>=controlX && i.x<304 && i.y>=controlY &&
            i.y<controlY+visibleActions(dual)*rowHeight;
        const int touch=hit?first+(i.y-controlY)/rowHeight:-1;
        if (touch>=0 && touch<=size+1) actionOption=touch;
        if (!pressed(i,Confirm) && (touch<0 || touch>size+1)) return;
        if (actionOption==size) leave();
        else if (actionOption==size+1) actions=false;
        else if (options[actionOption].second==Action::Resign) {
            confirmingResign=true;
            actionOption=0;
        } else if (options[actionOption].second==Action::ClaimWithMove) {
            clearSelection();
            claimMove=true;
            actions=false;
        } else submit(options[actionOption].second);
        return;
    }
    if (pressed(i,Back)) {
        if (selected>=0 || claimMove) clearSelection();
        else { actions=true; actionOption=0; }
        return;
    }
    const int maxOffset=std::max(0,int(m.game.history.size())-6);
    if (pressed(i,HistoryPrev)) historyOffset=std::min(historyOffset+2,maxOffset);
    if (pressed(i,HistoryNext)) historyOffset=std::max(0,historyOffset-2);
    updateBoardInput(i);
}
bool App::updatePromotion(Input i,bool dual) {
    if (promotion.empty()) return false;
    if (pressed(i,Back)) { promotion.clear(); return true; }
    cursorMove(promoOption,4,i);
    const int y=controlsTop(dual), row=controlsRow(dual);
    const bool touch=dual && i.x>=16 && i.x<304 && i.y>=y && i.y<y+4*row;
    if (touch) promoOption=(i.y-y)/row;
    if (touch || pressed(i,Confirm)) {
        for (auto move:promotion) {
            if (move.promotion==promotions[promoOption]) {
                playSelectedMove(move);
                break;
            }
        }
    }
    return true;
}
void App::updateBoardInput(Input i) {
    const auto viewSide=page==LichessPreview?accountGames.games[previewIndex].player:side();
    if (cursor<0) cursor=(variation?variation->turn:viewSide)==White?12:52;
    int cell=viewSide==White?cursor^56:cursor^7;
    int x=cell%8,y=cell/8;
    x=std::clamp(x+int(pressed(i,Right))-int(pressed(i,Left)),0,7);
    y=std::clamp(y+int(pressed(i,Down))-int(pressed(i,Up)),0,7);
    cursor=(y*8+x)^(viewSide==White?56:7);
    if (pressed(i,Confirm)) click(cursor);
}
void App::updateOnline(Input i,bool dual) {
    const auto previousItems=onlineMenu?menuItems():std::vector<std::string>{};
    const auto previousSelection=onlineMenu && option<int(previousItems.size())?previousItems[option]:std::string{};
    tickAccount();
    if (updateChat(i,dual)) return;
    const int top=controlsTop(dual);
    if (liveBoard && liveBoard->finished && !onlineMenu) {
        if (pressed(i,Menu) || pressed(i,Back)) { onlineMenu=true; option=0; return; }
        cursorMove(resultOption,3,i);
        const bool hit=dual && i.x>=16 && i.x<304 && i.y>=resultTop && i.y<resultTop+3*resultRow &&
            (i.y-resultTop)%resultRow<29;
        if (hit) resultOption=(i.y-resultTop)/resultRow;
        if (!hit && !pressed(i,Confirm)) return;
        if (resultOption==2) beginOnlineAnalysis();
        else if (resultOption==1) enterLobby();
        else {
            const auto opponent=accountGames.games[previewIndex].opponent;
            const auto rematchColor=opposite(accountGames.games[previewIndex].player);
            if (!validUsername(opponent)) { notice="Cannot challenge this opponent."; return; }
            const auto minutes=liveBoard->initialMs/60000;
            const unsigned preset=minutes==10?1:minutes==30?2:0;
            enterLobby(); lobbyClock=int(preset);
            queueLobby({LobbyAction::Challenge,opponent,clockMinutes[preset],preset==0?10u:0u,int(rematchColor)});
        }
        return;
    }
    if (onlineMenu) {
        const auto items=menuItems();
        if (items!=previousItems) {
            const auto found=std::find(items.begin(),items.end(),previousSelection);
            option=found==items.end()?0:int(found-items.begin());
            return; // Let the updated choices be drawn before accepting a press or tap.
        }
        if (confirmingResign) {
            if (pressed(i,Back) || pressed(i,Menu)) { confirmingResign=false; return; }
            cursorMove(actionOption,2,i);
            const bool touch=dual && i.x>=16 && i.x<304 && i.y>=top && i.y<top+2*29 &&
                (i.y-top)%29<28;
            if (touch) actionOption=(i.y-top)/29;
            if (touch || pressed(i,Confirm)) {
                confirmingResign=false;
                if (actionOption==1) submitOnlineAction(BoardAction::Resign);
            }
            return;
        }
        if (pressed(i,Back) || pressed(i,Menu)) { onlineMenu=false; return; }
        const int count=int(items.size()), first=firstVisible(option,3,count);
        cursorMove(option,count,i);
        const bool touch=dual && i.x>=16 && i.x<304 && i.y>=top && i.y<top+3*29 &&
            (i.y-top)%29<28;
        if (touch) option=first+(i.y-top)/29;
        if (option>=count) return;
        if (touch || pressed(i,Confirm)) activate(option);
        return;
    }
    if (updatePromotion(i,dual)) return;
    if (pressed(i,Menu) || (dual && i.x>=16 && i.x<304 && i.y>=top && i.y<top+28)) {
        onlineMenu=true;
        option=0;
        return;
    }
    if (pressed(i,Back)) {
        if (selected>=0) clearSelection();
        else { onlineMenu=true; option=0; }
        return;
    }
    updateBoardInput(i);
}
void App::drawOnline(bool dual) const {
    if (chat.focus!=ChatFocus::Closed) { drawChat(dual); return; }
    const auto& t=palette(themeId);
    const int width=dual?288:192, top=controlsTop(dual);
    setTextRight(dual?274:174);
    text(16,12,"Lichess game",t.text,2);
    setTextRight(dual?304:208);
    const int bubbleX=dual?278:180;
    roundedRect(bubbleX,5,26,20,t.surface);
    rect(bubbleX+4,23,5,3,t.surface);
    for (int n=0;n<3;++n) rect(bubbleX+5+n*6,14,3,3,t.text);
    if (chat.unread && !chat.hideNotifications) {
        roundedRect(bubbleX+16,3,12,12,t.accent);
        text(bubbleX+18,2,chat.unread>9?"+":std::to_string(chat.unread),t.background);
    }
    drawLivePlayerPanels(accountGames.games[previewIndex],liveBoard?&*liveBoard:nullptr,
                         themeId,dual,milliseconds(),32);
    if (liveBoard && liveBoard->finished && !onlineMenu) {
        text(16,dual?105:117,liveBoard->resultLabel(),t.heading);
        for (int n=0;n<3;++n) button(16,resultTop+n*resultRow,width,29,resultOptions[n],t,n==resultOption,n==0);
        return;
    }
    if (!promotion.empty()) {
        for (int n=0;n<4;++n)
            button(16,top+n*controlsRow(dual),width,controlsRow(dual)-1,names[n],t,n==promoOption);
        return;
    }
    if (onlineMenu) {
        if (confirmingResign) {
            button(16,top,width,28,"Cancel resignation",t,actionOption==0);
            button(16,top+29,width,28,"Confirm resignation",t,actionOption==1);
        } else {
            const auto labels=menuItems();
            const int first=firstVisible(option,3,int(labels.size()));
            for (int n=first;n<int(labels.size()) && n<first+3;++n)
                button(16,top+(n-first)*29,width,28,labels[n],t,option==n);
        }
    } else {
        button(16,top,width,28,"...  Match menu",t,false);
        if (liveBoard) {
            const auto& history=liveBoard->history;
            const int end=int(history.size());
            int line=0;
            for (int n=std::max(0,end-6);n<end;++n,++line)
                text(16+(line%2)*width/2,top+43+14*(line/2),
                    std::to_string(n/2+1)+(n%2?"... ":". ")+notation(history[n]),t.text);
        }
    }
    std::string status=liveBoard && liveBoard->finished?liveBoard->resultLabel():
        checkingAccount && streamSynchronized?"Connected to Lichess":"Waiting for connection";
    if (liveBoard && !liveBoard->finished) {
        const auto player=accountGames.games[previewIndex].player, opponent=opposite(player);
        if (liveBoard->drawOffer[opponent] && liveBoard->takebackOffer[opponent]) status="Draw and takeback offered";
        else if (liveBoard->drawOffer[opponent]) status="Opponent offers a draw";
        else if (liveBoard->takebackOffer[opponent]) status="Opponent requests takeback";
        else if (liveBoard->drawOffer[player]) status="Draw offer sent";
        else if (liveBoard->takebackOffer[player]) status="Takeback requested";
    }
    if (onlineRequest.blocked()) {
        switch (onlineRequest.result()) {
            case PostResult::Sending: status="Sending request..."; break;
            case PostResult::Uncertain: status="Delivery uncertain. Synchronizing."; break;
            case PostResult::Rejected: status="Request refused. Synchronizing."; break;
            case PostResult::RateLimited: status="Rate limited. Reconnect in a minute."; break;
            default: status="Confirming server board..."; break;
        }
    }
    text(16,dual?198:228,status,t.heading);
    text(16,dual?212:242,dual?"A select / play    B cancel":"Cross select  Square cancel",t.secondary);
    text(16,dual?226:256,notice,t.secondary);
}
void App::drawMenu(bool dual) const {
    if (page==LichessLobby) { drawLobby(dual); return; }
    const auto& t=palette(themeId);
    const auto items=menuItems();
    const int width=dual?288:448;
    const char* title=appTitle;
    if (page==Computer) title="Play computer";
    else if (page==HostSetup) title="Host LAN match";
    else if (page==JoinSetup) title="Join LAN match";
    else if (page==LichessSetup) title="Lichess setup";
    else if (page==LichessGames) title="Active games";
    else if (page==Appearance) title="Appearance";
    text(16,12,title,t.text,3);
    if (page==LichessGames) {
        const int games=activeGameRows();
        for (int n=0;n<games;++n) {
            const int y=menuTop+n*menuRow;
            const auto& game=accountGames.games[n];
            setTextRight(16+width-86);
            button(16,y,width,28,game.opponent,t,n==option);
            setTextRight(16+width);
            text(16+width-79,y+9,game.yourTurn?"Your turn":"Waiting",
                 game.yourTurn?t.active:t.secondary);
        }
        const int half=width/2-4;
        button(16,activeFooter,half,28,"Refresh",t,option==games);
        button(16+half+8,activeFooter,half,28,"Back",t,option==games+1);
        text(16,226,notice,t.heading);
        return;
    }
    for (int n=0;n<int(items.size());++n) {
        const int y=menuTop+n*menuRow;
        const auto label=page==Appearance && n==int(themeId)?items[n]+"  [Active]":items[n];
        const bool primaryRow=(page==Home && n==0) || (page==Computer && n==3) ||
            (page==HostSetup && n==2) || (page==JoinSetup && n==1) ||
            (page==LichessSetup && !confirmingSignOut && n==0);
        button(16,y,width,28,label,t,n==option,primaryRow);
    }
    if (page==JoinSetup) {
        const auto ip=address();
        size_t start=0;
        for (int n=0;n<edit;++n) start=ip.find('.',start)+1;
        const auto end=ip.find('.',start);
        const auto before="Host IP: "+ip.substr(0,start);
        const auto octet=ip.substr(start,end==std::string::npos?end:end-start);
        rect(26+textWidth(before,2),menuTop+22,textWidth(octet,2),2,t.accent);
        text(16,150,"Left/right edits octet. Select a number with A or Cross.",t.secondary);
        text(16,166,"Hold Start for steps of ten.",t.secondary);
    }
    if (page==Appearance) {
        text(16,151,"Select toggles appearance during play.",t.secondary);
        text(16,167,appearanceSaved?"Saved on this console.":"Could not save this setting.",t.secondary);
    }
    text(16,226,notice,t.heading);
}
void App::drawInfo(bool dual) const {
    const auto& t=palette(themeId);
    const auto& m=match();
    const auto& g=m.game;
    const int width=dual?288:192;
    text(16,11,appTitle,t.text,2);
    for (int n=0;n<2;++n) {
        const auto player=n?opposite(side()):side();
        const bool active=g.result==Result::Playing && g.pos.turn==player;
        const auto seconds=(uint64_t(m.remaining[player])+999)/1000;
        const bool urgent=active && m.initialMs && seconds<=30;
        const auto clock=m.initialMs?std::to_string(seconds/60)+":"+
            (seconds%60<10?"0":"")+std::to_string(seconds%60):std::string("Untimed");
        const int x=16+(dual?n*147:0), y=32+(dual?0:n*33);
        const int w=dual?141:width, h=dual?42:30;
        const Ink accent=g.result!=Result::Playing?t.secondary:active?t.active:t.waiting;
        roundedRect(x,y,w,h,t.surface);
        rect(x+2,y+4,urgent?4:2,h-8,accent);
        const std::string label=n?(lan.role==Lan::Off?"Computer":"Opponent"):"You";
        text(x+9,y+4,label+" / "+name(player),t.text);
        const auto turn=g.result!=Result::Playing?"Finished":active?(n?"Their turn":"Your turn"):"Waiting";
        text(x+9,y+(dual?29:17),turn,accent);
        const auto digits=(urgent?"! ":"")+clock;
        const int clockX=dual?x+9:x+w-8-textWidth(digits,4);
        text(clockX,y+(dual?11:5),digits,accent,4);
    }
    std::string status;
    if (g.result!=Result::Playing) status=std::string(resultName(g.result))+(m.flagFall?" on time":"");
    else if (lan.role!=Lan::Off && !lan.ready) status=lan.status;
    else if (working) status="Computer thinking";
    else if (g.pos.check(g.pos.turn)) status=std::string(name(g.pos.turn))+" in check";
    else status=std::string(name(g.pos.turn))+" to move";
    const int y=dual?77:100;
    text(16,y,status,t.text);
    std::string detail=notice;
    if (m.takeback>=0) detail=m.takeback==side()?"Takeback requested":"Opponent requests takeback";
    else if (m.offer==opposite(side())) detail="Opponent offers a draw";
    else if (m.rematch&(1u<<side())) detail="Waiting for rematch";
    else if (lan.role==Lan::Host && !lan.ready && !lan.address.empty()) detail="Host IP: "+lan.address;
    text(16,y+13,detail,t.heading);
}
void App::drawBoard(bool dual,const ActiveGame* preview) const {
    const auto& t=palette(themeId);
    const int tile=dual?28:32,bx=dual?88:216,by=8;
    const auto& g=match().game;
    const auto& p=preview?(liveBoard?liveBoard->position:*preview->position):
        variation?*variation:reviewing?reviewPosition:g.pos;
    const auto viewSide=preview?preview->player:side();
    const size_t shownPlies=preview || variation?0:reviewing?reviewPly:g.history.size();
    const auto lastMove=preview?(liveBoard?liveBoard->lastMove:std::nullopt):
        shownPlies?std::optional<Move>(g.history[shownPlies-1]):std::nullopt;
    outline(bx-2,by-2,8*tile+4,8*tile+4,t.border);
    std::vector<Move> moves;
    if (selected>=0 && (preview?canPlayOnline():variation.has_value() || canMove())) moves=p.legal();
    const bool checked=p.check(p.turn);
    setTextRight(dual?396:480);
    for (int cell=0;cell<64;++cell) {
        const int col=cell%8,row=cell/8,sq=cell^(viewSide==White?56:7);
        const int x=bx+col*tile,y=by+row*tile;
        Ink square=(col+row)%2?boardDark:boardLight;
        if (lastMove && (lastMove->to==sq || lastMove->from==sq))
            square=(col+row)%2?rgba(149,151,83):rgba(225,202,122);
        if (checked && p.board[sq]==(p.turn==White?King:-King)) square=rgba(193,105,92);
        if (sq==selected) square=rgba(176,203,99);
        rect(x,y,tile,tile,square);
        piece(x,y,tile,p.board[sq]);
        const bool target=std::any_of(moves.begin(),moves.end(),[&](Move m){return m.from==selected && m.to==sq;});
        if (target) {
            if (p.board[sq]) captureRing(x+tile/2,y+tile/2,tile/2-2,rgba(44,65,28));
            else roundedRect(x+tile/2-3,y+tile/2-3,7,7,rgba(70,94,49));
        }
        if (col==0) text(x+1,y+1,std::string(1,char('1'+sq/8)),(col+row)%2?boardLight:rgba(53,76,33));
        if (row==7) text(x+tile-7,y+tile-11,std::string(1,char('a'+sq%8)),(col+row)%2?boardLight:rgba(53,76,33));
        if ((preview?liveBoard && !liveBoard->finished:variation.has_value() || g.result==Result::Playing) && cursor==sq)
            outline(x,y,tile,tile,rgba(251,248,219));
    }
}
void App::drawResults(bool dual) const {
    const auto& t=palette(themeId);
    if (variation) {
        if (!promotion.empty()) {
            drawControls(dual);
            return;
        }
        text(16,100,"Variation",t.heading,2);
        text(16,122,std::string(name(variation->turn))+" to move",t.text);
        text(16,145,dual?"A: select / play":"Cross: select / play",t.secondary);
        if (dual) rect(16,174,288,30,t.surface);
        text(24,185,dual?"Return to game line":"Square: game line",t.text);
        return;
    }
    if (reviewing) {
        const auto& history=match().game.history;
        text(16,100,"Analysis board",t.heading,2);
        text(16,119,"Ply "+std::to_string(reviewPly)+"/"+std::to_string(history.size())+
             (reviewPly?"  "+notation(history[reviewPly-1]):"  Start"),t.text);
        if (dual) {
            rect(16,134,140,30,t.surface);
            rect(164,134,140,30,t.surface);
            rect(16,174,288,30,t.surface);
            text(24,145,"< Previous",t.text);
            text(172,145,"Next >",t.text);
            text(24,185,"Back to results",t.text);
            button(16,207,288,28,"Try a variation",t,false,true);
        } else {
            text(16,145,"Left / right: step",t.secondary);
            text(16,185,"Square: results",t.secondary);
            text(16,215,"Cross: try a variation",t.text);
        }
        return;
    }
    for (int n=0;n<3;++n) {
        const int y=resultTop+n*resultRow;
        button(16,y,dual?288:192,29,resultOptions[n],t,n==resultOption,n==0);
    }
}
void App::drawReviewSearch(bool dual) const {
    const auto& t=palette(themeId);
    text(16,13,"Engine review",t.heading,2);
    if (analyzing) {
        text(16,36,"Searching...",t.text);
    } else if (reviewChoice) {
        const auto& hint=*reviewChoice;
        if (hint.claim) text(16,36,"Draw claim available",t.text);
        else if (hint.valid) {
            std::string line="Try";
            for (unsigned n=0;n<hint.line.length;++n) line+=" "+notation(hint.line.moves[n]);
            text(16,36,line,t.text);
        }
        else text(16,36,"No suggestion available",t.text);
        if (hint.scored) {
            const auto turn=variation?variation->turn:reviewPosition.turn;
            const int score=turn==White?hint.score:-hint.score;
            const int magnitude=score<0?-score:score;
            std::string evaluation="Even";
            if (magnitude>=29900) evaluation=score>0?"Mate for White":"Mate for Black";
            else if (magnitude)
                evaluation=std::string(score>0?"White +":"Black +")+
                    std::to_string(magnitude/100)+"."+
                    (magnitude%100<10?"0":"")+std::to_string(magnitude%100);
            text(16,86,evaluation,t.text);
        }
    }
    if (dual) rect(16,52,288,27,t.surface);
    text(24,62,dual?"Analyze position":"Start: analyze position",t.text);
}
void App::drawControls(bool dual) const {
    const auto& t=palette(themeId);
    const int x=16,y=controlsTop(dual),row=controlsRow(dual), width=dual?288:192;
    if (!variation && lan.timedOut(milliseconds())) {
        button(x,145,width,28,"Retry connection",t,true);
        button(x,174,width,28,"Leave game",t,false);
        return;
    }
    if (confirmingResign || !promotion.empty() || actions) {
        std::vector<std::string> labels;
        int selectedRow=actionOption;
        if (confirmingResign) labels={"Cancel resignation","Confirm resignation"};
        else if (!promotion.empty()) {
            labels.assign(std::begin(names),std::end(names));
            selectedRow=promoOption;
        } else {
            for (const auto& option:actionList()) labels.push_back(option.first);
            labels.emplace_back("Leave game");
            labels.emplace_back("Close menu");
        }
        const int first=firstVisible(selectedRow,visibleActions(dual),int(labels.size()));
        for (int n=first;n<int(labels.size()) && n<first+visibleActions(dual);++n)
            button(x,y+(n-first)*row,width,row-1,labels[n],t,n==selectedRow);
        return;
    }
    button(x,y,width,row-1,claimMove?"Select a move to claim":"...  Match actions",t,false);
    text(x,y+row+6,"L / R   Move history",t.secondary);
    const auto& history=match().game.history;
    const int end=std::max(0,int(history.size())-historyOffset);
    int line=0;
    for (int n=std::max(0,end-6);n<end;++n) {
        const int col=line%2;
        const int yy=y+row+22+14*(line/2);
        const auto label=std::to_string(n/2+1)+(n%2?"... ":". ")+notation(history[n]);
        text(x+col*(width/2),yy,label,t.text);
        ++line;
    }
    text(x,dual?225:245,dual?"A select    B back    Select theme":"Cross select  Square back",t.secondary);
}
void App::draw(bool dual,bool top) const {
    const auto& t=palette(themeId);
    setTextRight(dual?(top?396:304):(page==Game?208:464));
    rect(0,0,dual?(top?400:320):480,dual?240:272,t.background);
    if (page==LichessPreview) {
        if (!dual || top) drawBoard(dual,&accountGames.games[previewIndex]);
        if (!dual || !top) {
            setTextRight(dual?304:208);
            drawOnline(dual);
        }
        return;
    }
    if (page!=Game) {
        if (dual && top) {
            text(21,42,appTitle,t.heading,3);
            text(22,79,"Computer, local Wi-Fi or Lichess",t.secondary,2);
        } else drawMenu(dual);
        return;
    }
    if (dual && top) drawBoard(true);
    else {
        if (!dual) drawBoard(false);
        setTextRight(dual?304:208);
        if (reviewing) drawReviewSearch(dual);
        else drawInfo(dual);
        if (match().game.result==Result::Playing) drawControls(dual);
        else drawResults(dual);
    }
}
}
