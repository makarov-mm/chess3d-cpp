#include "Chess.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <format>
#include <sstream>
#include <utility>

namespace chess {

namespace {

constexpr std::string_view StartFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// The squares a knight or a king reaches from each square.
struct LeaperTable {
    std::array<std::array<std::int8_t, 8>, 64> targets{};
    std::array<std::uint8_t, 64> count{};
};

constexpr LeaperTable BuildLeaperTable(const std::array<std::pair<int, int>, 8>& steps)
{
    LeaperTable table{};
    for (Square square = 0; square < 64; ++square) {
        for (auto [df, dr] : steps) {
            int file = FileOf(square) + df, rank = RankOf(square) + dr;
            if (IsInside(file, rank))
                table.targets[square][table.count[square]++] = static_cast<std::int8_t>(MakeSquare(file, rank));
        }
    }
    return table;
}

constexpr LeaperTable Knight = BuildLeaperTable({{{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}}});
constexpr LeaperTable King = BuildLeaperTable({{{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}}});

// The squares along each of the 8 directions from each square: 0-3 straight, 4-7 diagonal.
struct RayTable {
    std::array<std::array<std::array<std::int8_t, 7>, 8>, 64> squares{};
    std::array<std::array<std::uint8_t, 8>, 64> length{};
};

constexpr RayTable BuildRays()
{
    constexpr std::array<std::pair<int, int>, 8> directions{{{0, 1}, {0, -1}, {1, 0}, {-1, 0}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}}};
    RayTable table{};
    for (Square square = 0; square < 64; ++square) {
        for (int d = 0; d < 8; ++d) {
            auto [df, dr] = directions[d];
            for (int file = FileOf(square) + df, rank = RankOf(square) + dr; IsInside(file, rank); file += df, rank += dr)
                table.squares[square][d][table.length[square][d]++] = static_cast<std::int8_t>(MakeSquare(file, rank));
        }
    }
    return table;
}

constexpr RayTable Rays = BuildRays();

// Castling rights that survive a move from or to each square.
constexpr std::array<std::uint8_t, 64> CastlingMask = [] {
    std::array<std::uint8_t, 64> mask{};
    mask.fill(castling::All);
    mask[MakeSquare(4, 0)] = castling::All & ~(castling::WhiteKingside | castling::WhiteQueenside);
    mask[MakeSquare(7, 0)] = castling::All & ~castling::WhiteKingside;
    mask[MakeSquare(0, 0)] = castling::All & ~castling::WhiteQueenside;
    mask[MakeSquare(4, 7)] = castling::All & ~(castling::BlackKingside | castling::BlackQueenside);
    mask[MakeSquare(7, 7)] = castling::All & ~castling::BlackKingside;
    mask[MakeSquare(0, 7)] = castling::All & ~castling::BlackQueenside;
    return mask;
}();

constexpr std::uint64_t SplitMix64(std::uint64_t& state)
{
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

struct ZobristKeys {
    std::array<std::array<std::uint64_t, 64>, 12> piece{};
    std::array<std::uint64_t, 16> castling{};
    std::array<std::uint64_t, 8> enPassant{};
    std::uint64_t side = 0;
};

constexpr ZobristKeys Zobrist = [] {
    ZobristKeys keys{};
    std::uint64_t state = 0x3D5A1C7F2B9E4806ull;
    for (auto& squares : keys.piece)
        for (auto& key : squares)
            key = SplitMix64(state);
    for (auto& key : keys.castling)
        key = SplitMix64(state);
    for (auto& key : keys.enPassant)
        key = SplitMix64(state);
    keys.side = SplitMix64(state);
    return keys;
}();

constexpr int PieceIndex(Piece piece) { return std::to_underlying(piece.type) - 1 + 6 * std::to_underlying(piece.color); }

constexpr std::array<PieceType, 4> Promotions{PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight};

char PieceLetter(PieceType type)
{
    switch (type) {
    case PieceType::Pawn: return 'P';
    case PieceType::Knight: return 'N';
    case PieceType::Bishop: return 'B';
    case PieceType::Rook: return 'R';
    case PieceType::Queen: return 'Q';
    case PieceType::King: return 'K';
    default: return '?';
    }
}

PieceType PieceFromLetter(char letter)
{
    switch (std::toupper(static_cast<unsigned char>(letter))) {
    case 'P': return PieceType::Pawn;
    case 'N': return PieceType::Knight;
    case 'B': return PieceType::Bishop;
    case 'R': return PieceType::Rook;
    case 'Q': return PieceType::Queen;
    case 'K': return PieceType::King;
    default: return PieceType::None;
    }
}

std::vector<std::string_view> SplitWhitespace(std::string_view text)
{
    std::vector<std::string_view> parts;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        std::size_t start = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])))
            ++i;
        if (i > start)
            parts.push_back(text.substr(start, i - start));
    }
    return parts;
}

std::string_view Trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return text;
}

// SAN without the check marks and the '=' of promotions, and with zeros for castling read as letters.
std::string NormalizeSan(std::string_view san)
{
    std::string result;
    for (char c : san) {
        if (c == '+' || c == '#' || c == '!' || c == '?' || c == '=')
            continue;
        result += c == '0' ? 'O' : c;
    }
    if (result.ends_with("e.p."))
        result.resize(result.size() - 4);
    return result;
}

} // namespace

std::string SquareName(Square square)
{
    return {static_cast<char>('a' + FileOf(square)), static_cast<char>('1' + RankOf(square))};
}

std::string ToUci(Move move)
{
    std::string text = SquareName(move.from) + SquareName(move.to);
    if (move.promotion != PieceType::None)
        text += static_cast<char>(std::tolower(PieceLetter(move.promotion)));
    return text;
}

// Position -----------------------------------------------------------------------------------------

Position Position::Start()
{
    return *FromFen(StartFen);
}

std::expected<Position, std::string> Position::FromFen(std::string_view fen)
{
    std::vector<std::string_view> fields = SplitWhitespace(fen);
    if (fields.size() < 2)
        return std::unexpected("A FEN needs at least the pieces and the side to move.");

    Position position;
    int file = 0, rank = 7;
    for (char c : fields[0]) {
        if (c == '/') {
            if (file != 8)
                return std::unexpected("A rank of the FEN does not have 8 squares.");
            file = 0;
            if (--rank < 0)
                return std::unexpected("The FEN has more than 8 ranks.");
        }
        else if (c >= '1' && c <= '8') {
            file += c - '0';
        }
        else {
            PieceType type = PieceFromLetter(c);
            if (type == PieceType::None || file > 7)
                return std::unexpected(std::format("Unexpected '{}' in the FEN.", c));
            position.m_board[MakeSquare(file++, rank)] = {type, std::isupper(static_cast<unsigned char>(c)) ? Color::White : Color::Black};
        }
        if (file > 8)
            return std::unexpected("A rank of the FEN has more than 8 squares.");
    }
    if (rank != 0 || file != 8)
        return std::unexpected("The FEN does not describe 8 ranks of 8 squares.");

    int kings[2] = {0, 0};
    for (Square square = 0; square < 64; ++square) {
        Piece piece = position.m_board[square];
        if (piece.type == PieceType::King) {
            ++kings[std::to_underlying(piece.color)];
            position.m_king[std::to_underlying(piece.color)] = static_cast<std::int8_t>(square);
        }
        if (piece.type == PieceType::Pawn && (RankOf(square) == 0 || RankOf(square) == 7))
            return std::unexpected("A pawn stands on the first or the last rank.");
    }
    if (kings[0] != 1 || kings[1] != 1)
        return std::unexpected("Each side needs exactly one king.");

    if (fields[1] == "w")
        position.m_side = Color::White;
    else if (fields[1] == "b")
        position.m_side = Color::Black;
    else
        return std::unexpected("The side to move must be 'w' or 'b'.");

    if (fields.size() > 2 && fields[2] != "-") {
        for (char c : fields[2]) {
            switch (c) {
            case 'K': position.m_castling |= castling::WhiteKingside; break;
            case 'Q': position.m_castling |= castling::WhiteQueenside; break;
            case 'k': position.m_castling |= castling::BlackKingside; break;
            case 'q': position.m_castling |= castling::BlackQueenside; break;
            default: return std::unexpected("Unexpected castling rights in the FEN.");
            }
        }
    }
    // Rights that the pieces do not allow are dropped.
    auto keepIf = [&](std::uint8_t right, Square king, Square rook, Color color) {
        if (!position.m_board[king].Is(PieceType::King, color) || !position.m_board[rook].Is(PieceType::Rook, color))
            position.m_castling &= static_cast<std::uint8_t>(~right);
    };
    keepIf(castling::WhiteKingside, MakeSquare(4, 0), MakeSquare(7, 0), Color::White);
    keepIf(castling::WhiteQueenside, MakeSquare(4, 0), MakeSquare(0, 0), Color::White);
    keepIf(castling::BlackKingside, MakeSquare(4, 7), MakeSquare(7, 7), Color::Black);
    keepIf(castling::BlackQueenside, MakeSquare(4, 7), MakeSquare(0, 7), Color::Black);

    if (fields.size() > 3 && fields[3] != "-") {
        std::string_view ep = fields[3];
        if (ep.size() != 2 || ep[0] < 'a' || ep[0] > 'h' || ep[1] < '1' || ep[1] > '8')
            return std::unexpected("Unexpected en passant square in the FEN.");
        Square square = MakeSquare(ep[0] - 'a', ep[1] - '1');
        Color us = position.m_side;
        int pawnRank = RankOf(square) + (us == Color::White ? -1 : 1);
        bool capturable = false;
        for (int df : {-1, 1}) {
            int f = FileOf(square) + df;
            if (IsInside(f, pawnRank) && position.m_board[MakeSquare(f, pawnRank)].Is(PieceType::Pawn, us))
                capturable = true;
        }
        if (capturable && RankOf(square) == (us == Color::White ? 5 : 2))
            position.m_enPassant = static_cast<std::int8_t>(square);
    }

    if (fields.size() > 4)
        position.m_halfmoveClock = static_cast<std::uint16_t>(std::clamp(std::atoi(std::string(fields[4]).c_str()), 0, 9999));
    if (fields.size() > 5)
        position.m_fullmove = static_cast<std::uint16_t>(std::clamp(std::atoi(std::string(fields[5]).c_str()), 1, 9999));

    if (position.IsAttacked(position.KingSquare(Opposite(position.m_side)), position.m_side))
        return std::unexpected("The side that is not to move is in check.");

    position.ComputeKey();
    return position;
}

std::string Position::ToFen() const
{
    std::string fen;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            Piece piece = m_board[MakeSquare(file, rank)];
            if (piece.Empty()) {
                ++empty;
                continue;
            }
            if (empty > 0)
                fen += static_cast<char>('0' + std::exchange(empty, 0));
            char letter = PieceLetter(piece.type);
            fen += piece.color == Color::White ? letter : static_cast<char>(std::tolower(letter));
        }
        if (empty > 0)
            fen += static_cast<char>('0' + empty);
        if (rank > 0)
            fen += '/';
    }

    fen += m_side == Color::White ? " w " : " b ";
    if (m_castling == 0)
        fen += '-';
    if (m_castling & castling::WhiteKingside) fen += 'K';
    if (m_castling & castling::WhiteQueenside) fen += 'Q';
    if (m_castling & castling::BlackKingside) fen += 'k';
    if (m_castling & castling::BlackQueenside) fen += 'q';
    fen += ' ';
    fen += m_enPassant >= 0 ? SquareName(m_enPassant) : "-";
    fen += std::format(" {} {}", m_halfmoveClock, m_fullmove);
    return fen;
}

void Position::Put(Square square, Piece piece)
{
    m_board[square] = piece;
    m_key ^= Zobrist.piece[PieceIndex(piece)][square];
}

void Position::Clear(Square square)
{
    m_key ^= Zobrist.piece[PieceIndex(m_board[square])][square];
    m_board[square] = {};
}

void Position::SetEnPassant(int square)
{
    if (m_enPassant >= 0)
        m_key ^= Zobrist.enPassant[FileOf(m_enPassant)];
    m_enPassant = static_cast<std::int8_t>(square);
    if (m_enPassant >= 0)
        m_key ^= Zobrist.enPassant[FileOf(m_enPassant)];
}

void Position::ComputeKey()
{
    m_key = 0;
    for (Square square = 0; square < 64; ++square)
        if (!m_board[square].Empty())
            m_key ^= Zobrist.piece[PieceIndex(m_board[square])][square];
    m_key ^= Zobrist.castling[m_castling];
    if (m_enPassant >= 0)
        m_key ^= Zobrist.enPassant[FileOf(m_enPassant)];
    if (m_side == Color::Black)
        m_key ^= Zobrist.side;
}

void Position::GenerateMoves(MoveList& moves) const
{
    for (Square square = 0; square < 64; ++square) {
        Piece piece = m_board[square];
        if (piece.Empty() || piece.color != m_side)
            continue;
        if (piece.type == PieceType::Pawn)
            AddPawnMoves(square, moves, false);
        else
            AddPieceMoves(square, piece, moves, false);
    }
    AddCastling(moves);
}

void Position::GenerateTactical(MoveList& moves) const
{
    for (Square square = 0; square < 64; ++square) {
        Piece piece = m_board[square];
        if (piece.Empty() || piece.color != m_side)
            continue;
        if (piece.type == PieceType::Pawn)
            AddPawnMoves(square, moves, true);
        else
            AddPieceMoves(square, piece, moves, true);
    }
}

void Position::AddPawnMoves(Square from, MoveList& moves, bool tacticalOnly) const
{
    const int forward = m_side == Color::White ? 8 : -8;
    const int startRank = m_side == Color::White ? 1 : 6;
    const int lastRank = m_side == Color::White ? 7 : 0;
    const auto u8 = [](int square) { return static_cast<std::uint8_t>(square); };

    auto addAdvance = [&](Square to, MoveFlag flag) {
        if (RankOf(to) != lastRank) {
            moves.Add({u8(from), u8(to), flag, PieceType::None});
        }
        else if (tacticalOnly) {
            moves.Add({u8(from), u8(to), flag, PieceType::Queen});
        }
        else {
            for (PieceType promotion : Promotions)
                moves.Add({u8(from), u8(to), flag, promotion});
        }
    };

    Square ahead = from + forward;
    if (m_board[ahead].Empty()) {
        if (!tacticalOnly || RankOf(ahead) == lastRank)
            addAdvance(ahead, MoveFlag::Normal);
        if (!tacticalOnly && RankOf(from) == startRank && m_board[ahead + forward].Empty())
            moves.Add({u8(from), u8(ahead + forward), MoveFlag::DoublePush, PieceType::None});
    }

    for (int df : {-1, 1}) {
        int file = FileOf(from) + df;
        if (file < 0 || file > 7)
            continue;
        Square target = ahead + df;
        Piece victim = m_board[target];
        if (!victim.Empty() && victim.color != m_side)
            addAdvance(target, MoveFlag::Normal);
        else if (target == m_enPassant)
            moves.Add({u8(from), u8(target), MoveFlag::EnPassant, PieceType::None});
    }
}

void Position::AddPieceMoves(Square from, Piece piece, MoveList& moves, bool capturesOnly) const
{
    auto add = [&](Square to) { moves.Add({static_cast<std::uint8_t>(from), static_cast<std::uint8_t>(to), MoveFlag::Normal, PieceType::None}); };

    if (piece.type == PieceType::Knight || piece.type == PieceType::King) {
        const LeaperTable& table = piece.type == PieceType::Knight ? Knight : King;
        for (int i = 0; i < table.count[from]; ++i) {
            Square to = table.targets[from][i];
            Piece target = m_board[to];
            if (target.Empty() ? !capturesOnly : target.color != m_side)
                add(to);
        }
        return;
    }

    int firstDirection = piece.type == PieceType::Bishop ? 4 : 0;
    int lastDirection = piece.type == PieceType::Rook ? 4 : 8;
    for (int d = firstDirection; d < lastDirection; ++d) {
        for (int i = 0; i < Rays.length[from][d]; ++i) {
            Square to = Rays.squares[from][d][i];
            Piece target = m_board[to];
            if (target.Empty()) {
                if (!capturesOnly)
                    add(to);
                continue;
            }
            if (target.color != m_side)
                add(to);
            break;
        }
    }
}

// The king may not castle out of, through or into check. The rights are lost when the king or the rook
// moves or the rook is taken, so with a right both stand on their squares.
void Position::AddCastling(MoveList& moves) const
{
    const bool white = m_side == Color::White;
    const std::uint8_t kingside = white ? castling::WhiteKingside : castling::BlackKingside;
    const std::uint8_t queenside = white ? castling::WhiteQueenside : castling::BlackQueenside;
    if ((m_castling & (kingside | queenside)) == 0)
        return;

    const int rank = white ? 0 : 7;
    const Color enemy = Opposite(m_side);
    const Square king = MakeSquare(4, rank);
    if (IsAttacked(king, enemy))
        return;

    auto empty = [&](int file) { return m_board[MakeSquare(file, rank)].Empty(); };
    auto safe = [&](int file) { return !IsAttacked(MakeSquare(file, rank), enemy); };

    if ((m_castling & kingside) && empty(5) && empty(6) && safe(5) && safe(6))
        moves.Add({static_cast<std::uint8_t>(king), static_cast<std::uint8_t>(MakeSquare(6, rank)), MoveFlag::CastleKingside, PieceType::None});
    if ((m_castling & queenside) && empty(1) && empty(2) && empty(3) && safe(3) && safe(2))
        moves.Add({static_cast<std::uint8_t>(king), static_cast<std::uint8_t>(MakeSquare(2, rank)), MoveFlag::CastleQueenside, PieceType::None});
}

bool Position::IsAttacked(Square square, Color by) const
{
    const int file = FileOf(square);
    const int pawnRank = RankOf(square) + (by == Color::White ? -1 : 1);
    if (pawnRank >= 0 && pawnRank < 8) {
        if (file > 0 && m_board[MakeSquare(file - 1, pawnRank)].Is(PieceType::Pawn, by))
            return true;
        if (file < 7 && m_board[MakeSquare(file + 1, pawnRank)].Is(PieceType::Pawn, by))
            return true;
    }

    for (int i = 0; i < Knight.count[square]; ++i)
        if (m_board[Knight.targets[square][i]].Is(PieceType::Knight, by))
            return true;
    for (int i = 0; i < King.count[square]; ++i)
        if (m_board[King.targets[square][i]].Is(PieceType::King, by))
            return true;

    for (int d = 0; d < 8; ++d) {
        const PieceType slider = d < 4 ? PieceType::Rook : PieceType::Bishop;
        for (int i = 0; i < Rays.length[square][d]; ++i) {
            Piece piece = m_board[Rays.squares[square][d][i]];
            if (piece.Empty())
                continue;
            if (piece.color == by && (piece.type == slider || piece.type == PieceType::Queen))
                return true;
            break;
        }
    }
    return false;
}

bool Position::MakeMove(Move move, Undo& undo)
{
    const Color us = m_side;
    const Color them = Opposite(us);
    const Piece piece = m_board[move.from];
    const Square capturedSquare = move.flag == MoveFlag::EnPassant ? (us == Color::White ? move.to - 8 : move.to + 8) : move.to;
    const Piece captured = m_board[capturedSquare];

    undo = {captured, m_castling, m_enPassant, m_halfmoveClock, m_key};

    if (!captured.Empty())
        Clear(capturedSquare);
    Clear(move.from);
    Put(move.to, move.promotion != PieceType::None ? Piece{move.promotion, us} : piece);

    if (move.IsCastling()) {
        const int rank = RankOf(move.from);
        const bool kingside = move.flag == MoveFlag::CastleKingside;
        const Square rookFrom = MakeSquare(kingside ? 7 : 0, rank);
        const Square rookTo = MakeSquare(kingside ? 5 : 3, rank);
        const Piece rook = m_board[rookFrom];
        Clear(rookFrom);
        Put(rookTo, rook);
    }
    if (piece.type == PieceType::King)
        m_king[std::to_underlying(us)] = static_cast<std::int8_t>(move.to);

    const std::uint8_t rights = m_castling & CastlingMask[move.from] & CastlingMask[move.to];
    if (rights != m_castling) {
        m_key ^= Zobrist.castling[m_castling] ^ Zobrist.castling[rights];
        m_castling = rights;
    }

    // An en passant square only counts when an enemy pawn stands ready to take there.
    int enPassant = -1;
    if (move.flag == MoveFlag::DoublePush) {
        const int file = FileOf(move.to);
        const int rank = RankOf(move.to);
        if ((file > 0 && m_board[MakeSquare(file - 1, rank)].Is(PieceType::Pawn, them)) ||
            (file < 7 && m_board[MakeSquare(file + 1, rank)].Is(PieceType::Pawn, them)))
            enPassant = (move.from + move.to) / 2;
    }
    SetEnPassant(enPassant);

    m_halfmoveClock = piece.type == PieceType::Pawn || !captured.Empty() ? 0 : static_cast<std::uint16_t>(m_halfmoveClock + 1);
    if (us == Color::Black)
        ++m_fullmove;
    m_side = them;
    m_key ^= Zobrist.side;

    if (IsAttacked(KingSquare(us), them)) {
        UnmakeMove(move, undo);
        return false;
    }
    return true;
}

void Position::UnmakeMove(Move move, const Undo& undo)
{
    const Color us = Opposite(m_side);
    m_side = us;
    if (us == Color::Black)
        --m_fullmove;

    Piece piece = m_board[move.to];
    if (move.promotion != PieceType::None)
        piece = {PieceType::Pawn, us};
    m_board[move.to] = {};
    m_board[move.from] = piece;

    if (move.IsCastling()) {
        const int rank = RankOf(move.from);
        const bool kingside = move.flag == MoveFlag::CastleKingside;
        const Square rookFrom = MakeSquare(kingside ? 7 : 0, rank);
        const Square rookTo = MakeSquare(kingside ? 5 : 3, rank);
        m_board[rookFrom] = m_board[rookTo];
        m_board[rookTo] = {};
    }
    if (!undo.captured.Empty()) {
        const Square capturedSquare = move.flag == MoveFlag::EnPassant ? (us == Color::White ? move.to - 8 : move.to + 8) : move.to;
        m_board[capturedSquare] = undo.captured;
    }
    if (piece.type == PieceType::King)
        m_king[std::to_underlying(us)] = static_cast<std::int8_t>(move.from);

    m_castling = undo.castling;
    m_enPassant = undo.enPassant;
    m_halfmoveClock = undo.halfmoveClock;
    m_key = undo.key;
}

void Position::MakeNullMove(Undo& undo)
{
    undo = {{}, m_castling, m_enPassant, m_halfmoveClock, m_key};
    SetEnPassant(-1);
    ++m_halfmoveClock;
    m_side = Opposite(m_side);
    m_key ^= Zobrist.side;
}

void Position::UnmakeNullMove(const Undo& undo)
{
    m_side = Opposite(m_side);
    m_enPassant = undo.enPassant;
    m_halfmoveClock = undo.halfmoveClock;
    m_key = undo.key;
}

bool Position::GivesCheck(Move move) const
{
    Position copy = *this;
    Undo undo;
    return copy.MakeMove(move, undo) && copy.InCheck();
}

bool Position::HasNonPawnMaterial(Color color) const
{
    for (Piece piece : m_board)
        if (piece.color == color && piece.type != PieceType::None && piece.type != PieceType::Pawn && piece.type != PieceType::King)
            return true;
    return false;
}

// No mate is possible with the kings and a single minor piece, or with bishops all on squares of one color.
bool Position::InsufficientMaterial() const
{
    int knights = 0, bishops = 0, bishopColors = 0;
    for (Square square = 0; square < 64; ++square) {
        switch (m_board[square].type) {
        case PieceType::Pawn:
        case PieceType::Rook:
        case PieceType::Queen:
            return false;
        case PieceType::Knight:
            ++knights;
            break;
        case PieceType::Bishop:
            ++bishops;
            bishopColors |= 1 << ((FileOf(square) + RankOf(square)) & 1);
            break;
        default:
            break;
        }
    }
    return knights + bishops <= 1 || (knights == 0 && bishopColors != 3);
}

std::vector<Move> Position::LegalMoves() const
{
    MoveList moves;
    GenerateMoves(moves);
    std::vector<Move> legal;
    Position copy = *this;
    for (Move move : moves) {
        Undo undo;
        if (copy.MakeMove(move, undo)) {
            legal.push_back(move);
            copy.UnmakeMove(move, undo);
        }
    }
    return legal;
}

std::vector<Move> Position::LegalMovesFrom(Square from) const
{
    std::vector<Move> moves = LegalMoves();
    std::erase_if(moves, [from](Move move) { return move.from != from; });
    return moves;
}

bool Position::HasLegalMove() const
{
    MoveList moves;
    GenerateMoves(moves);
    Position copy = *this;
    for (Move move : moves) {
        Undo undo;
        if (copy.MakeMove(move, undo))
            return true;
    }
    return false;
}

bool Position::IsLegal(Move move) const
{
    const std::vector<Move> moves = LegalMoves();
    return std::ranges::find(moves, move) != moves.end();
}

std::string Position::ToSan(Move move) const
{
    std::string san;
    if (move.IsCastling()) {
        san = move.flag == MoveFlag::CastleKingside ? "O-O" : "O-O-O";
    }
    else {
        const Piece piece = m_board[move.from];
        const bool capture = IsCapture(move);
        if (piece.type == PieceType::Pawn) {
            if (capture) {
                san += static_cast<char>('a' + FileOf(move.from));
                san += 'x';
            }
            san += SquareName(move.to);
            if (move.promotion != PieceType::None) {
                san += '=';
                san += PieceLetter(move.promotion);
            }
        }
        else {
            san += PieceLetter(piece.type);
            bool ambiguous = false, sameFile = false, sameRank = false;
            for (Move other : LegalMoves()) {
                if (other.to != move.to || other.from == move.from || m_board[other.from].type != piece.type)
                    continue;
                ambiguous = true;
                sameFile |= FileOf(other.from) == FileOf(move.from);
                sameRank |= RankOf(other.from) == RankOf(move.from);
            }
            if (ambiguous) {
                if (!sameFile)
                    san += static_cast<char>('a' + FileOf(move.from));
                else if (!sameRank)
                    san += static_cast<char>('1' + RankOf(move.from));
                else
                    san += SquareName(move.from);
            }
            if (capture)
                san += 'x';
            san += SquareName(move.to);
        }
    }

    Position after = *this;
    Undo undo;
    if (after.MakeMove(move, undo) && after.InCheck())
        san += after.HasLegalMove() ? '+' : '#';
    return san;
}

std::optional<Move> Position::ParseSan(std::string_view text) const
{
    const std::string wanted = NormalizeSan(Trim(text));
    if (wanted.empty())
        return std::nullopt;
    for (Move move : LegalMoves())
        if (NormalizeSan(ToSan(move)) == wanted)
            return move;
    // Some writers leave out the 'x' of captures, or the piece letter case differs.
    for (Move move : LegalMoves()) {
        std::string san = NormalizeSan(ToSan(move));
        std::erase(san, 'x');
        std::string loose = wanted;
        std::erase(loose, 'x');
        if (san == loose)
            return move;
    }
    return std::nullopt;
}

std::optional<Move> Position::ParseUci(std::string_view text) const
{
    text = Trim(text);
    if (text.size() < 4)
        return std::nullopt;
    auto square = [](std::string_view s) -> int {
        if (s[0] < 'a' || s[0] > 'h' || s[1] < '1' || s[1] > '8')
            return -1;
        return MakeSquare(s[0] - 'a', s[1] - '1');
    };
    const int from = square(text.substr(0, 2));
    const int to = square(text.substr(2, 2));
    const PieceType promotion = text.size() > 4 ? PieceFromLetter(text[4]) : PieceType::None;
    if (from < 0 || to < 0)
        return std::nullopt;
    for (Move move : LegalMoves())
        if (move.from == from && move.to == to && move.promotion == promotion)
            return move;
    return std::nullopt;
}

// Game ---------------------------------------------------------------------------------------------

void Game::Reset(const Position& start)
{
    m_positions.assign(1, start);
    m_moves.clear();
    m_redo.clear();
    UpdateStatus();
}

bool Game::Play(Move move)
{
    if (m_status.Over() || !Current().IsLegal(move))
        return false;
    Push(move);
    m_redo.clear();
    return true;
}

void Game::Push(Move move)
{
    const Position& position = Current();
    MoveRecord record;
    record.move = move;
    record.piece = position.At(move.from);
    record.capturedSquare = move.flag == MoveFlag::EnPassant ? MakeSquare(FileOf(move.to), RankOf(move.from)) : move.to;
    record.captured = position.At(record.capturedSquare);
    record.san = position.ToSan(move);

    Position next = position;
    Position::Undo undo;
    next.MakeMove(move, undo);
    m_positions.push_back(next);
    m_moves.push_back(std::move(record));
    UpdateStatus();
}

bool Game::Undo()
{
    if (m_moves.empty())
        return false;
    m_redo.push_back(std::move(m_moves.back()));
    m_moves.pop_back();
    m_positions.pop_back();
    UpdateStatus();
    return true;
}

bool Game::Redo()
{
    if (m_redo.empty())
        return false;
    Move move = m_redo.back().move;
    m_redo.pop_back();
    Push(move);
    return true;
}

void Game::UpdateStatus()
{
    const Position& position = Current();
    m_status = {};
    m_status.check = position.InCheck();

    if (!position.HasLegalMove()) {
        if (m_status.check) {
            m_status.result = position.SideToMove() == Color::White ? GameResult::BlackWins : GameResult::WhiteWins;
            m_status.reason = EndReason::Checkmate;
        }
        else {
            m_status.result = GameResult::Draw;
            m_status.reason = EndReason::Stalemate;
        }
        return;
    }

    auto draw = [&](EndReason reason) {
        m_status.result = GameResult::Draw;
        m_status.reason = reason;
    };
    if (position.InsufficientMaterial()) {
        draw(EndReason::InsufficientMaterial);
        return;
    }

    int repetitions = 0;
    const int first = std::max(0, static_cast<int>(m_positions.size()) - 1 - position.HalfmoveClock());
    for (int i = first; i < static_cast<int>(m_positions.size()); ++i)
        if (m_positions[i].Key() == position.Key())
            ++repetitions;
    if (repetitions >= 3)
        draw(EndReason::Repetition);
    else if (position.HalfmoveClock() >= 100)
        draw(EndReason::FiftyMoves);
}

std::vector<std::uint64_t> Game::RepetitionHistory() const
{
    std::vector<std::uint64_t> keys;
    const int last = static_cast<int>(m_positions.size()) - 1;
    const int first = std::max(0, last - Current().HalfmoveClock());
    for (int i = first; i < last; ++i)
        keys.push_back(m_positions[i].Key());
    return keys;
}

std::string Game::Serialize() const
{
    std::string text = Start().ToFen() + "|";
    for (const MoveRecord& record : m_moves)
        text += ToUci(record.move) + ' ';
    if (!text.empty() && text.back() == ' ')
        text.pop_back();
    return text;
}

std::optional<Game> Game::Deserialize(std::string_view text)
{
    const std::size_t bar = text.find('|');
    if (bar == std::string_view::npos)
        return std::nullopt;
    auto start = Position::FromFen(text.substr(0, bar));
    if (!start)
        return std::nullopt;

    Game game;
    game.Reset(*start);
    for (std::string_view token : SplitWhitespace(text.substr(bar + 1))) {
        auto move = game.Current().ParseUci(token);
        if (!move || !game.Play(*move))
            break;
    }
    return game;
}

std::expected<Game, std::string> Game::Parse(std::string_view text)
{
    text = Trim(text);
    if (text.empty())
        return std::unexpected("The text is empty.");

    // A FEN: 8 ranks separated by slashes in the first word.
    if (text.front() != '[') {
        std::string_view first = text.substr(0, text.find_first_of(" \t\r\n"));
        if (std::ranges::count(first, '/') == 7) {
            auto position = Position::FromFen(text);
            if (!position)
                return std::unexpected(position.error());
            Game game;
            game.Reset(*position);
            return game;
        }
    }

    // A PGN: the tags, then the moves.
    Position start = Position::Start();
    std::string moves;
    std::istringstream lines{std::string(text)};
    for (std::string line; std::getline(lines, line);) {
        std::string_view trimmed = Trim(line);
        if (trimmed.starts_with('[')) {
            if (trimmed.starts_with("[FEN ")) {
                std::size_t open = trimmed.find('"'), close = trimmed.rfind('"');
                if (open != std::string_view::npos && close > open) {
                    auto position = Position::FromFen(trimmed.substr(open + 1, close - open - 1));
                    if (!position)
                        return std::unexpected(position.error());
                    start = *position;
                }
            }
            continue;
        }
        moves += line;
        moves += '\n';
    }

    // Comments, variations and annotations are skipped.
    std::string cleaned;
    int variationDepth = 0;
    for (std::size_t i = 0; i < moves.size(); ++i) {
        char c = moves[i];
        if (c == '{') {
            std::size_t close = moves.find('}', i);
            i = close == std::string::npos ? moves.size() : close;
            cleaned += ' ';
        }
        else if (c == ';') {
            std::size_t end = moves.find('\n', i);
            i = end == std::string::npos ? moves.size() : end;
            cleaned += ' ';
        }
        else if (c == '(') {
            ++variationDepth;
        }
        else if (c == ')') {
            variationDepth = std::max(0, variationDepth - 1);
        }
        else if (variationDepth == 0) {
            cleaned += c;
        }
    }

    Game game;
    game.Reset(start);
    for (std::string_view token : SplitWhitespace(cleaned)) {
        // Move numbers may be glued to the moves: "1.e4", "12...Nf6".
        while (!token.empty() && (std::isdigit(static_cast<unsigned char>(token.front())) || token.front() == '.')) {
            std::string_view rest = token;
            while (!rest.empty() && std::isdigit(static_cast<unsigned char>(rest.front())))
                rest.remove_prefix(1);
            if (rest.empty() || rest.front() != '.')
                break;
            while (!rest.empty() && rest.front() == '.')
                rest.remove_prefix(1);
            token = rest;
        }
        if (token.empty() || token.starts_with('$') || token == "*" || token == "1-0" || token == "0-1" || token == "1/2-1/2")
            continue;
        auto move = game.Current().ParseSan(token);
        if (!move)
            return std::unexpected(std::format("The move '{}' is not legal here.", token));
        if (!game.Play(*move))
            break; // the game ended
    }
    return game;
}

std::string Game::ToPgn() const
{
    const auto today = std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now())};
    std::string result = "*";
    switch (m_status.result) {
    case GameResult::WhiteWins: result = "1-0"; break;
    case GameResult::BlackWins: result = "0-1"; break;
    case GameResult::Draw: result = "1/2-1/2"; break;
    default: break;
    }

    std::string pgn = std::format("[Event \"Casual game\"]\n[Site \"3D Chess\"]\n[Date \"{:04}.{:02}.{:02}\"]\n[Round \"-\"]\n[White \"White\"]\n[Black \"Black\"]\n[Result \"{}\"]\n",
                                  static_cast<int>(today.year()), static_cast<unsigned>(today.month()), static_cast<unsigned>(today.day()), result);
    const std::string startFen = Start().ToFen();
    if (startFen != StartFen)
        pgn += std::format("[SetUp \"1\"]\n[FEN \"{}\"]\n", startFen);
    pgn += '\n';

    std::string line;
    auto append = [&](const std::string& word) {
        if (line.size() + word.size() + 1 > 80) {
            pgn += line + '\n';
            line.clear();
        }
        if (!line.empty())
            line += ' ';
        line += word;
    };

    for (int ply = 0; ply < Ply(); ++ply) {
        const Position& position = m_positions[ply];
        if (position.SideToMove() == Color::White)
            append(std::format("{}. {}", position.FullmoveNumber(), m_moves[ply].san));
        else if (ply == 0)
            append(std::format("{}... {}", position.FullmoveNumber(), m_moves[ply].san));
        else
            append(m_moves[ply].san);
    }
    append(result);
    pgn += line + '\n';
    return pgn;
}

} // namespace chess
