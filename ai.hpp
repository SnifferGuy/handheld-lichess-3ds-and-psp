#pragma once
#include "chess.hpp"
#include <atomic>
namespace chess {
enum class Difficulty { Easy, Normal, Hard };
struct Suggestion {
    std::array<Move,3> moves{};
    unsigned length=0;
};
struct Choice {
    Move move{};
    bool valid=false, claim=false, claimWithMove=false;
    int score=0; // centipawns from the side-to-move perspective
    bool scored=false;
    Suggestion line;
};
using Clock = uint64_t (*)(); // monotonic milliseconds
Choice choose(const GameState&, Difficulty, Clock, const std::atomic<bool>& cancel);
}
