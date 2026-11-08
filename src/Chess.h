#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The rules of chess: positions, move generation, notation, and a game with its history.
namespace chess {

enum class Color : std::uint8_t { White, Black };

constexpr Color Opposite(Color color) { return color == Color::White ? Color::Black : Color::White; }

enum class PieceType : std::uint8_t { None, Pawn, Knight, Bishop, Rook, Queen, King };

struct Piece {
    PieceType type = PieceType::None;
    Color color = Color::White;

    constexpr bool Empty() const { return type == PieceType::None; }
    constexpr bool Is(PieceType t, Color c) const { return type == t && color == c; }
    constexpr bool operator==(const Piece&) const = default;
};

// 0 is a1, 7 is h1, 56 is a8, 63 is h8.
using Square = int;

constexpr int FileOf(Square square) { return square & 7; }
constexpr int RankOf(Square square) { return square >> 3; }
constexpr Square MakeSquare(int file, int rank) { return rank * 8 + file; }
constexpr bool IsInside(int file, int rank) { return file >= 0 && file < 8 && rank >= 0 && rank < 8; }
std::string SquareName(Square square);

enum class MoveFlag : std::uint8_t { Normal, DoublePush, EnPassant, CastleKingside, CastleQueenside };

struct Move {
    std::uint8_t from = 0;
    std::uint8_t to = 0;
    MoveFlag flag = MoveFlag::Normal;
    PieceType promotion = PieceType::None;

    constexpr bool IsNull() const { return from == to; }
    constexpr bool IsCastling() const { return flag == MoveFlag::CastleKingside || flag == MoveFlag::CastleQueenside; }
    constexpr bool operator==(const Move&) const = default;
};

// "e2e4", "e7e8q"
std::string ToUci(Move move);

struct MoveList {
    std::array<Move, 256> moves{};
    int count = 0;

    void Add(Move move) { moves[count++] = move; }
    Move* begin() { return moves.data(); }
    Move* end() { return moves.data() + count; }
    const Move* begin() const { return moves.data(); }
    const Move* end() const { return moves.data() + count; }
};

namespace castling {
constexpr std::uint8_t WhiteKingside = 1, WhiteQueenside = 2, BlackKingside = 4, BlackQueenside = 8, All = 15;
}

class Position {
public:
    // What taking a move back needs, besides the move.
    struct Undo {
        Piece captured;
        std::uint8_t castling = 0;
        std::int8_t enPassant = -1;
        std::uint16_t halfmoveClock = 0;
        std::uint64_t key = 0;
    };

    static Position Start();
    static std::expected<Position, std::string> FromFen(std::string_view fen);
    std::string ToFen() const;

    Piece At(Square square) const { return m_board[square]; }
    Color SideToMove() const { return m_side; }
    std::uint8_t CastlingRights() const { return m_castling; }
    // The square a pawn has just passed over, if the side to move has a pawn that could take there; else -1.
    int EnPassantSquare() const { return m_enPassant; }
    int HalfmoveClock() const { return m_halfmoveClock; }
    int FullmoveNumber() const { return m_fullmove; }
    std::uint64_t Key() const { return m_key; }
    Square KingSquare(Color color) const { return m_king[std::to_underlying(color)]; }

    // The moves by the movement rules; some may leave the king in check.
    void GenerateMoves(MoveList& moves) const;
    // Captures and promotions, for the quiescence search.
    void GenerateTactical(MoveList& moves) const;

    // Plays a pseudo-legal move; returns false, with the position unchanged, if it leaves the mover in check.
    bool MakeMove(Move move, Undo& undo);
    void UnmakeMove(Move move, const Undo& undo);
    void MakeNullMove(Undo& undo);
    void UnmakeNullMove(const Undo& undo);

    bool IsAttacked(Square square, Color by) const;
    bool InCheck() const { return IsAttacked(KingSquare(m_side), Opposite(m_side)); }
    bool GivesCheck(Move move) const;
    bool IsCapture(Move move) const { return move.flag == MoveFlag::EnPassant || !m_board[move.to].Empty(); }
    bool HasNonPawnMaterial(Color color) const;
    bool InsufficientMaterial() const;

    std::vector<Move> LegalMoves() const;
    std::vector<Move> LegalMovesFrom(Square from) const;
    bool HasLegalMove() const;
    bool IsLegal(Move move) const;

    // Standard algebraic notation of a legal move: "Nf3", "exd5", "O-O", "e8=Q+".
    std::string ToSan(Move move) const;
    std::optional<Move> ParseSan(std::string_view text) const;
    std::optional<Move> ParseUci(std::string_view text) const;

private:
    void Put(Square square, Piece piece);
    void Clear(Square square);
    void SetEnPassant(int square);
    void ComputeKey();
    void AddPawnMoves(Square from, MoveList& moves, bool tacticalOnly) const;
    void AddPieceMoves(Square from, Piece piece, MoveList& moves, bool capturesOnly) const;
    void AddCastling(MoveList& moves) const;

    std::array<Piece, 64> m_board{};
    std::array<std::int8_t, 2> m_king{};
    Color m_side = Color::White;
    std::uint8_t m_castling = 0;
    std::int8_t m_enPassant = -1;
    std::uint16_t m_halfmoveClock = 0;
    std::uint16_t m_fullmove = 1;
    std::uint64_t m_key = 0;
};

enum class GameResult : std::uint8_t { Ongoing, WhiteWins, BlackWins, Draw };
enum class EndReason : std::uint8_t { None, Checkmate, Stalemate, Repetition, FiftyMoves, InsufficientMaterial };

struct GameStatus {
    GameResult result = GameResult::Ongoing;
    EndReason reason = EndReason::None;
    bool check = false;

    constexpr bool Over() const { return result != GameResult::Ongoing; }
};

struct MoveRecord {
    Move move;
    Piece piece;           // the piece that moved, before any promotion
    Piece captured;        // empty if none
    Square capturedSquare; // differs from move.to for en passant
    std::string san;
};

// A game: the starting position, the moves played, and the moves taken back that can be replayed.
class Game {
public:
    Game() { Reset(Position::Start()); }

    void Reset(const Position& start);
    // A FEN position or a PGN game (tags, move numbers, comments and variations are skipped).
    static std::expected<Game, std::string> Parse(std::string_view text);
    // The starting FEN and the moves in UCI notation, for saving.
    std::string Serialize() const;
    static std::optional<Game> Deserialize(std::string_view text);

    const Position& Current() const { return m_positions.back(); }
    const Position& Start() const { return m_positions.front(); }
    const Position& PositionAt(int ply) const { return m_positions[ply]; }
    std::span<const MoveRecord> Moves() const { return m_moves; }
    int Ply() const { return static_cast<int>(m_moves.size()); }
    GameStatus Status() const { return m_status; }

    bool Play(Move move); // only legal moves are played
    bool CanUndo() const { return !m_moves.empty(); }
    bool CanRedo() const { return !m_redo.empty(); }
    bool Undo();
    bool Redo();
    const MoveRecord* NextRedo() const { return m_redo.empty() ? nullptr : &m_redo.back(); }

    // The keys of the earlier positions that a repetition could still return to, for the engine.
    std::vector<std::uint64_t> RepetitionHistory() const;

    std::string ToPgn() const;

private:
    void Push(Move move);
    void UpdateStatus();

    std::vector<Position> m_positions;
    std::vector<MoveRecord> m_moves;
    std::vector<MoveRecord> m_redo;
    GameStatus m_status;
};

} // namespace chess
