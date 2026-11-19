#include "ai.hpp"
#include <algorithm>
#include <cstdlib>

namespace chess {
namespace {
constexpr int values[]={0,100,320,330,500,900,20000};
constexpr int maxDepth=6, captureDepth=4;
Suggestion prepend(Move move,const Suggestion& reply) {
    Suggestion line;
    line.moves[0]=move;
    const auto copied=std::min<size_t>(reply.length,line.moves.size()-1);
    std::copy_n(reply.moves.begin(),copied,line.moves.begin()+1);
    line.length=unsigned(copied+1);
    return line;
}
int evaluate(const Position& p) {
    int score=0;
    for (int i=0;i<64;++i) {
        const int piece=p.board[i], type=std::abs(piece);
        if (!type) continue;
        const int rank=piece>0?i/8:7-i/8;
        const int center=14-std::abs(2*(i%8)-7)-std::abs(2*(i/8)-7);
        const int bonus=type==Pawn?rank*7: type==King?-center: center*(type==Knight?4:2);
        score+=(piece>0?1:-1)*(values[type]+bonus);
    }
    return p.turn==White?score:-score;
}
bool tactical(const Position& p, Move m) {
    return m.promotion || p.board[m.to] || (std::abs(p.board[m.from])==Pawn && m.to==p.ep);
}
int priority(const Position& p, Move m) {
    const int victim=std::abs(p.board[m.to]);
    return (tactical(p,m)?10000:0)+values[victim]*8-values[std::abs(p.board[m.from])]+values[m.promotion];
}
struct Search {
    Clock clock;
    uint64_t stopAt;
    const std::atomic<bool>& cancel;
    unsigned remaining;
    bool stopped=false;
    std::vector<std::array<int8_t,67>> keys;
    std::array<Suggestion,maxDepth+captureDepth+1> lines{};
    bool stop() {
        if (cancel.load(std::memory_order_relaxed) || !remaining || clock()>=stopAt) stopped=true;
        if (!stopped) --remaining;
        return stopped;
    }
    int score(const Position& p, int depth, int q, int ply, int alpha, int beta) {
        auto& line=lines[ply];
        line.length=0;
        if (stop()) return 0;
        auto moves=p.legal();
        const bool checked=p.check(p.turn);
        if (moves.empty()) return checked?-30000+ply:0;
        const auto key=p.identity();
        const auto repetitions=std::count(keys.end()-std::min<size_t>(keys.size(),p.quiet),keys.end(),key)+1;
        if (p.dead() || p.quiet>=150 || repetitions>=5) return 0;
        const bool canDraw=p.quiet>=100 || repetitions>=3;
        int best=canDraw?0:-31000;
        if (depth==0) {
            if (!q) return canDraw?std::max(0,evaluate(p)):evaluate(p);
            if (!checked) best=std::max(best,evaluate(p));
        }
        alpha=std::max(alpha,best);
        if (alpha>=beta) return best;
        std::sort(moves.begin(),moves.end(),[&](Move a,Move b){return priority(p,a)>priority(p,b);});
        keys.push_back(key);
        for (auto m : moves) {
            if (!depth && !checked && !tactical(p,m)) continue;
            const int value=-score(p.after(m),std::max(0,depth-1),depth?q:q-1,ply+1,-beta,-alpha);
            if (stopped) break;
            if (value>best) {
                best=value;
                line=prepend(m,lines[ply+1]);
            }
            alpha=std::max(alpha,value);
            if (alpha>=beta) break;
        }
        keys.pop_back();
        return best;
    }
};
}
Choice choose(const GameState& game, Difficulty difficulty, Clock now, const std::atomic<bool>& cancel) {
    if (cancel.load() || game.result!=Result::Playing) return {};
    const auto start=now();
    auto moves=game.pos.legal();
    if (moves.empty()) return {};
    if (game.canClaim()) return {{},true,true,false,0,false,{}};
    const int level=static_cast<int>(difficulty);
    const uint64_t time[]={100,500,1500};
    const unsigned nodes[]={10000,50000,250000};
    Search search{now,start+time[level],cancel,nodes[level],false,game.positions};
    Move best=moves.front();
    int bestScore=0;
    bool scored=false;
    Suggestion bestLine=prepend(best,{});
    // An intended claim is useful when available. Avoid repeatedly generating
    // every legal move in positions where an intended claim is impossible.
    if (game.pos.quiet>=4) {
        for (auto move : moves) {
            const auto next=game.pos.after(move);
            const auto key=next.identity();
            if ((next.quiet>=100 || std::count(game.positions.begin(),game.positions.end(),key)>=2) &&
                game.canClaim(&move)) return {move,true,true,true,0,false,{}};
        }
    }
    for (int depth=1;depth<=std::min(maxDepth,2*(level+1));++depth) {
        std::stable_sort(moves.begin(),moves.end(),[&](Move a,Move b) {
            if ((a==best)!=(b==best)) return a==best;
            return priority(game.pos,a)>priority(game.pos,b);
        });
        int alpha=-31000;
        Suggestion candidate;
        for (Move m : moves) {
            const int value=-search.score(game.pos.after(m),depth-1,captureDepth,1,-31000,-alpha);
            if (search.stopped) break;
            if (value>alpha) {
                alpha=value;
                candidate=prepend(m,search.lines[1]);
            }
        }
        if (search.stopped) break;
        bestLine=candidate;
        best=bestLine.moves[0];
        bestScore=alpha;
        scored=true;
        if (alpha>=29900) break;
    }
    return cancel.load()?Choice{}:Choice{best,true,false,false,bestScore,scored,bestLine};
}
}
