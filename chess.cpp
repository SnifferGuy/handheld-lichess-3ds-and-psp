#include "chess.hpp"
#include <algorithm>
#include <charconv>
#include <cstdlib>

namespace chess {
Color opposite(Color c) { return c == White ? Black : White; }
static int sign(Color c) { return c == White ? 1 : -1; }
static bool valid(int x, int y) { return x >= 0 && x < 8 && y >= 0 && y < 8; }
static constexpr int directions[8][2] = {{1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,1},{1,-1},{-1,-1}};
static constexpr int jumps[8][2] = {{1,2},{2,1},{-1,2},{-2,1},{1,-2},{2,-1},{-1,-2},{-2,-1}};

Position Position::start() {
    Position p;
    constexpr int back[] = {Rook,Knight,Bishop,Queen,King,Bishop,Knight,Rook};
    for (int i=0;i<8;++i) {
        p.board[i]=back[i]; p.board[8+i]=Pawn;
        p.board[48+i]=-Pawn; p.board[56+i]=-back[i];
    }
    return p;
}

std::optional<Position> Position::fromFen(std::string_view fen) {
    if (fen.size()>128) return std::nullopt;
    std::array<std::string_view,6> fields;
    for (size_t i=0;i<fields.size();++i) {
        const auto end=fen.find(' ');
        if ((end==std::string_view::npos)!=(i==fields.size()-1)) return std::nullopt;
        fields[i]=fen.substr(0,end);
        if (fields[i].empty()) return std::nullopt;
        if (end!=std::string_view::npos) fen.remove_prefix(end+1);
    }
    Position position;
    constexpr std::string_view pieces="PNBRQKpnbrqk";
    int rank=7, file=0;
    for (const char ch:fields[0]) {
        if (ch=='/') {
            if (file!=8 || rank==0) return std::nullopt;
            --rank;
            file=0;
        } else if (ch>='1' && ch<='8') {
            file+=ch-'0';
            if (file>8) return std::nullopt;
        } else {
            const auto index=pieces.find(ch);
            if (index==std::string_view::npos || file==8) return std::nullopt;
            const int piece=int(index%6)+1;
            if (piece==Pawn && (rank==0 || rank==7)) return std::nullopt;
            position.board[rank*8+file++]=index<6?piece:-piece;
        }
    }
    if (rank!=0 || file!=8 ||
        std::count(position.board.begin(),position.board.end(),King)!=1 ||
        std::count(position.board.begin(),position.board.end(),-King)!=1) return std::nullopt;
    if (fields[1]=="b") position.turn=Black;
    else if (fields[1]!="w") return std::nullopt;
    position.castle=0;
    if (fields[2]!="-") {
        for (const char ch:fields[2]) {
            const auto bit=std::string_view("KQkq").find(ch);
            if (bit==std::string_view::npos || (position.castle&(1u<<bit))) return std::nullopt;
            position.castle|=1u<<bit;
        }
    }
    if (fields[3]!="-") {
        const auto square=fields[3];
        if (square.size()!=2 || square[0]<'a' || square[0]>'h' ||
            square[1]!=(position.turn==White?'6':'3')) return std::nullopt;
        position.ep=(square[1]-'1')*8+square[0]-'a';
        if (position.board[position.ep]!=Empty) return std::nullopt;
    }
    const auto readNumber=[](std::string_view number,auto& out) {
        const auto parsed=std::from_chars(number.data(),number.data()+number.size(),out);
        return parsed.ec==std::errc{} && parsed.ptr==number.data()+number.size();
    };
    unsigned fullMove=0;
    if (!readNumber(fields[4],position.quiet) || !readNumber(fields[5],fullMove) || !fullMove)
        return std::nullopt;
    return position;
}

bool Position::attacked(int square, Color by) const {
    const int x=square%8, y=square/8, s=sign(by);
    const int pawnY=y-s;
    for (int dx : {-1,1})
        if (valid(x+dx,pawnY) && board[pawnY*8+x+dx]==s*Pawn) return true;
    for (auto& d : jumps)
        if (valid(x+d[0],y+d[1]) && board[(y+d[1])*8+x+d[0]]==s*Knight) return true;
    for (int i=0;i<8;++i) {
        const auto& d=directions[i];
        for (int n=1;valid(x+n*d[0],y+n*d[1]);++n) {
            const int piece=board[(y+n*d[1])*8+x+n*d[0]]*s;
            if (!piece) continue;
            if (piece==Queen || piece==(i<4?Rook:Bishop) || (n==1 && piece==King)) return true;
            break;
        }
    }
    return false;
}
bool Position::check(Color c) const {
    const auto king=std::find(board.begin(),board.end(),sign(c)*King);
    return king==board.end() || attacked(int(king-board.begin()),opposite(c));
}

Position Position::after(Move m) const {
    Position p=*this;
    const int piece=board[m.from], s=sign(turn);
    p.ep=-1;
    p.quiet=(std::abs(piece)==Pawn || board[m.to])?0:quiet+1;
    p.board[m.from]=Empty;
    p.board[m.to]=m.promotion ? s*m.promotion : piece;
    if (std::abs(piece)==Pawn) {
        if (m.to==ep && !board[m.to]) p.board[m.to-8*s]=Empty;
        if (std::abs(int(m.to)-m.from)==16) p.ep=(m.from+m.to)/2;
    }
    if (std::abs(piece)==King) {
        p.castle &= turn==White ? 12 : 3;
        if (int(m.to)-m.from==2) {
            p.board[m.to+1]=Empty; p.board[m.to-1]=s*Rook;
        } else if (int(m.from)-m.to==2) {
            p.board[m.to-2]=Empty; p.board[m.to+1]=s*Rook;
        }
    }
    constexpr int corners[]={7,0,63,56};
    for (int i=0;i<4;++i)
        if (m.from==corners[i] || m.to==corners[i]) p.castle &= ~(1<<i);
    p.turn=opposite(turn);
    return p;
}

std::vector<Move> Position::legal() const {
    std::vector<Move> moves;
    moves.reserve(48);
    const int s=sign(turn);
    const Color enemy=opposite(turn);
    for (int from=0;from<64;++from) {
        const int piece=board[from]*s, x=from%8, y=from/8;
        if (piece<=0) continue;
        auto add=[&](int to,int promotion=Empty) {
            if (board[to]*s>0 || board[to]==-s*King) return;
            Move m{uint8_t(from),uint8_t(to),uint8_t(promotion)};
            if (!after(m).check(turn)) moves.push_back(m);
        };
        if (piece==Pawn) {
            auto advance=[&](int to) {
                if (to/8==0 || to/8==7)
                    for (int p : {Queen,Rook,Bishop,Knight}) add(to,p);
                else add(to);
            };
            if (!valid(x,y+s)) continue;
            const int next=from+8*s;
            if (!board[next]) {
                advance(next);
                if (y==(turn==White?1:6) && !board[next+8*s]) add(next+8*s);
            }
            for (int dx : {-1,1})
                if (valid(x+dx,y+s) && (board[next+dx]*s<0 ||
                    (next+dx==ep && board[from+dx]==-s*Pawn && !board[next+dx]))) advance(next+dx);
        } else if (piece==Knight) {
            for (auto& d : jumps)
                if (valid(x+d[0],y+d[1])) add((y+d[1])*8+x+d[0]);
        } else {
            const int first=piece==Bishop?4:0, last=piece==Rook?4:8;
            for (int i=first;i<last;++i) {
                const auto& d=directions[i];
                for (int n=1;valid(x+n*d[0],y+n*d[1]);++n) {
                    const int to=(y+n*d[1])*8+x+n*d[0];
                    add(to);
                    if (board[to] || piece==King) break;
                }
            }
            if (piece==King && from==(turn==White?4:60) && !attacked(from,enemy)) {
                const int rights=castle>>(turn==White?0:2);
                if ((rights&1) && !board[from+1] && !board[from+2] &&
                    board[from+3]==s*Rook && !attacked(from+1,enemy)) add(from+2);
                if ((rights&2) && !board[from-1] && !board[from-2] && !board[from-3] &&
                    board[from-4]==s*Rook && !attacked(from-1,enemy)) add(from-2);
            }
        }
    }
    return moves;
}
std::array<int8_t,67> Position::identity() const {
    std::array<int8_t,67> key{};
    std::copy(board.begin(),board.end(),key.begin());
    key[64]=turn; key[65]=castle; key[66]=-1;
    if (ep>=0) {
        const int s=sign(turn), row=ep/8-s, x=ep%8;
        for (int dx : {-1,1}) {
            if (!valid(x+dx,row)) continue;
            const int from=row*8+x+dx;
            if (board[from]==s*Pawn && board[ep-8*s]==-s*Pawn &&
                !after({uint8_t(from),uint8_t(ep),0}).check(turn)) key[66]=ep;
        }
    }
    return key;
}
bool Position::dead() const {
    int minors=0, bishopColor=-1;
    bool bishopsOnly=true;
    for (int i=0;i<64;++i) {
        const int p=std::abs(board[i]);
        if (!p || p==King) continue;
        if (p!=Bishop && p!=Knight) return false;
        ++minors;
        if (p==Knight) bishopsOnly=false;
        else {
            const int color=(i/8+i%8)%2;
            if (bishopColor>=0 && bishopColor!=color) bishopsOnly=false;
            bishopColor=color;
        }
    }
    return minors<=1 || bishopsOnly;
}
bool GameState::play(Move m) {
    if (result!=Result::Playing) return false;
    const auto moves=pos.legal();
    if (std::find(moves.begin(),moves.end(),m)==moves.end()) return false;
    pos=pos.after(m);
    history.push_back(m);
    positions.push_back(pos.identity());
    updateResult();
    return true;
}
bool GameState::canClaim(const Move* intended) const {
    if (result!=Result::Playing) return false;
    Position candidate=pos;
    if (intended) {
        const auto moves=pos.legal();
        if (std::find(moves.begin(),moves.end(),*intended)==moves.end()) return false;
        candidate=pos.after(*intended);
        if (candidate.legal().empty() && candidate.check(candidate.turn)) return false;
    }
    return candidate.quiet>=100 ||
        std::count(positions.begin(),positions.end(),candidate.identity())+(intended?1:0)>=3;
}
void GameState::updateResult() {
    if (pos.legal().empty()) {
        result=pos.check(pos.turn)?(pos.turn==White?Result::BlackWins:Result::WhiteWins):Result::Draw;
    } else if (pos.dead() || pos.quiet>=150 ||
               std::count(positions.begin(),positions.end(),positions.back())>=5) {
        result=Result::Draw;
    }
}
void GameState::resign(Color color) {
    if (result!=Result::Playing) return;
    const int winnerSign=sign(opposite(color));
    const bool bare=std::none_of(pos.board.begin(),pos.board.end(),[&](int p) {
        return p*winnerSign>0 && std::abs(p)!=King;
    });
    result=bare?Result::Draw:(color==White?Result::BlackWins:Result::WhiteWins);
}
const char* resultName(Result r) {
    switch(r) {
        case Result::WhiteWins: return "White wins";
        case Result::BlackWins: return "Black wins";
        case Result::Draw: return "Draw";
        default: return "Playing";
    }
}
std::string notation(Move m) {
    std::string out{char('a'+m.from%8),char('1'+m.from/8),'-',char('a'+m.to%8),char('1'+m.to/8)};
    if (m.promotion>=Knight && m.promotion<=Queen) out += "  NBRQ"[m.promotion];
    return out;
}
}
