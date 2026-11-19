#ifdef _WIN32
#include <winsock2.h>
#endif
#include "app.hpp"
#include "screen.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <thread>

using namespace chess;
namespace {
#define CHECK(expr) do { if (!(expr)) { std::cerr << "FAIL line " << __LINE__ << ": " << #expr << '\n'; std::exit(1); } } while (0)
Move mv(const char* name) {
    auto square=[](const char* s){return uint8_t((s[1]-'1')*8+s[0]-'a');};
    return {square(name),square(name+2),uint8_t(name[4]?std::string("  nbrq").find(name[4]):0)};
}
Position fen(const char* s) {
    Position p;
    int rank=7,file=0;
    for (;*s!=' ';++s) {
        if (*s=='/') { --rank; file=0; }
        else if (*s>='1' && *s<='8') file+=*s-'0';
        else {
            const auto key=std::string(" PNBRQKpnbrqk").find(*s);
            CHECK(key!=std::string::npos);
            p.board[rank*8+file++]=key<=6?int(key):6-int(key);
        }
    }
    p.turn=*++s=='w'?White:Black;
    p.castle=0;
    for (s+=2;*s!=' ';++s) {
        const auto right=std::string("KQkq").find(*s);
        if (right!=std::string::npos) p.castle|=1u<<right;
    }
    ++s;
    if (*s!='-') p.ep=(s[1]-'1')*8+s[0]-'a';
    return p;
}
GameState from(Position p) {
    GameState g;
    g.pos=p;
    g.positions={p.identity()};
    return g;
}
uint64_t perft(const Position& p,int depth) {
    if (!depth) return 1;
    uint64_t nodes=0;
    for (auto m:p.legal()) nodes+=perft(p.after(m),depth-1);
    return nodes;
}
bool legal(const Position& p,Move m) {
    const auto list=p.legal();
    return std::find(list.begin(),list.end(),m)!=list.end();
}
void chessRules() {
    const auto initial=Position::start();
    CHECK(perft(initial,1)==20);
    CHECK(perft(initial,2)==400);
    CHECK(perft(initial,3)==8902);
    CHECK(perft(initial,4)==197281);
    CHECK(initial.identity()==Position::start().identity());
    const auto complex=fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -");
    CHECK(perft(complex,3)==97862);
    const auto ep=fen("k3r3/8/8/3pP3/8/8/8/4K3 w - d6");
    CHECK(!legal(ep,mv("e5d6")));
    auto noEp=ep;
    noEp.ep=-1;
    CHECK(ep.identity()==noEp.identity());
    const auto unpinned=fen("k7/8/8/3pP3/8/8/8/4K3 w - d6");
    CHECK(legal(unpinned,mv("e5d6")));
    CHECK(unpinned.after(mv("e5d6")).board[35]==0);
    CHECK(unpinned.identity()[66]==43);
    const auto castle=fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq -");
    CHECK(legal(castle,mv("e1g1")));
    CHECK(castle.after(mv("e1g1")).board[5]==Rook);
    CHECK(castle.after(mv("a1a8")).castle==5);
    const auto through=fen("r3kr2/8/8/8/8/8/8/R3K2R w KQq -");
    CHECK(!legal(through,mv("e1g1")));
    CHECK(legal(through,mv("e1c1")));
    const auto promote=fen("4k3/P7/8/8/8/8/8/4K3 w - -");
    for (auto s:{"a7a8n","a7a8b","a7a8r","a7a8q"}) {
        auto m=mv(s);
        CHECK(legal(promote,m));
        CHECK(promote.after(m).board[56]==m.promotion);
    }
    CHECK(!legal(promote,mv("a7a8")));

    GameState repeat;
    const auto cycle={"g1f3","g8f6","f3g1","f6g8"};
    for (int i=0;i<4;++i) {
        for (auto s:cycle) {
            const Move move=mv(s);
            if (i==1 && std::string(s)=="f6g8") CHECK(repeat.canClaim(&move));
            CHECK(repeat.play(move));
        }
        if (i==1) CHECK(repeat.canClaim() && repeat.result==Result::Playing);
    }
    CHECK(repeat.result==Result::Draw);
    auto longGame=from(castle);
    longGame.pos.quiet=99;
    const auto intend=mv("h1h2");
    CHECK(longGame.canClaim(&intend));
    CHECK(longGame.play(intend) && longGame.canClaim() && longGame.result==Result::Playing);
    longGame.pos.quiet=149;
    CHECK(longGame.play(mv("h8h7")) && longGame.result==Result::Draw);
    auto mate=from(fen("7k/8/5KQ1/8/8/8/8/8 w - -"));
    mate.pos.quiet=149;
    CHECK(mate.play(mv("g6g7")) && mate.result==Result::WhiteWins);
    auto stale=from(fen("7k/5Q2/6K1/8/8/8/8/8 b - -"));
    stale.updateResult();
    CHECK(stale.result==Result::Draw);
    CHECK(fen("4k3/8/8/8/8/8/8/2B1K3 w - -").dead());
    CHECK(!fen("4k3/8/8/8/8/8/8/1NN1K3 w - -").dead());
    CHECK(fen("4k3/8/8/8/8/4b3/8/2B1K3 w - -").dead());
    CHECK(!fen("4k3/8/8/8/4b3/8/8/2B1K3 w - -").dead());
    std::cout << "PASS move generation, castling, en passant, promotions and draw rules\n";
}
void codec() {
    Match host,guest;
    host.host=Black;
    host.setClock(900000,10000);
    CHECK(!host.elapse(2000,true));
    CHECK(host.apply(White,{0,1,Action::Move,mv("e2e4")},true));
    CHECK(!host.apply(White,{0,1,Action::Move,mv("e2e4")},true));
    CHECK(!host.apply(White,{1,2,Action::Move,mv("d2d4")},true));
    CHECK(host.apply(Black,{1,1,Action::Move,mv("e7e5")},false));
    CHECK(host.apply(White,{2,3,Action::Offer,{}},true));
    CHECK(host.apply(Black,{3,2,Action::Accept,{}},false));
    CHECK(host.game.result==Result::Draw);
    const auto state=stateMessage(host,54321);
    const auto packet=frame(state);
    Framer framer;
    std::vector<Bytes> packets;
    for (const auto byte:packet) CHECK(framer.feed(&byte,1,packets));
    CHECK(packets.size()==1 && packets[0]==state);
    uint64_t token=0;
    CHECK(readState(packets[0],guest,token));
    CHECK(guest.remaining[White]==908000 && guest.remaining[Black]==910000);
    CHECK(token==54321 && guest.host==Black && guest.game.history==host.game.history);
    CHECK(guest.game.pos.identity()==host.game.pos.identity() && guest.game.result==Result::Draw);
    auto invalid=state;
    invalid.pop_back();
    CHECK(!readState(invalid,guest,token));
    uint8_t oversize[]={0,1,0,0};
    CHECK(!framer.feed(oversize,sizeof(oversize),packets));
    CHECK(host.apply(White,{4,4,Action::Rematch,{}},true));
    CHECK(host.apply(Black,{5,2,Action::Rematch,{}},false));
    CHECK(readState(stateMessage(host,token),guest,token));
    CHECK(guest.round==2 && guest.host==White && guest.game.history.empty());
    CHECK(guest.remaining[White]==900000 && guest.incrementMs==10000);
    CHECK(!guest.elapse(900000,false));
    CHECK(guest.game.result==Result::Playing);
    CHECK(host.elapse(900000,true));
    CHECK(readState(stateMessage(host,token),guest,token));
    CHECK(guest.flagFall && guest.game.result==Result::BlackWins);
    Match bareKing;
    bareKing.game=from(fen("7k/8/8/8/8/8/R7/K7 w - -"));
    bareKing.setClock(1000,0);
    CHECK(bareKing.elapse(1000,true));
    CHECK(bareKing.flagFall && bareKing.game.result==Result::Draw);
    Match undo;
    undo.setClock(900000,10000);
    CHECK(!undo.elapse(2000,true));
    CHECK(undo.apply(White,{0,0,Action::Move,mv("e2e4")},false));
    CHECK(!undo.elapse(3000,true));
    CHECK(undo.apply(Black,{1,0,Action::Move,mv("e7e5")},false));
    CHECK(undo.undoTo(0));
    CHECK(undo.game.history.empty() && undo.game.positions.size()==1);
    CHECK(undo.game.pos.identity()==Position::start().identity());
    CHECK(undo.remaining[White]==898000 && undo.remaining[Black]==900000);
    CHECK(validAddress("192.168.1.10") && !validAddress("256.1.1.1") && !validAddress("1.2.3"));
    std::cout << "PASS versioned frames, request deduplication, snapshots and rematches\n";
}
uint64_t fakeNow=0;
uint64_t fakeClock() { return fakeNow++; }
void engine() {
    const GameState g;
    std::atomic<bool> cancel{false};
    for (auto difficulty:{Difficulty::Easy,Difficulty::Normal,Difficulty::Hard}) {
        fakeNow=0;
        const auto choice=choose(g,difficulty,fakeClock,cancel);
        CHECK(choice.valid && !choice.claim && legal(g.pos,choice.move));
        CHECK(choice.line.length && choice.line.moves[0]==choice.move);
        auto continuation=g;
        for (unsigned n=0;n<choice.line.length;++n)
            CHECK(continuation.play(choice.line.moves[n]));
        CHECK(fakeNow<=1502);
    }
    cancel=true;
    CHECK(!choose(g,Difficulty::Hard,fakeClock,cancel).valid);
    const auto tactic=from(fen("7k/8/5KQ1/8/8/8/8/8 w - -"));
    cancel=false;
    fakeNow=0;
    const auto choice=choose(tactic,Difficulty::Normal,fakeClock,cancel);
    auto after=tactic;
    CHECK(choice.valid && after.play(choice.move) && after.result==Result::WhiteWins);
    CHECK(choice.scored && choice.score>=29900);
    CHECK(choice.line.length==1 && choice.line.moves[0]==choice.move);
    const auto fullLine=choose(g,Difficulty::Easy,[]{return uint64_t(0);},cancel);
    CHECK(fullLine.line.length>=2);
    std::cout << "PASS legal AI moves, budget, cancellation and mate in one\n";
}
std::thread workers[2];
int screenWidth=480,screenHeight=272,rectangles=0;
std::array<handheld::Ink,480*272> framePixels{};
void capture(const std::string& name) {
    const auto* directory=std::getenv("HANDHELD_CAPTURE_DIR");
    if (!directory || !*directory) return;
    std::ofstream out(std::string(directory)+"/"+name+".ppm",std::ios::binary);
    out<<"P6\n"<<screenWidth<<" "<<screenHeight<<"\n255\n";
    for (int i=0;i<screenWidth*screenHeight;++i) {
        for (unsigned channel=0;channel<3;++channel)
            out.put(char(framePixels[i]>>(channel*8)));
    }
    CHECK(out.good());
}
}
namespace chess {
bool wifiReady(uint64_t) { return true; }
std::string wifiAddress() { return "127.0.0.1"; }
uint64_t sessionSeed() { return 4321; }
}
namespace handheld {
uint64_t milliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool launchWorker(void (*entry)(void*),void* arg,unsigned slot) {
    if (slot>=2 || workers[slot].joinable()) return false;
    workers[slot]=std::thread(entry,arg);
    return true;
}
void joinWorker(unsigned slot) { if (slot<2 && workers[slot].joinable()) workers[slot].join(); }
void rect(int x,int y,int w,int h,Ink ink) {
    CHECK(x>=0 && y>=0 && w>0 && h>0 && x+w<=screenWidth && y+h<=screenHeight);
    for (int row=y;row<y+h;++row)
        std::fill_n(framePixels.data()+row*screenWidth+x,w,ink);
    ++rectangles;
}
void sprite(int x,int y,const Sprite& s,Ink tint) {
    if (x<0 || y<0 || x+s.w>screenWidth || y+s.h>screenHeight)
        std::cerr<<"Sprite outside screen: "<<x<<","<<y<<" "<<s.w<<"x"<<s.h<<" screen "<<screenWidth<<"x"<<screenHeight<<'\n';
    CHECK(x>=0 && y>=0 && x+s.w<=screenWidth && y+s.h<=screenHeight);
    blitSprite(framePixels.data(),screenWidth,screenWidth,screenHeight,x,y,s,tint);
}
}
namespace {
void sockets() {
    using handheld::milliseconds;
#ifdef _WIN32
    WSADATA wsa{};
    CHECK(WSAStartup(MAKEWORD(2,2),&wsa)==0);
#endif
    for (auto color:{White,Black}) {
        Lan host,guest;
        auto now=milliseconds();
        host.begin(Lan::Host,color,{},now);
        host.match.setClock(900000,10000);
        host.tick(now);
        guest.begin(Lan::Guest,White,"127.0.0.1",now);
        auto until=[&](auto done,bool pollGuest=true) {
            const auto deadline=milliseconds()+4500;
            while (!done() && milliseconds()<deadline) {
                auto t=milliseconds();
                host.tick(t);
                if (pollGuest) guest.tick(t);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            CHECK(done());
        };
        until([&]{return host.ready && guest.ready;});
        CHECK(host.side()==color && guest.side()==opposite(color));
        CHECK(guest.match.initialMs==900000 && guest.match.incrementMs==10000);
        now=milliseconds();
        host.interrupt(now);
        guest.interrupt(now);
        CHECK(!host.ready && !guest.ready);
        until([&]{return host.ready && guest.ready;});
        CHECK(host.match.game.history.empty() && guest.match.game.history.empty());
        if (host.side()==White) {
            CHECK(host.submit(Action::Move,mv("e2e4")));
            until([&]{return guest.match.revision==1;});
        }
        const auto before=guest.match.revision;
        CHECK(guest.submit(Action::Move,before?mv("e7e5"):mv("e2e4")));
        guest.tick(milliseconds()); // write the request before the host can reply
        until([&]{return host.match.revision==before+1;},false);
        CHECK(guest.match.revision==before);
        guest.interrupt(milliseconds());
        until([&]{return guest.ready && guest.match.revision==host.match.revision;});
        CHECK(guest.match.game.history==host.match.game.history);
        CHECK(host.match.game.history.size()==before+1);
        CHECK(host.match.game.pos.identity()==guest.match.game.pos.identity());
        CHECK(guest.submit(Action::Takeback));
        until([&]{return host.match.takeback==guest.side() &&
                          guest.match.revision==host.match.revision;});
        CHECK(host.submit(Action::AcceptTakeback));
        until([&]{return guest.match.game.history.size()==before;});
        CHECK(guest.match.game.history==host.match.game.history);
        CHECK(guest.match.game.pos.identity()==host.match.game.pos.identity());
        CHECK(host.match.game.pos.turn==guest.side());
        CHECK(guest.submit(Action::Move,before?mv("d7d5"):mv("d2d4")));
        until([&]{return guest.match.game.history.size()==before+1;});
        CHECK(guest.match.game.history==host.match.game.history);
        CHECK(host.submit(Action::Resign));
        until([&]{return guest.match.game.result!=Result::Playing;});
        CHECK(host.submit(Action::Rematch));
        until([&]{return guest.match.rematch!=0;});
        CHECK(guest.submit(Action::Rematch));
        until([&]{return guest.match.round==2;});
        CHECK(guest.side()==color && guest.match.game.history.empty());
        const std::array<Move,4> line{mv("f2f3"),mv("e7e5"),mv("g2g4"),mv("d8h4")};
        for (size_t ply=0;ply<line.size();++ply) {
            auto& mover=host.match.game.pos.turn==host.side()?host:guest;
            CHECK(mover.submit(Action::Move,line[ply]));
            until([&]{return guest.match.game.history.size()==ply+1;});
            CHECK(host.match.game.history==guest.match.game.history);
            CHECK(host.match.game.pos.identity()==guest.match.game.pos.identity());
        }
        CHECK(host.match.game.result==Result::BlackWins && guest.match.game.result==Result::BlackWins);
        CHECK(host.match.remaining==guest.match.remaining && !host.match.flagFall && !guest.match.flagFall);
        guest.stop();
        host.stop();
    }
#ifdef _WIN32
    WSACleanup();
#endif
    std::cout << "PASS TCP loopback, pre-move reconnect, lost acknowledgment and full timed rematch\n";
}
void userInterface() {
    using namespace handheld;
    const auto* directory=std::getenv("HANDHELD_TEST_DIR");
    CHECK(directory && *directory);
    const std::string settings=std::string(directory)+"/appearance-fixture.txt";
    CHECK(saveTheme(settings.c_str(),ThemeId::Light));
    CHECK(loadTheme(settings.c_str())==ThemeId::Light);
    { std::ofstream invalid(settings); invalid<<"light-but-invalid"; }
    CHECK(loadTheme(settings.c_str())==ThemeId::Dark);
    std::remove(settings.c_str());
    for (bool dual:{false,true}) {
        App app;
        int scene=0;
        auto render=[&]{
            for (const std::string theme:{"dark","light"}) {
                const auto name=std::to_string(scene)+"-"+theme;
                screenWidth=dual?320:480;
                screenHeight=dual?240:272;
                app.draw(dual,false);
                CHECK(framePixels[0]==palette(theme=="dark"?ThemeId::Dark:ThemeId::Light).background);
                capture((dual?"3ds-bottom-":"psp-")+name);
                if (dual) {
                    screenWidth=400;
                    app.draw(true,true);
                    capture("3ds-top-"+name);
                }
                app.update({AppearanceKey},dual);
            }
            ++scene;
        };
        render();
        for (int n=0;n<4;++n) app.update({Down},dual);
        app.update({Confirm},dual); // Appearance
        render();
        app.update({Back},dual);
        app.update({Confirm},dual);
        render(); // computer setup
        app.update({Down},dual);
        app.update({Down},dual);
        app.update({Down},dual);
        app.update({Confirm},dual);
        render();
        if (dual) {
            app.update({Confirm},true);
            app.update({Up},true);
            app.update({Up},true);
            app.update({Confirm},true);
        } else {
            app.update({Confirm},false); // cursor begins on e2
            app.update({Up},false);
            app.update({Up},false);
            app.update({Confirm},false);
        }
        render();
        app.update({Menu},dual);
        render();
        app.update({Down},dual); // takeback -> resign
        app.update({Confirm},dual);
        app.update({Down},dual); // cancel -> confirm resignation
        app.update({Confirm},dual);
        render();
        app.update({Up},dual); // rematch -> analysis
        app.update({Confirm},dual);
        app.update({Menu},dual); // start analysis on a copy
        render();
        app.update({Left},dual);
        app.update({Confirm},dual); // explore a variation from the opening
        app.update({Left},dual); // e2 -> d2
        app.update({Confirm},dual);
        app.update({Up},dual);
        app.update({Up},dual);
        app.update({Confirm},dual); // try d2-d4 instead of e2-e4
        render();
        app.update({Back},dual); // variation -> recorded line
        render();
        app.update({Right},dual);
        render();
        app.update({Back},dual); // analysis -> results
        app.update({Up},dual); // analysis -> new opponent
        app.update({Confirm},dual);
        app.update({Back},dual); // setup -> home
        app.update({Back},dual);
        CHECK(app.quit);
    }
    CHECK(rectangles>3000);
    std::cout << "PASS native-size rendering, appearance persistence and interface navigation\n";
}
void lichessProfile() {
    using handheld::accountUsername;
    CHECK(accountUsername(R"({"profile":{"username":"Wrong"},"username":"Chess_Player-42"})")==
          "Chess_Player-42");
    CHECK(accountUsername("{\"username\":\"Knight\"} \n")=="Knight");
    CHECK(accountUsername(R"({"username":"wrong"} trailing)").empty());
    CHECK(accountUsername(R"({"username":"bad\nname"})").empty());
    CHECK(accountUsername(R"({"username":12})").empty());
    CHECK(accountUsername(R"({"username":"Knight","number":1e999})").empty());
    CHECK(accountUsername(R"({"profile":{"username":"Wrong"}})").empty());
    CHECK(accountUsername(std::string(17,'[')+std::string(17,']')).empty());
    CHECK(accountUsername(std::string(32769,' ')).empty());
    std::cout << "PASS bounded Lichess profile response and username validation\n";
}
void lichessActiveGames() {
    using handheld::parseActiveGames;
    const auto games=parseActiveGames(R"({"nowPlaying":[
        {"opponent":{"username":"Mary"},"isMyTurn":true,"color":"black",
         "fen":"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
         "variant":{"key":"standard"}},
        {"opponent":{"username":"Stockfish level 1","id":null},"isMyTurn":false}
    ]})");
    CHECK(games && games->size()==2);
    CHECK((*games)[0].opponent=="Mary" && (*games)[0].yourTurn);
    CHECK((*games)[1].opponent=="Stockfish level 1" && !(*games)[1].yourTurn);
    const auto& preview=(*games)[0];
    CHECK(preview.player==Black && preview.position);
    CHECK(preview.position->board[28]==Pawn && preview.position->board[12]==Empty);
    CHECK(preview.position->turn==Black && preview.position->ep==20);
    CHECK(!(*games)[1].position);
    CHECK(!Position::fromFen("8/8/8/8/8/8/8/8 w - - 0 1"));
    CHECK(!Position::fromFen("7k/8/8/8/8/8/8/K8 w - - 0 1"));
    CHECK(!Position::fromFen("7k/8/8/8/8/8/8/K7 w - - 999999 1"));
    const auto empty=parseActiveGames(R"({"nowPlaying":[]})");
    CHECK(empty && empty->empty());
    CHECK(!parseActiveGames(R"({"nowPlaying":[{"opponent":{"username":"Mary"},"isMyTurn":"true"}]})"));
    CHECK(!parseActiveGames("{}"));
    std::cout << "PASS Lichess active games, board snapshots and invalid responses\n";
}
void lichessGameStream() {
    using handheld::GameStreamDecoder;
    using handheld::LiveBoard;
    const auto receive=[](void* out,const LiveBoard& board) {
        static_cast<std::vector<LiveBoard>*>(out)->push_back(board);
    };
    GameStreamDecoder stream("AbC12345");
    std::vector<LiveBoard> boards;
    const std::string_view events=
        R"({"type":"gameFull","id":"AbC12345","variant":{"key":"standard"},"initialFen":"startpos","clock":{"initial":900000,"increment":10000},"state":{"moves":"e2e4","status":"started","wtime":900000,"btime":900000,"winc":10000,"binc":10000}})" "\n\n"
        R"({"type":"chatLine","text":"Ignored in the preview"})" "\r\n"
        R"({"type":"gameState","moves":"e2e4 e7e5","status":"started","wtime":900000,"btime":910000,"winc":10000,"binc":10000})" "\n"
        R"({"type":"gameState","moves":"e2e4","status":"started","wtime":900000,"btime":900000,"winc":10000,"binc":10000})" "\n"
        R"({"type":"gameState","moves":"e2e4 d7d5","status":"resign","winner":"white","wtime":895000,"btime":907000,"winc":10000,"binc":10000})" "\n";
    for (size_t start=0;start<events.size();start+=3)
        CHECK(stream.feed(events.substr(start,3),receive,&boards));
    CHECK(boards.size()==4 && stream.complete());
    CHECK(boards[0].position.board[28]==Pawn && boards[0].position.turn==Black);
    CHECK(boards[1].position.board[36]==-Pawn && boards[1].position.turn==White);
    CHECK(boards[2].position.board[36]==Empty && boards[2].position.turn==Black);
    CHECK(boards[3].lastMove==mv("d7d5") && boards[3].finished);
    CHECK(boards[0].realtimeClock && !boards[0].clockRunning && boards[0].plies==1);
    CHECK(boards[0].remainingAt(Black,5000)==900000);
    CHECK(boards[1].clockRunning && boards[1].plies==2 && boards[1].incrementMs[Black]==10000);
    CHECK(boards[1].remainingAt(White,5000)==895000 && boards[1].remainingAt(Black,5000)==910000);
    CHECK(!boards[2].clockRunning && boards[2].remainingMs[Black]==900000 && boards[2].plies==1);
    CHECK(boards[3].winner==White && boards[3].resultLabel()=="White wins / Resigned");
    CHECK(!boards[3].clockRunning && boards[3].remainingAt(White,999999)==895000);
    auto clock=boards[1];
    clock.receivedAt=2000;
    CHECK(clock.remainingAt(White,1000)==900000); // Monotonic timer reset must not underflow.
    CHECK(clock.remainingAt(White,1002000)==0 && !clock.finished); // Only Lichess declares a flag fall.
    clock.remainingMs[White]=10000;
    CHECK(clock.remainingAt(White,4000)==8000);
    // Exercise the same clock panels used by the live preview, without a credential.
    for (const bool dual:{false,true}) for (const auto theme:{handheld::ThemeId::Dark,handheld::ThemeId::Light})
        for (const auto player:{White,Black}) {
            screenWidth=dual?320:480;
            screenHeight=dual?240:272;
            handheld::rect(0,0,screenWidth,screenHeight,handheld::palette(theme).background);
            handheld::ActiveGame game;
            game.opponent="Long_opponent_name_for_clipping";
            game.player=player;
            handheld::drawLivePlayerPanels(game,&clock,theme,dual,4000);
            const auto label=std::string("live-clocks-")+(dual?"3ds-":"psp-")+
                (theme==handheld::ThemeId::Dark?"dark-":"light-")+(player==White?"white":"black");
            capture(label);
            handheld::drawLivePlayerPanels(game,&boards[3],theme,dual,4000);
            handheld::drawLivePlayerPanels(game,nullptr,theme,dual,4000);
        }

    GameStreamDecoder reconnected("AbC12345");
    const std::string full=R"({"type":"gameFull","id":"AbC12345","variant":{"key":"standard"},"initialFen":"startpos","clock":{"initial":900000,"increment":10000},"state":{"moves":"e2e4 e7e5","status":"started","wtime":870000,"btime":880000,"winc":10000,"binc":10000}})" "\n";
    CHECK(reconnected.feed(full,receive,&boards));
    CHECK(boards.back().remainingMs[White]==870000 && boards.back().clockRunning);
    CHECK(reconnected.feed(R"({"type":"gameState","moves":"e2e4 e7e5","status":"outoftime","wtime":0,"btime":880000,"winc":10000,"binc":10000})" "\n",receive,&boards));
    CHECK(boards.back().resultLabel()=="Draw / On time" && !boards.back().winner);
    CHECK(boards.back().remainingAt(Black,999999)==880000);

    for (const std::string bad:{"-1","0.5","2147483648","\"900000\"","null"}) {
        auto malformed=full;
        malformed.replace(malformed.find("\"wtime\":")+8,6,bad);
        GameStreamDecoder invalidClock("AbC12345");
        CHECK(!invalidClock.feed(malformed,receive,&boards));
    }
    for (const std::string bad:{"\"surprise\"","false","null"}) {
        auto malformed=full;
        malformed.replace(malformed.find("\"status\":")+9,9,bad);
        GameStreamDecoder invalidStatus("AbC12345");
        CHECK(!invalidStatus.feed(malformed,receive,&boards));
    }
    for (const std::string bad:{"\"green\"","true","5"}) {
        GameStreamDecoder invalidWinner("AbC12345");
        CHECK(invalidWinner.feed(full,receive,&boards));
        CHECK(!invalidWinner.feed(std::string(R"({"type":"gameState","moves":"e2e4 e7e5","status":"resign","wtime":1,"btime":2,"winc":0,"binc":0,"winner":)")+bad+"}\n",receive,&boards));
    }

    GameStreamDecoder castling("AbC12345");
    CHECK(castling.feed(R"({"type":"gameFull","id":"AbC12345","variant":{"key":"standard","name":"Standard"},"initialFen":"4k3/8/8/8/8/8/8/4K2R w K - 0 1","state":{"moves":"e1h1","status":"started","wtime":2147483647,"btime":2147483647,"winc":0,"binc":0}})"
                        "\n",receive,&boards));
    CHECK(boards.back().position.board[6]==King && boards.back().position.board[5]==Rook);
    CHECK(!boards.back().realtimeClock && boards.back().remainingAt(Black,999999)==2147483647);
    CHECK(!castling.feed(R"({"type":"gameState","moves":"e1h1 e8e2","status":"started","wtime":0,"btime":0,"winc":0,"binc":0})"
                         "\n",receive,&boards));
    GameStreamDecoder invalid("AbC12345");
    CHECK(!invalid.feed(std::string(32769,'x'),receive,&boards));
    std::cout << "PASS Lichess live board, fragmented messages, takebacks, castling, clocks and server results\n";
}
void lichessMoves() {
    using namespace handheld;
    CHECK(boardMovePath("AbC12345",mv("e2e4"))=="/api/board/game/AbC12345/move/e2e4");
    for (const char* uci:{"a7a8n","a7a8b","a7a8r","a7a8q"})
        CHECK(boardMovePath("AbC12345",mv(uci))==std::string("/api/board/game/AbC12345/move/")+uci);
    CHECK(boardMovePath("AbC12345",mv("e1g1"))=="/api/board/game/AbC12345/move/e1g1");
    CHECK(!boardMovePath("../oops",mv("e2e4")));
    CHECK(!boardMovePath("AbC12345",Move{64,0,0}));
    CHECK(!boardMovePath("AbC12345",Move{12,28,Pawn}));
    CHECK(postReply(200,R"({"ok":true})",true)==PostResult::Accepted);
    CHECK(postReply(200,R"({"ok":true})",false)==PostResult::Uncertain);
    CHECK(postReply(200,R"({"ok":false})",true)==PostResult::Uncertain);
    CHECK(postReply(200,"bad JSON",true)==PostResult::Uncertain);
    CHECK(postReply(500,"",true)==PostResult::Uncertain);
    CHECK(postReply(400,"",false)==PostResult::Rejected);
    CHECK(postReply(403,"",false)==PostResult::Rejected);
    CHECK(postReply(429,"",false)==PostResult::RateLimited);
    for (const auto reply:{PostResult::Accepted,PostResult::Rejected,
                          PostResult::Uncertain,PostResult::RateLimited}) {
        LiveBoard board;
        board.position=Position::start();
        board.status="started";
        OnlineRequestGate gate;
        CHECK(!gate.begin(board,White,mv("e2e4"),false));
        CHECK(!gate.begin(board,Black,mv("e7e5"),true));
        CHECK(!gate.begin(board,White,mv("e2e5"),true));
        CHECK(gate.begin(board,White,mv("e2e4"),true)==1);
        CHECK(!gate.begin(board,White,mv("d2d4"),true));
        gate.complete(reply);
        CHECK(gate.blocked()); // A POST acknowledgment is not an authoritative board.
        gate.synchronize(0);
        CHECK(gate.blocked()); // Old stream events cannot release a new request.
        gate.synchronize(1);
        CHECK(!gate.blocked() && gate.result()==reply);
        CHECK(gate.begin(board,White,mv("d2d4"),true)==2);
        gate.synchronize(2); // Snapshot can arrive before the main loop reads the POST reply.
        CHECK(gate.blocked());
        gate.complete(reply);
        CHECK(!gate.blocked());
        board.finished=true;
        CHECK(!gate.begin(board,White,mv("e2e4"),true));
    }
    std::cout << "PASS online move encoding, response handling and mandatory resynchronization\n";
}
void lichessActions() {
    using namespace handheld;
    const std::pair<BoardAction,const char*> paths[]={
        {BoardAction::OfferDraw,"draw/yes"},{BoardAction::AcceptDraw,"draw/yes"},
        {BoardAction::DeclineDraw,"draw/no"},{BoardAction::RequestTakeback,"takeback/yes"},
        {BoardAction::AcceptTakeback,"takeback/yes"},{BoardAction::DeclineTakeback,"takeback/no"},
        {BoardAction::Resign,"resign"}};
    for (const auto& path:paths) {
        CHECK(boardActionPath("AbC12345",path.first)==std::string("/api/board/game/AbC12345/")+path.second);
        CHECK(!boardActionPath("../oops",path.first));
    }
    CHECK(!boardActionPath("AbC12345",BoardAction::Move));
    for (const auto player:{White,Black}) {
        LiveBoard board;
        board.position=Position::start();
        board.status="started";
        CHECK(!boardActionAvailable(board,player,BoardAction::OfferDraw));
        CHECK(!boardActionAvailable(board,player,BoardAction::RequestTakeback));
        board.history={mv("e2e4"),mv("e7e5")};
        board.plies=2;
        board.position=board.position.after(board.history[0]).after(board.history[1]);
        CHECK(boardActionAvailable(board,player,BoardAction::OfferDraw));
        CHECK(boardActionAvailable(board,player,BoardAction::RequestTakeback));
        CHECK(!boardActionAvailable(board,player,BoardAction::AcceptDraw));
        CHECK(!boardActionAvailable(board,player,BoardAction::AcceptTakeback));
        board.drawOffer[opposite(player)]=true;
        board.takebackOffer[opposite(player)]=true;
        CHECK(!boardActionAvailable(board,player,BoardAction::OfferDraw));
        CHECK(!boardActionAvailable(board,player,BoardAction::RequestTakeback));
        for (const auto action:{BoardAction::AcceptDraw,BoardAction::DeclineDraw,BoardAction::AcceptTakeback,
                                BoardAction::DeclineTakeback,BoardAction::Resign}) {
            CHECK(boardActionAvailable(board,player,action)); // Includes actions on the opponent's turn.
            OnlineRequestGate gate;
            CHECK(!gate.beginAction(board,player,action,false));
            CHECK(gate.beginAction(board,player,action,true)==1);
            CHECK(!gate.beginAction(board,player,BoardAction::Resign,true));
            CHECK(!gate.begin(board,White,mv("g1f3"),true));
            gate.complete(PostResult::Uncertain);
            CHECK(gate.blocked());
            gate.synchronize(1);
            CHECK(!gate.blocked());
        }
        board.finished=true;
        for (const auto& path:paths) CHECK(!boardActionAvailable(board,player,path.first));
    }
    const auto receive=[](void* context,const LiveBoard& board) {
        *static_cast<LiveBoard*>(context)=board;
    };
    LiveBoard board;
    GameStreamDecoder decoder("AbC12345");
    const std::string full=R"({"type":"gameFull","id":"AbC12345","variant":{"key":"standard"},"initialFen":"startpos","state":{"moves":"e2e4 e7e5","status":"started","wtime":10000,"btime":20000,"winc":0,"binc":0,"wdraw":true,"bdraw":false,"wtakeback":false,"btakeback":true}})" "\n";
    CHECK(decoder.feed(full,receive,&board));
    CHECK(board.drawOffer[White] && !board.drawOffer[Black] && !board.takebackOffer[White] && board.takebackOffer[Black]);
    CHECK(decoder.feed(R"({"type":"gameState","moves":"e2e4","status":"started","wtime":30000,"btime":40000,"winc":0,"binc":0})" "\n",receive,&board));
    CHECK(!board.drawOffer[White] && !board.takebackOffer[Black]); // Omitted flags clear previous offers.
    CHECK(board.history.size()==1 && board.remainingMs[White]==30000 && board.remainingMs[Black]==40000);
    for (const char* field:{"wdraw","bdraw","wtakeback","btakeback"}) {
        auto malformed=full;
        const std::string key=std::string("\"")+field+"\":";
        const auto value=malformed.find(key)+key.size(), end=malformed.find_first_of(",}",value);
        malformed.replace(value,end-value,"null");
        GameStreamDecoder invalid("AbC12345");
        CHECK(!invalid.feed(malformed,receive,&board));
    }
    std::cout << "PASS online action paths, offer flags, eligibility and request serialization\n";
}
enum class FakeSeek { Pending, Closed, Matched, RateLimited };
struct FakeOnline {
    handheld::LiveBoard board;
    Color player=White;
    handheld::PostResult result=handheld::PostResult::Accepted;
    bool applyMove=true;
    std::atomic<unsigned> streams{0}, posts{0};
    std::atomic<unsigned> actions{0};
    handheld::BoardAction lastAction=handheld::BoardAction::Move;
    std::atomic<bool> releasePost{false};
    std::atomic<bool> incomingOffer{false}, offerDelivered{false};
    std::vector<handheld::ChatMessage> chatHistory{{"Opponent","Good game"},{"Opponent","Good game"}};
    std::atomic<unsigned> historyCalls{0},chatSends{0},eventConnections{0},lobbyCalls{0},seekCancellations{0};
    handheld::PostResult chatResult=handheld::PostResult::Accepted;
    std::string sentChat;
    handheld::LobbyRequest lastLobby;
    handheld::FriendRequest lastFriendRequest;
    std::atomic<unsigned> friendCalls{0};
    std::vector<handheld::Friend> followed;
    handheld::AccountCheck friendStatus=handheld::AccountCheck::Verified;
    bool lostFriendReply=false;
    std::atomic<FakeSeek> seek{FakeSeek::Pending};
    std::atomic<bool> gameReady{false},injectMove{false},moveDelivered{false},finishGame{false},endDelivered{false};
    bool initialChallenge=false;
};
FakeOnline* fakeOnline=nullptr;
bool fakeAvailable() { return true; }
handheld::AccountProfile fakeVerify(const std::string&,const char*,const std::atomic<bool>&) {
    return {handheld::AccountCheck::Verified,"You"};
}
handheld::ActiveGames fakeGames(const std::string&,const char*,const std::atomic<bool>&) {
    handheld::ActiveGame game;
    game.id="AbC12345";
    game.opponent="Long_opponent_name_for_clipping";
    game.player=fakeOnline->player;
    game.position=fakeOnline->board.position;
    game.yourTurn=true;
    return {handheld::AccountCheck::Verified,{game}};
}
handheld::AccountCheck fakeStream(const std::string&,const std::string&,const char*,
    const std::atomic<bool>& cancelled,handheld::LiveBoardReceiver receive,void* target,
    const std::atomic<bool>* interrupted,handheld::ChatReceiver chatReceive,void* chatTarget) {
    receive(target,fakeOnline->board);
    ++fakeOnline->streams;
    if (fakeOnline->board.finished) return handheld::AccountCheck::Verified;
    while (!cancelled.load() && !(interrupted && interrupted->load())) {
        if (fakeOnline->injectMove.exchange(false)) {
            auto& board=fakeOnline->board;
            board.position=board.position.after(mv("e2e4"));
            board.history.push_back(mv("e2e4")); board.plies=1;
            receive(target,board);
            if (chatReceive) chatReceive(chatTarget,{"Opponent","Hello"});
            fakeOnline->moveDelivered.store(true);
        }
        if (fakeOnline->finishGame.exchange(false)) {
            fakeOnline->board.finished=true; fakeOnline->board.status="resign";
            fakeOnline->board.winner=White; receive(target,fakeOnline->board);
            fakeOnline->endDelivered.store(true);
            return handheld::AccountCheck::Verified;
        }
        if (fakeOnline->incomingOffer.exchange(false)) {
            fakeOnline->board.drawOffer[opposite(fakeOnline->player)]=true;
            receive(target,fakeOnline->board);
            fakeOnline->offerDelivered.store(true);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return handheld::AccountCheck::Failed;
}
handheld::PostResult fakePost(const std::string&,Move move,const std::string&,const char*,
                                 const std::atomic<bool>& cancelled) {
    ++fakeOnline->posts;
    while (!fakeOnline->releasePost.load() && !cancelled.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (!cancelled.load() && fakeOnline->applyMove) {
        fakeOnline->board.position=fakeOnline->board.position.after(move);
        fakeOnline->board.history.push_back(move);
        fakeOnline->board.lastMove=move;
        ++fakeOnline->board.plies;
    }
    return fakeOnline->result;
}
handheld::ChatHistory fakeHistory(const std::string&,const std::string&,const char*,const std::atomic<bool>&) {
    ++fakeOnline->historyCalls;
    return {handheld::AccountCheck::Verified,fakeOnline->chatHistory,fakeOnline->chatHistory.size()};
}
handheld::PostResult fakeChatSend(const std::string&,const std::string& text,const std::string&,const char*,const std::atomic<bool>&) {
    fakeOnline->sentChat=text; fakeOnline->chatHistory.push_back({"You",text}); ++fakeOnline->chatSends;
    return fakeOnline->chatResult;
}
handheld::AccountCheck fakeEvents(const std::string&,const char*,const std::atomic<bool>& cancelled,
    handheld::AccountEventReceiver receive,void* target) {
    using namespace handheld;
    receive(target,AccountEvent{}); ++fakeOnline->eventConnections;
    if (fakeOnline->initialChallenge) {
        AccountEvent event; event.kind=AccountEventKind::Challenge; event.challenge={"QwEr1234","Opponent",true}; receive(target,event);
    }
    while (!cancelled.load()) {
        if (fakeOnline->gameReady.exchange(false)) {
            AccountEvent event; event.kind=AccountEventKind::GameStart;
            event.game.id="AbC12345"; event.game.player=fakeOnline->player;
            event.game.opponent="Opponent"; event.game.position=Position::start(); receive(target,event);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return AccountCheck::Failed;
}
handheld::LobbyReply fakeLobby(const handheld::LobbyRequest& request,const std::string&,const char*,const std::atomic<bool>& cancelled) {
    using namespace handheld;
    fakeOnline->lastLobby=request; ++fakeOnline->lobbyCalls;
    if (request.action==LobbyAction::Seek) {
        while (!cancelled.load() && fakeOnline->seek.load()==FakeSeek::Pending)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (cancelled.load()) { ++fakeOnline->seekCancellations; return {AccountCheck::Failed,{}}; }
        if (fakeOnline->seek.load()==FakeSeek::RateLimited) return {AccountCheck::RateLimited,{}};
        if (fakeOnline->seek.load()==FakeSeek::Matched) fakeOnline->gameReady.store(true);
    } else if (request.action==LobbyAction::Accept) fakeOnline->gameReady.store(true);
    return {AccountCheck::Verified,request.action==LobbyAction::Challenge?"QwEr1234":""};
}
handheld::FriendReply fakeFriends(const handheld::FriendRequest& request,const std::string&,const char*,
                                  const std::atomic<bool>&) {
    using namespace handheld;
    auto& server=*fakeOnline;
    server.lastFriendRequest=request;
    FriendReply reply;
    if (request.action==FriendAction::Follow && usernameKey(request.username)=="missing") {
        reply.mutation=AccountCheck::Failed;
        reply.targetUnavailable=true;
    } else if (server.friendStatus==AccountCheck::Verified) {
        const auto id=usernameKey(request.username);
        const auto found=std::find_if(server.followed.begin(),server.followed.end(),
            [&](const Friend& player){return player.id==id;});
        if (request.action==FriendAction::Follow && found==server.followed.end())
            server.followed.push_back({id,request.username,Presence::Online});
        if (request.action==FriendAction::Unfollow && found!=server.followed.end()) server.followed.erase(found);
        if (request.action!=FriendAction::Refresh && server.lostFriendReply) reply.mutation=AccountCheck::Failed;
    }
    reply.list={server.friendStatus,server.followed};
    ++server.friendCalls;
    return reply;
}
void friendsModels() {
    using namespace handheld;
    CHECK(FriendsDecoder{}.finish().players.empty());
    FriendsDecoder stream;
    CHECK(stream.feed("\n{\"id\":\"pat\",\"user"));
    CHECK(stream.feed("name\":\"Pat\"}\r\n{\"id\":\"PAT\",\"username\":\"pAt\"}\n"));
    CHECK(stream.feed(R"({"id":"alex","username":"Alex"})"));
    auto list=stream.finish();
    CHECK(list.status==AccountCheck::Verified && list.players.size()==2);
    CHECK(list.players[0].id=="alex" && list.players[1].username=="Pat");
    CHECK(applyPresence(R"([{"id":"pat","playing":true}])",list.players,0,2));
    CHECK(list.players[0].presence==Presence::Unknown && list.players[1].presence==Presence::Playing);
    CHECK(applyPresence(R"([{"id":"alex","online":true},{"id":"pat"}])",list.players,0,2));
    CHECK(list.players[0].presence==Presence::Online && list.players[1].presence==Presence::Offline);
    CHECK(!applyPresence(R"([{"id":"alex","online":"yes"}])",list.players,0,2));
    CHECK(list.players[0].presence==Presence::Online);
    CHECK(!FriendsDecoder{}.feed(std::string(32769,'x')));
    CHECK(!FriendsDecoder{}.feed("{\"id\":\"../invalid\",\"username\":\"Invalid\"}\n"));
    CHECK(friendMutationPath({FriendAction::Follow,"PaT"})=="/api/rel/follow/pat");
    CHECK(friendMutationPath({FriendAction::Unfollow,"PaT"})=="/api/rel/unfollow/pat");
    CHECK(!friendMutationPath({FriendAction::Follow,"bad/name"}));
    FriendsDecoder large;
    for (int n=0;n<1001;++n) {
        const auto user="user"+std::to_string(n);
        CHECK(large.feed("{\"id\":\""+user+"\",\"username\":\""+user+"\"}\n"));
    }
    const auto bounded=large.finish();
    CHECK(bounded.truncated && bounded.players.size()==1000);
    std::cout<<"PASS bounded friends list, fragmented NDJSON, presence and follow request paths\n";
}
void extraModels() {
    using namespace handheld;
    CHECK(safeText("caf\xc3\xa9 \xf0\x9f\x98\x80")=="caf? ?");
    CHECK(safeText(std::string(200,'a')).size()==140);
    CHECK(formEncode("a b&+?")=="a%20b%26%2B%3F");
    CHECK(parseChatLine(R"({"room":"player","username":"Pat","text":"Hello"})")->text=="Hello");
    CHECK(!parseChatLine(R"({"room":"spectator","username":"Pat","text":"Ignore"})"));
    auto history=parseChatHistory(R"([{"user":"Pat","text":"Same"},{"user":"Pat","text":"Same"}])");
    CHECK(history && history->total==2 && history->messages.size()==2);
    ChatState state; state.replace(*history); state.replace(*history);
    CHECK(state.messages.size()==2);
    state.receive({"Pat","Same"}); CHECK(state.messages.size()==3);
    history->messages.push_back({"Pat","Same"}); history->total=3; state.replace(*history);
    CHECK(state.messages.size()==3); // Intentionally repeated messages retain separate positions.
    for (int n=0;n<150;++n) state.receive({"Pat",std::to_string(n)});
    CHECK(state.messages.size()==100 && state.unread==99);
    state.draft="Keep this"; CHECK(state.beginSend()); state.completeSend(PostResult::Uncertain);
    CHECK(state.draft=="Keep this"); CHECK(state.beginSend()); state.completeSend(PostResult::Rejected);
    CHECK(state.draft=="Keep this"); CHECK(state.beginSend()); state.completeSend(PostResult::Accepted);
    CHECK(state.draft.empty());
    state.replace({AccountCheck::Rejected,{},0}); state.draft="Blocked"; CHECK(!state.beginSend());
    ChatInbox inbox; for (int n=0;n<140;++n) inbox.push(ChatEvent{});
    CHECK(inbox.overflow.load()); ChatEvent event; int count=0; while (inbox.pop(event)) ++count; CHECK(count==127);
    TextEditor editor; for (const bool symbols:{false,true}) { editor.symbols=symbols; for (int n=0;n<40;++n) CHECK(editor.character(n)>=32 && editor.character(n)<127); }
    editor={}; std::string draft(140,'a'); editor.insert(draft); CHECK(draft.size()==140);
    editor.selected=43; editor.insert(draft); CHECK(draft.size()==139);
    CHECK(lobbyRequest({LobbyAction::Seek,{},15,10,-1})->second=="rated=false&variant=standard&time=15&increment=10");
    CHECK(lobbyRequest({LobbyAction::Challenge,"Player_1",10,0,1})->second.find("color=black")!=std::string::npos);
    CHECK(!lobbyRequest({LobbyAction::Challenge,"../escape",15,10,-1}));
    CHECK(!lobbyRequest({LobbyAction::Seek,{},1,0,-1}));
    std::vector<AccountEvent> events;
    const auto receive=[](void* out,const AccountEvent& e){static_cast<std::vector<AccountEvent>*>(out)->push_back(e);};
    AccountEventDecoder decoder;
    const std::string json=R"({"type":"challenge","challenge":{"id":"QwEr1234","challenger":{"name":"Pat"},"variant":{"key":"standard"}}})" "\n"
        R"({"type":"gameStart","game":{"gameId":"AbC12345","color":"black","opponent":{"username":"Pat"}}})" "\n"
        R"({"type":"challengeCanceled","challenge":{"id":"QwEr1234"}})" "\n";
    for (size_t n=0;n<json.size();n+=2) CHECK(decoder.feed(std::string_view(json).substr(n,2),receive,&events));
    CHECK(events.size()==4 && events[1].challenge.user=="Pat" && events[2].game.player==Black && events[3].kind==AccountEventKind::ChallengeRemoved);
    CHECK(!decoder.feed(std::string(32769,'x'),receive,&events));
    std::cout<<"PASS chat limits, repeated-message reconciliation, UTF-8, keyboard and account event framing\n";
}
handheld::PostResult fakeAction(const std::string&,handheld::BoardAction action,const std::string&,const char*,
                                const std::atomic<bool>& cancelled) {
    using namespace handheld;
    fakeOnline->lastAction=action;
    ++fakeOnline->actions;
    while (!fakeOnline->releasePost.load() && !cancelled.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (!cancelled.load() && fakeOnline->applyMove) {
        auto& board=fakeOnline->board;
        const auto player=fakeOnline->player, opponent=opposite(player);
        switch (action) {
            case BoardAction::OfferDraw: board.drawOffer[player]=true; break;
            case BoardAction::DeclineDraw: board.drawOffer[opponent]=false; break;
            case BoardAction::RequestTakeback: board.takebackOffer[player]=true; break;
            case BoardAction::DeclineTakeback: board.takebackOffer[opponent]=false; break;
            case BoardAction::AcceptTakeback:
                board.position=Position::start();
                board.history.clear();
                board.lastMove.reset();
                board.plies=0;
                board.remainingMs={900000,900000};
                board.takebackOffer={};
                break;
            case BoardAction::AcceptDraw:
                board.finished=true;
                board.status="draw";
                board.winner.reset();
                board.drawOffer={};
                break;
            case BoardAction::Resign:
                board.finished=true;
                board.status="resign";
                board.winner=opponent;
                break;
            default: CHECK(false);
        }
    }
    return fakeOnline->result;
}
void onlineInterface() {
    using namespace handheld;
    const std::string credential=std::string(std::getenv("HANDHELD_TEST_DIR"))+"/fake-online-token.txt";
    { std::ofstream file(credential); file<<"Test_fixture_only"; }
    LichessService service;
    service.available=fakeAvailable;
    service.games=fakeGames;
    service.stream=fakeStream;
    service.move=fakePost;
    for (const bool dual:{false,true}) for (int scenario=0;scenario<10;++scenario) {
        FakeOnline server;
        fakeOnline=&server;
        server.board.position=Position::start();
        server.board.realtimeClock=true;
        server.board.remainingMs={900000,900000};
        server.board.incrementMs={10000,10000};
        server.board.status="started";
        if (scenario==1) {
            server.player=Black;
            server.board.position=server.board.position.after(mv("e2e4"));
            server.board.history={mv("e2e4")};
            server.board.plies=1;
        }
        if (scenario==2) { server.result=PostResult::Uncertain; }
        if (scenario>=3 && scenario<=6) server.board.position=fen("7k/P7/8/8/8/8/8/7K w - - 0 1");
        if (scenario>=7) {
            server.applyMove=false;
            server.result=scenario==7?PostResult::Uncertain:scenario==8?PostResult::Rejected:
                PostResult::RateLimited;
        }
        {
            App app(credential.c_str(),nullptr,nullptr,service);
            auto press=[&](unsigned key){app.update({key},dual);};
            auto until=[&](auto ready) {
                const auto deadline=milliseconds()+3000;
                while (!ready() && milliseconds()<deadline) {
                    app.update({},dual);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                app.update({},dual);
                CHECK(ready());
            };
            auto render=[&](const std::string& stage) {
                for (const std::string theme:{"dark","light"}) {
                    screenWidth=dual?320:480;
                    screenHeight=dual?240:272;
                    app.draw(dual,false);
                    capture(std::string("online-")+(dual?"3ds-bottom-":"psp-")+
                            std::to_string(scenario)+"-"+stage+"-"+theme);
                    if (dual) { screenWidth=400; app.draw(true,true); }
                    press(AppearanceKey);
                }
            };
            for (int n=0;n<3;++n) press(Down);
            press(Confirm); // Lichess setup
            press(Down); press(Confirm); // Active games
            for (int n=0;n<30;++n) { app.update({},dual); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
            press(Confirm); // Select the fixture game
            until([&]{return server.streams.load()==1;});
            render("board");
            if (scenario>=3 && scenario<=6) {
                for (int n=0;n<4;++n) press(Left);
                for (int n=0;n<5;++n) press(Up);
                press(Confirm); press(Up); press(Confirm);
                render("promotion");
                for (int n=3;n<scenario;++n) press(Down);
                press(Confirm);
            } else {
                press(Confirm); press(Up); press(Up); press(Confirm);
            }
            until([&]{return server.posts.load()==1;});
            render("sending");
            press(Confirm); press(Confirm);
            CHECK(server.posts.load()==1); // No second submission while waiting.
            server.releasePost.store(true);
            if (scenario==9) {
                for (int n=0;n<30;++n) { app.update({},dual); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
                CHECK(server.streams.load()==1);
            } else until([&]{return server.streams.load()==2;});
            render("synchronized");
            press(Menu); render("menu"); press(Back);
            for (int n=0;n<10;++n) { app.update({},dual); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
            CHECK(server.posts.load()==1); // A lost acknowledgment never causes an automatic resend.
            if (scenario==9) {
                press(Menu); press(Down); press(Confirm); // Respect the one-minute rate-limit delay.
                CHECK(server.streams.load()==1 && server.posts.load()==1);
            }
        }
        if (server.applyMove)
            CHECK(server.board.history.back()==(scenario==1?mv("e7e5"):scenario<3?mv("e2e4"):
                Move{48,56,uint8_t(Queen-(scenario-3))}));
        else CHECK(server.board.history.empty());
    }
    fakeOnline=nullptr;
    std::remove(credential.c_str());
    std::cout << "PASS online controls, both colors, promotions and uncertain move acknowledgments\n";
}
void onlineActionInterface() {
    using namespace handheld;
    const std::string credential=std::string(std::getenv("HANDHELD_TEST_DIR"))+"/fake-actions-token.txt";
    { std::ofstream file(credential); file<<"Test_fixture_only"; }
    LichessService service;
    service.available=fakeAvailable;
    service.games=fakeGames;
    service.stream=fakeStream;
    service.move=fakePost;
    service.action=fakeAction;
    const BoardAction actions[]={BoardAction::OfferDraw,BoardAction::AcceptDraw,BoardAction::DeclineDraw,
        BoardAction::RequestTakeback,BoardAction::AcceptTakeback,BoardAction::DeclineTakeback,BoardAction::Resign};
    for (const bool dual:{false,true}) for (const auto player:{White,Black}) for (const auto action:actions) {
        FakeOnline server;
        fakeOnline=&server;
        server.player=player;
        server.board.history={mv("e2e4"),mv("e7e5")};
        server.board.position=Position::start().after(server.board.history[0]).after(server.board.history[1]);
        server.board.plies=2;
        server.board.status="started";
        server.board.realtimeClock=true;
        server.board.clockRunning=true;
        server.board.remainingMs={880000,890000};
        const auto opponent=opposite(player);
        if (action==BoardAction::AcceptDraw || action==BoardAction::DeclineDraw) server.board.drawOffer[opponent]=true;
        if (action==BoardAction::AcceptTakeback || action==BoardAction::DeclineTakeback) server.board.takebackOffer[opponent]=true;
        // A lost resignation acknowledgment must still produce a single request and the server's result.
        if (action==BoardAction::Resign) server.result=PostResult::Uncertain;
        {
            App app(credential.c_str(),nullptr,nullptr,service);
            auto press=[&](unsigned key) { app.update({key},dual); };
            auto until=[&](auto ready) {
                const auto deadline=milliseconds()+3000;
                while (!ready() && milliseconds()<deadline) {
                    app.update({},dual);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                app.update({},dual);
                CHECK(ready());
            };
            auto render=[&](const std::string& stage) {
                for (const std::string theme:{"dark","light"}) {
                    screenWidth=dual?320:480;
                    screenHeight=dual?240:272;
                    app.draw(dual,false);
                    capture(std::string("actions-")+(dual?"3ds-bottom-":"psp-")+
                            std::to_string(int(player))+"-"+std::to_string(int(action))+"-"+stage+"-"+theme);
                    if (dual) { screenWidth=400; app.draw(true,true); }
                    press(AppearanceKey);
                }
            };
            for (int n=0;n<3;++n) press(Down);
            press(Confirm); press(Down); press(Confirm);
            for (int n=0;n<30;++n) { app.update({},dual); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
            press(Confirm);
            until([&]{return server.streams.load()==1;});
            press(Menu);
            const int index=action==BoardAction::Resign || action==BoardAction::DeclineTakeback?3:
                action==BoardAction::DeclineDraw || action==BoardAction::RequestTakeback ||
                action==BoardAction::AcceptTakeback?2:1;
            for (int n=0;n<index;++n) press(Down);
            render("menu");
            if (action==BoardAction::Resign) {
                // Deliver a draw offer while Resign is focused, then press on the update frame.
                server.incomingOffer.store(true);
                const auto deadline=milliseconds()+3000;
                while (!server.offerDelivered.load() && milliseconds()<deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                CHECK(server.offerDelivered.load());
                press(Confirm); // Menu changes must be drawn before interpreting this press.
                CHECK(server.actions.load()==0);
                render("offer-arrived");
            }
            press(Confirm);
            if (action==BoardAction::Resign) {
                render("confirmation");
                CHECK(server.actions.load()==0);
                press(Confirm); // Cancel is selected initially.
                CHECK(server.actions.load()==0);
                press(Confirm); // Reopen confirmation.
                if (dual) app.update({0,30,104+29+10},true); // Confirm through the touchscreen.
                else { press(Down); press(Confirm); }
            }
            until([&]{return server.actions.load()==1;});
            CHECK(!server.board.finished); // Result stays server-authoritative during the pending request.
            render("sending");
            server.releasePost.store(true);
            until([&]{return server.streams.load()==2;});
            render("synchronized");
            for (int n=0;n<10;++n) { app.update({},dual); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
            CHECK(server.actions.load()==1 && server.posts.load()==0);
        }
        CHECK(server.lastAction==action);
        if (action==BoardAction::Resign) CHECK(server.board.finished && server.board.winner==opponent);
        if (action==BoardAction::AcceptDraw) CHECK(server.board.finished && !server.board.winner);
        if (action==BoardAction::AcceptTakeback) {
            CHECK(server.board.history.empty() && server.board.position.identity()==Position::start().identity());
            CHECK(server.board.remainingMs[White]==900000 && server.board.remainingMs[Black]==900000);
        }
        if (action==BoardAction::OfferDraw) CHECK(server.board.drawOffer[player]);
        if (action==BoardAction::RequestTakeback) CHECK(server.board.takebackOffer[player]);
        if (action==BoardAction::DeclineDraw) CHECK(!server.board.drawOffer[opponent]);
        if (action==BoardAction::DeclineTakeback) CHECK(!server.board.takebackOffer[opponent]);
    }
    fakeOnline=nullptr;
    std::remove(credential.c_str());
    std::cout << "PASS online resignation confirmation, draw/takeback actions, clock restoration and lost replies\n";
}
struct OnlineHarness {
    handheld::App& app;
    bool dual;
    std::string prefix;
    void press(unsigned key) { app.update({key},dual); }
    int type(const std::string& value) {
        handheld::TextEditor keyboard;
        int current=0;
        for (char c:value) {
            int target=0; while (target<40 && keyboard.character(target)!=c) ++target;
            CHECK(target<40);
            while (current/10<target/10) { press(handheld::Down); current+=10; }
            while (current/10>target/10) { press(handheld::Up); current-=10; }
            while (current%10<target%10) { press(handheld::Right); ++current; }
            while (current%10>target%10) { press(handheld::Left); --current; }
            press(handheld::Confirm);
        }
        return current;
    }
    void sendFrom(int cell) {
        for (int row=0;row<=cell/10;++row) press(handheld::Up);
        if (cell%10>=5) press(handheld::Left);
        press(handheld::Confirm);
    }
    template<class Predicate> void until(Predicate ready) {
        const auto deadline=handheld::milliseconds()+4000;
        while (!ready() && handheld::milliseconds()<deadline) {
            app.update({},dual); std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        app.update({},dual); CHECK(ready());
    }
    void settle() { for (int n=0;n<30;++n) { app.update({},dual); std::this_thread::sleep_for(std::chrono::milliseconds(1)); } }
    void render(const std::string& stage) {
        using namespace handheld;
        for (const std::string theme:{"dark","light"}) {
            screenWidth=dual?320:480; screenHeight=dual?240:272;
            app.draw(dual,false); capture(prefix+(dual?"-bottom-":"-psp-")+stage+"-"+theme);
            if (dual) { screenWidth=400; app.draw(true,true); capture(prefix+"-top-"+stage+"-"+theme); }
            press(AppearanceKey);
        }
    }
    void setup() { using namespace handheld; for (int n=0;n<3;++n) press(Down); press(Confirm); }
    void activeGame() { using namespace handheld; setup(); press(Down); press(Confirm); settle(); press(Confirm); until([]{return fakeOnline->streams.load()>0;}); }
};
void friendsInterface() {
    using namespace handheld;
    const std::string credential=std::string(std::getenv("HANDHELD_TEST_DIR"))+"/fake-friends-token.txt";
    { std::ofstream file(credential); file<<"Test_fixture_only"; }
    LichessService service; service.available=fakeAvailable; service.verify=fakeVerify;
    service.stream=fakeStream;
    ExtraService extra; extra.events=fakeEvents; extra.lobby=fakeLobby; extra.friends=fakeFriends;
    for (bool dual:{false,true}) {
        FakeOnline server; fakeOnline=&server; server.lostFriendReply=true;
        server.initialChallenge=true;
        server.board.position=Position::start(); server.board.status="started";
        {
            App app(credential.c_str(),nullptr,nullptr,service,extra);
            OnlineHarness h{app,dual,"friends"};
            h.setup(); h.press(Down); h.press(Down); h.press(Confirm);
            h.until([&]{return server.eventConnections.load()==1;});
            h.press(Up); h.press(Up); h.press(Confirm);
            h.until([&]{return server.friendCalls.load()==1;}); h.settle();
            h.render("empty");
            if (dual) app.update({0,30,60},true);
            else h.press(Confirm);
            h.sendFrom(h.type("you")); // Own username is rejected before sending.
            CHECK(server.friendCalls.load()==1);
            h.press(Back); h.press(Confirm); h.render("add");
            h.sendFrom(h.type("pat"));
            h.until([&]{return server.friendCalls.load()==2;}); h.settle();
            CHECK(server.lastFriendRequest.action==FriendAction::Follow && server.lastFriendRequest.username=="pat");
            h.render("followed");
            h.press(Up); // Pat.
            server.followed.push_back({"zed","Zed",Presence::Playing});
            h.press(Menu); h.until([&]{return server.friendCalls.load()==3;}); h.settle();
            h.render("refreshed");
            h.press(Confirm); h.press(Down); h.press(Confirm); h.render("unfollow");
            h.press(Confirm); // Cancel is selected by default.
            CHECK(server.friendCalls.load()==3);
            h.press(Confirm); h.press(Down); h.press(Confirm);
            h.until([&]{return server.friendCalls.load()==4;}); h.settle();
            CHECK(server.lastFriendRequest.action==FriendAction::Unfollow && server.lastFriendRequest.username=="pat");
            h.press(Confirm); h.sendFrom(h.type("missing"));
            h.until([&]{return server.friendCalls.load()==5;}); h.settle();
            CHECK(server.followed.size()==1 && server.followed.front().id=="zed");
            h.render("unavailable-player");
            h.press(Up); h.press(Confirm); h.press(Confirm);
            h.until([&]{return server.lobbyCalls.load()==1;}); h.settle();
            CHECK(server.lastLobby.action==LobbyAction::Challenge && server.lastLobby.target=="Zed");
            CHECK(server.lastLobby.minutes==15 && server.lastLobby.increment==10);
            server.gameReady.store(true);
            h.until([&]{return server.streams.load()==1;});
        }
        for (const auto status:{AccountCheck::Rejected,AccountCheck::RateLimited,AccountCheck::Verified}) {
            FakeOnline server; fakeOnline=&server;
            server.friendStatus=status; server.initialChallenge=true;
            server.board.position=Position::start(); server.board.status="started";
            App app(credential.c_str(),nullptr,nullptr,service,extra);
            OnlineHarness h{app,dual,"friends"};
            h.setup(); h.press(Down); h.press(Down); h.press(Confirm);
            h.until([&]{return server.eventConnections.load()==1;});
            h.press(Up); h.press(Up); h.press(Confirm);
            h.until([&]{return server.friendCalls.load()==1;}); h.settle();
            if (status==AccountCheck::RateLimited) {
                h.press(Down); h.press(Confirm); h.settle();
                CHECK(server.friendCalls.load()==1);
                h.render("rate-limit");
                continue;
            }
            h.render(status==AccountCheck::Rejected?"missing-permissions":"incoming-available");
            h.press(Down); h.press(Down); h.press(Confirm);
            h.press(Confirm); h.press(Confirm);
            h.until([&]{return server.lobbyCalls.load()==1 && server.streams.load()==1;});
            CHECK(server.lastLobby.action==LobbyAction::Accept);
        }
    }
    fakeOnline=nullptr; std::remove(credential.c_str());
    std::cout<<"PASS Friends menus, follow reconciliation, unfollow, challenges, permissions and rate limiting\n";
}
void onlineReleaseUI() {
    using namespace handheld;
    const std::string credential=std::string(std::getenv("HANDHELD_TEST_DIR"))+"/fake-release-token.txt";
    { std::ofstream file(credential); file<<"Test_fixture_only"; }
    LichessService service; service.available=fakeAvailable; service.verify=fakeVerify; service.games=fakeGames; service.stream=fakeStream;
    service.move=fakePost; service.action=fakeAction;
    ExtraService extra; extra.history=fakeHistory; extra.send=fakeChatSend; extra.events=fakeEvents; extra.lobby=fakeLobby;
    for (const bool dual:{false,true}) {
        for (int scenario=0;scenario<3;++scenario) {
            FakeOnline server; fakeOnline=&server; server.board.position=Position::start(); server.board.status="started";
            server.board.initialMs=900000; server.board.realtimeClock=true; server.board.remainingMs={900000,900000};
            if (scenario==1) server.chatResult=PostResult::Uncertain;
            {
                App app(credential.c_str(),nullptr,nullptr,service,extra);
                OnlineHarness h{app,dual,"release-chat-"+std::to_string(scenario)};
                h.activeGame(); h.press(Chat); h.until([&]{return server.historyCalls.load()==1;}); h.settle(); h.render("transcript");
                if (scenario==2) {
                    h.press(Down); h.press(Confirm); h.render("phrases"); h.press(Confirm);
                    CHECK(server.chatSends.load()==0); h.render("phrase-draft"); h.press(Confirm);
                    h.until([&]{return server.chatSends.load()==1;}); h.settle();
                } else {
                    h.press(Confirm); int keyboardCell=h.type("ab");
                    h.render("keyboard");
                    if (scenario==0) {
                        server.injectMove.store(true); h.until([&]{return server.moveDelivered.load();});
                        h.press(Left); h.press(Left); h.press(Confirm); h.render("incoming-move"); // Still typing: "abc".
                        CHECK(server.posts.load()==0);
                        server.finishGame.store(true); h.until([&]{return server.endDelivered.load();}); h.settle(); h.render("results");
                        h.press(Chat); h.settle(); h.press(Confirm); // Preserved draft after completion.
                        keyboardCell=0;
                    }
                    h.sendFrom(keyboardCell);
                    h.until([&]{return server.chatSends.load()==1;}); h.settle(); h.render("delivery");
                    if (scenario==1) {
                        CHECK(server.chatSends.load()==1); h.press(Back); h.press(Chat); h.press(Chat); h.press(Confirm);
                        h.press(Up); h.press(Confirm); // User explicitly retries the preserved draft.
                        h.until([&]{return server.chatSends.load()==2;}); h.settle();
                    }
                }
            }
            CHECK(server.sentChat==(scenario==0?"abc":scenario==1?"ab":"Good luck"));
        }
        for (int scenario=0;scenario<5;++scenario) {
            FakeOnline server; fakeOnline=&server; server.board.position=Position::start(); server.board.status="started";
            server.initialChallenge=scenario==2 || scenario==3;
            App app(credential.c_str(),nullptr,nullptr,service,extra);
            OnlineHarness h{app,dual,"release-lobby-"+std::to_string(scenario)};
            h.setup(); h.press(Down); h.press(Down); h.press(Confirm);
            h.until([&]{return server.eventConnections.load()==1;}); h.settle(); h.render("menu");
            if (scenario==0) {
                h.press(Down); h.press(Confirm); h.until([&]{return server.lobbyCalls.load()==1;}); h.render("searching");
                CHECK(server.lastLobby.action==LobbyAction::Seek && server.lastLobby.minutes==15 &&
                    server.lastLobby.increment==10 && server.lastLobby.target.empty());
                h.press(Back); // Close the active seek before an opponent has accepted it.
                CHECK(server.seekCancellations.load()==1 && server.streams.load()==0);
                h.press(Confirm); // Find opponent is already focused after canceling.
                h.until([&]{return server.lobbyCalls.load()==2;});
                server.seek.store(FakeSeek::Closed); h.settle(); h.render("search-ended");
                CHECK(server.streams.load()==0);
                server.seek.store(FakeSeek::Pending);
                h.press(Confirm);
                h.until([&]{return server.lobbyCalls.load()==3;});
                server.seek.store(FakeSeek::Matched);
                h.until([&]{return server.streams.load()==1;}); h.render("paired");
            } else if (scenario==1) {
                h.press(Down); h.press(Down); h.press(Confirm); h.render("username");
                h.sendFrom(h.type("bb"));
                h.until([&]{return server.lobbyCalls.load()==1;}); h.settle(); h.render("challenge-sent");
                CHECK(server.lastLobby.target=="bb");
            } else if (scenario<=3) {
                for (int n=0;n<3;++n) h.press(Down);
                h.press(Confirm); h.render("incoming"); h.press(Confirm); h.render("challenge-actions");
                if (scenario==3) h.press(Down);
                h.press(Confirm); h.until([&]{return server.lobbyCalls.load()==1;});
                if (scenario==2) { h.until([&]{return server.streams.load()==1;}); h.render("accepted"); }
                else h.settle();
            } else {
                server.seek.store(FakeSeek::RateLimited);
                h.press(Down); h.press(Confirm);
                h.until([&]{return server.lobbyCalls.load()==1;});
                h.settle(); h.render("search-rate-limited");
                h.press(Confirm); h.settle();
                CHECK(server.lobbyCalls.load()==1 && server.streams.load()==0);
            }
        }
        {
            FakeOnline server; fakeOnline=&server;
            server.board.history={mv("e2e4"),mv("e7e5")}; server.board.plies=2;
            server.board.position=Position::start().after(mv("e2e4")).after(mv("e7e5"));
            server.board.status="resign"; server.board.finished=true; server.board.winner=White;
            server.board.initialMs=900000; server.board.realtimeClock=true; server.board.remainingMs={800000,810000};
            {
                App app(credential.c_str(),nullptr,nullptr,service,extra); OnlineHarness h{app,dual,"release-analysis"};
                h.activeGame(); h.render("results"); h.press(Up); h.press(Confirm); h.render("recorded-line");
                h.press(Menu); h.press(Left); // Position change cancels the previous search.
                h.press(Menu);
                for (int n=0;n<7;++n) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); app.update({},dual); }
                h.render("evaluation"); h.press(Confirm);
                h.press(Left); h.press(Confirm); h.press(Down); h.press(Down); h.press(Confirm);
                h.render("variation");
                h.press(Back); h.press(Back); h.render("back-to-results");
                h.press(Up); h.press(Up); h.press(Confirm); // Rematch: a new challenge with the opposite color.
                h.until([&]{return server.lobbyCalls.load()==1;}); h.settle();
                CHECK(server.lastLobby.action==LobbyAction::Challenge && server.lastLobby.color==Black);
            }
            CHECK(server.board.history.size()==2 && server.board.position.board[28]==Pawn);
        }
    }
    fakeOnline=nullptr; std::remove(credential.c_str());
    std::cout<<"PASS chat focus/drafts/delivery/postgame, lobby search/challenges, rematches and isolated online analysis\n";
}
}
int main() {
    chessRules();
    codec();
    engine();
    sockets();
    userInterface();
    lichessProfile();
    lichessActiveGames();
    lichessGameStream();
    lichessMoves();
    lichessActions();
    onlineInterface();
    onlineActionInterface();
    extraModels();
    friendsModels();
    friendsInterface();
    onlineReleaseUI();
}
