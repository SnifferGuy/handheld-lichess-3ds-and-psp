#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chess {
enum Piece { Empty, Pawn, Knight, Bishop, Rook, Queen, King };
enum Color { White, Black };
enum class Result : uint8_t { Playing, WhiteWins, BlackWins, Draw };
struct Move {
    uint8_t from = 0, to = 0, promotion = Empty;
    bool operator==(Move m) const { return from == m.from && to == m.to && promotion == m.promotion; }
};
struct Position {
    std::array<int8_t,64> board{}; // a1 = 0, white positive
    Color turn = White;
    uint8_t castle = 15; // WK, WQ, BK, BQ
    int8_t ep = -1;
    uint16_t quiet = 0;
    static Position start();
    static std::optional<Position> fromFen(std::string_view);
    bool check(Color color) const;
    bool attacked(int square, Color by) const;
    std::vector<Move> legal() const;
    Position after(Move m) const; // caller supplies a legal or pseudo-legal move
    std::array<int8_t,67> identity() const;
    bool dead() const;
};
struct GameState {
    Position pos = Position::start();
    std::vector<Move> history;
    std::vector<std::array<int8_t,67>> positions{pos.identity()};
    Result result = Result::Playing;
    bool play(Move move);
    bool canClaim(const Move* intended = nullptr) const;
    void resign(Color color);
    void updateResult();
};
Color opposite(Color color);
const char* resultName(Result result);
std::string notation(Move move);
}
