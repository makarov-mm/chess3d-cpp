#include "Engine.h"

#include <algorithm>
#include <random>
#include <utility>

namespace chess {

namespace {

constexpr int Infinity = 32000;
constexpr int MaxPly = 128;

enum Bound : std::uint8_t { NoBound, UpperBound, LowerBound, ExactBound };

// Piece-square tables from White's side, rank 8 first, after the "Simplified Evaluation Function".
// clang-format off
constexpr std::array<int, 64> PawnTable{
      0,  0,  0,  0,  0,  0,  0,  0,
     50, 50, 50, 50, 50, 50, 50, 50,
     10, 10, 20, 30, 30, 20, 10, 10,
      5,  5, 10, 25, 25, 10,  5,  5,
      0,  0,  0, 20, 20,  0,  0,  0,
      5, -5,-10,  0,  0,-10, -5,  5,
      5, 10, 10,-20,-20, 10, 10,  5,
      0,  0,  0,  0,  0,  0,  0,  0};
constexpr std::array<int, 64> KnightTable{
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50};
constexpr std::array<int, 64> BishopTable{
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -20,-10,-10,-10,-10,-10,-10,-20};
constexpr std::array<int, 64> RookTable{
      0,  0,  0,  0,  0,  0,  0,  0,
      5, 10, 10, 10, 10, 10, 10,  5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
      0,  0,  0,  5,  5,  0,  0,  0};
constexpr std::array<int, 64> QueenTable{
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5,  5,  5,  5,  0,-10,
     -5,  0,  5,  5,  5,  5,  0, -5,
      0,  0,  5,  5,  5,  5,  0, -5,
    -10,  5,  5,  5,  5,  5,  0,-10,
    -10,  0,  5,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20};
constexpr std::array<int, 64> KingMiddleTable{
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -10,-20,-20,-20,-20,-20,-20,-10,
     20, 20,  0,  0,  0,  0, 20, 20,
     20, 30, 10,  0,  0, 10, 30, 20};
constexpr std::array<int, 64> KingEndTable{
    -50,-40,-30,-20,-20,-30,-40,-50,
    -30,-20,-10,  0,  0,-10,-20,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-30,  0,  0,  0,  0,-30,-30,
    -50,-30,-30,-30,-30,-30,-30,-50};
// clang-format on

constexpr std::array<int, 7> MiddleValue{0, 100, 320, 330, 500, 950, 0};
constexpr std::array<int, 7> EndValue{0, 120, 310, 320, 540, 980, 0};
constexpr std::array<int, 7> PhaseWeight{0, 0, 1, 1, 2, 4, 0};
constexpr int FullPhase = 24;

// For a passed pawn, by the number of ranks it has advanced.
constexpr std::array<int, 8> PassedMiddle{0, 5, 10, 18, 30, 50, 80, 0};
constexpr std::array<int, 8> PassedEnd{0, 10, 18, 32, 55, 90, 140, 0};

int TableIndex(Square square, Color color)
{
    // The tables list rank 8 first; Black reads them mirrored.
    return color == Color::White ? (7 - RankOf(square)) * 8 + FileOf(square) : RankOf(square) * 8 + FileOf(square);
}

std::uint16_t Pack(Move move)
{
    return static_cast<std::uint16_t>(move.from | (move.to << 6) | (std::to_underlying(move.promotion) << 12));
}

int VictimValue(const Position& position, Move move)
{
    if (move.flag == MoveFlag::EnPassant)
        return MiddleValue[std::to_underlying(PieceType::Pawn)];
    return MiddleValue[std::to_underlying(position.At(move.to).type)];
}

} // namespace

Engine::Engine(int hashMegabytes)
{
    std::size_t entries = 1;
    while (entries * 2 * sizeof(Entry) <= static_cast<std::size_t>(hashMegabytes) * 1024 * 1024)
        entries *= 2;
    m_table.resize(entries);
}

void Engine::Clear()
{
    std::ranges::fill(m_table, Entry{});
    m_history = {};
    m_killers = {};
}

int Engine::Evaluate(const Position& position)
{
    int middle[2] = {0, 0}, end[2] = {0, 0};
    int phase = 0;
    int bishops[2] = {0, 0};
    // The least and the most advanced pawn of each side on each file, for the pawn structure.
    std::array<int, 8> whiteLowest, blackHighest;
    whiteLowest.fill(8);
    blackHighest.fill(-1);
    std::array<std::array<int, 8>, 2> pawnsOnFile{};

    for (Square square = 0; square < 64; ++square) {
        const Piece piece = position.At(square);
        if (piece.Empty())
            continue;
        const int side = std::to_underlying(piece.color);
        const int type = std::to_underlying(piece.type);
        const int index = TableIndex(square, piece.color);
        middle[side] += MiddleValue[type];
        end[side] += EndValue[type];
        phase += PhaseWeight[type];

        switch (piece.type) {
        case PieceType::Pawn:
            middle[side] += PawnTable[index];
            end[side] += PawnTable[index] / 2;
            ++pawnsOnFile[side][FileOf(square)];
            if (piece.color == Color::White)
                whiteLowest[FileOf(square)] = std::min(whiteLowest[FileOf(square)], RankOf(square));
            else
                blackHighest[FileOf(square)] = std::max(blackHighest[FileOf(square)], RankOf(square));
            break;
        case PieceType::Knight:
            middle[side] += KnightTable[index];
            end[side] += KnightTable[index];
            break;
        case PieceType::Bishop:
            middle[side] += BishopTable[index];
            end[side] += BishopTable[index];
            ++bishops[side];
            break;
        case PieceType::Rook:
            middle[side] += RookTable[index];
            end[side] += RookTable[index];
            break;
        case PieceType::Queen:
            middle[side] += QueenTable[index];
            end[side] += QueenTable[index];
            break;
        case PieceType::King:
            middle[side] += KingMiddleTable[index];
            end[side] += KingEndTable[index];
            break;
        default:
            break;
        }
    }

    for (int side = 0; side < 2; ++side) {
        if (bishops[side] >= 2) {
            middle[side] += 30;
            end[side] += 50;
        }
        for (int file = 0; file < 8; ++file) {
            const int count = pawnsOnFile[side][file];
            if (count > 1) {
                middle[side] -= 12 * (count - 1);
                end[side] -= 20 * (count - 1);
            }
            const bool supported = (file > 0 && pawnsOnFile[side][file - 1] > 0) || (file < 7 && pawnsOnFile[side][file + 1] > 0);
            if (count > 0 && !supported) {
                middle[side] -= 12;
                end[side] -= 15;
            }
        }
    }

    for (Square square = 0; square < 64; ++square) {
        const Piece piece = position.At(square);
        if (piece.type == PieceType::Pawn) {
            const int file = FileOf(square), rank = RankOf(square);
            bool passed = true;
            for (int f = std::max(0, file - 1); f <= std::min(7, file + 1); ++f) {
                if (piece.color == Color::White ? blackHighest[f] > rank : whiteLowest[f] < rank)
                    passed = false;
            }
            if (passed) {
                const int advanced = piece.color == Color::White ? rank - 1 : 6 - rank;
                const int side = std::to_underlying(piece.color);
                middle[side] += PassedMiddle[std::clamp(advanced, 0, 7)];
                end[side] += PassedEnd[std::clamp(advanced, 0, 7)];
            }
        }
        else if (piece.type == PieceType::Rook) {
            const int side = std::to_underlying(piece.color);
            const int file = FileOf(square);
            if (pawnsOnFile[side][file] == 0) {
                const bool open = pawnsOnFile[1 - side][file] == 0;
                middle[side] += open ? 20 : 10;
                end[side] += open ? 10 : 5;
            }
        }
    }

    // A pawn shield in front of a castled king.
    for (int side = 0; side < 2; ++side) {
        const Color color = static_cast<Color>(side);
        const Square king = position.KingSquare(color);
        const int file = FileOf(king);
        const int rank = RankOf(king);
        const int homeRank = color == Color::White ? 0 : 7;
        const int forward = color == Color::White ? 1 : -1;
        if (rank != homeRank || (file > 2 && file < 5))
            continue;
        for (int f = std::max(0, file - 1); f <= std::min(7, file + 1); ++f) {
            for (int step = 1; step <= 2; ++step) {
                const int r = rank + step * forward;
                if (r >= 0 && r < 8 && position.At(MakeSquare(f, r)).Is(PieceType::Pawn, color)) {
                    middle[side] += step == 1 ? 12 : 6;
                    break;
                }
            }
        }
    }

    phase = std::min(phase, FullPhase);
    const int middleScore = middle[0] - middle[1];
    const int endScore = end[0] - end[1];
    const int score = (middleScore * phase + endScore * (FullPhase - phase)) / FullPhase;
    const int tempo = 10;
    return (position.SideToMove() == Color::White ? score : -score) + tempo;
}

bool Engine::ShouldStop()
{
    if ((m_nodes & 1023) == 0 && (m_stop.stop_requested() || std::chrono::steady_clock::now() >= m_deadline))
        m_aborted = true;
    return m_aborted;
}

bool Engine::IsDraw() const
{
    if (m_position.HalfmoveClock() >= 100 || m_position.InsufficientMaterial())
        return true;
    // Any repetition within the search counts as a draw.
    const std::uint64_t key = m_position.Key();
    const int last = static_cast<int>(m_keys.size()) - 1;
    const int first = std::max(0, last - m_position.HalfmoveClock());
    for (int i = last - 2; i >= first; i -= 2)
        if (m_keys[i] == key)
            return true;
    return false;
}

int Engine::MoveScore(Move move, std::uint16_t ttMove, int ply) const
{
    if (Pack(move) == ttMove)
        return 10'000'000;
    if (m_position.IsCapture(move)) {
        const int attacker = MiddleValue[std::to_underlying(m_position.At(move.from).type)];
        return 1'000'000 + VictimValue(m_position, move) * 10 - attacker / 10;
    }
    if (move.promotion == PieceType::Queen)
        return 900'000;
    if (ply < MaxPly) {
        if (m_killers[ply][0] == move)
            return 800'000;
        if (m_killers[ply][1] == move)
            return 700'000;
    }
    return m_history[std::to_underlying(m_position.SideToMove())][move.from][move.to];
}

void Engine::OrderMoves(MoveList& moves, std::uint16_t ttMove, int ply) const
{
    std::array<int, 256> scores{};
    for (int i = 0; i < moves.count; ++i)
        scores[i] = MoveScore(moves.moves[i], ttMove, ply);
    // Insertion sort, best first: the lists are short.
    for (int i = 1; i < moves.count; ++i) {
        const Move move = moves.moves[i];
        const int score = scores[i];
        int j = i - 1;
        while (j >= 0 && scores[j] < score) {
            moves.moves[j + 1] = moves.moves[j];
            scores[j + 1] = scores[j];
            --j;
        }
        moves.moves[j + 1] = move;
        scores[j + 1] = score;
    }
}

void Engine::Store(int depth, int score, int bound, Move move, int ply)
{
    // Mate scores are stored as distances from this node, not from the root.
    if (score >= Mate - MaxPly)
        score += ply;
    else if (score <= -Mate + MaxPly)
        score -= ply;
    Entry& entry = m_table[m_position.Key() & (m_table.size() - 1)];
    if (entry.key == m_position.Key() && entry.depth > depth && bound != ExactBound)
        return;
    entry = {m_position.Key(), Pack(move), static_cast<std::int16_t>(score), static_cast<std::int8_t>(depth), static_cast<std::uint8_t>(bound)};
}

int Engine::Quiescence(int alpha, int beta, int ply)
{
    if (ShouldStop())
        return 0;
    ++m_nodes;
    if (ply >= MaxPly - 1)
        return Evaluate(m_position);

    const bool inCheck = m_position.InCheck();
    int best = -Infinity;
    if (!inCheck) {
        best = Evaluate(m_position);
        if (best >= beta)
            return best;
        alpha = std::max(alpha, best);
    }

    MoveList moves;
    if (inCheck)
        m_position.GenerateMoves(moves);
    else
        m_position.GenerateTactical(moves);
    OrderMoves(moves, 0, MaxPly);

    bool anyLegal = false;
    for (Move move : moves) {
        // A capture that cannot lift the score near alpha is not worth a look.
        if (!inCheck && move.promotion == PieceType::None && best + VictimValue(m_position, move) + 200 < alpha)
            continue;
        Position::Undo undo;
        if (!m_position.MakeMove(move, undo))
            continue;
        anyLegal = true;
        const int score = -Quiescence(-beta, -alpha, ply + 1);
        m_position.UnmakeMove(move, undo);
        if (m_aborted)
            return 0;
        if (score > best) {
            best = score;
            if (score >= beta)
                return score;
            alpha = std::max(alpha, score);
        }
    }
    if (inCheck && !anyLegal)
        return -Mate + ply;
    return best;
}

int Engine::Negamax(int depth, int alpha, int beta, int ply, bool allowNull)
{
    if (ShouldStop())
        return 0;
    const bool pvNode = beta - alpha > 1;

    if (ply > 0) {
        if (IsDraw())
            return 0;
        // A mate found closer to the root cannot be improved on.
        alpha = std::max(alpha, -Mate + ply);
        beta = std::min(beta, Mate - ply - 1);
        if (alpha >= beta)
            return alpha;
    }

    const bool inCheck = m_position.InCheck();
    if (inCheck)
        ++depth;
    if (depth <= 0 || ply >= MaxPly - 1)
        return Quiescence(alpha, beta, ply);
    ++m_nodes;

    std::uint16_t ttMove = 0;
    const Entry& entry = m_table[m_position.Key() & (m_table.size() - 1)];
    if (entry.key == m_position.Key()) {
        ttMove = entry.move;
        if (!pvNode && entry.depth >= depth) {
            int score = entry.score;
            if (score >= Mate - MaxPly)
                score -= ply;
            else if (score <= -Mate + MaxPly)
                score += ply;
            if (entry.bound == ExactBound || (entry.bound == LowerBound && score >= beta) || (entry.bound == UpperBound && score <= alpha))
                return score;
        }
    }

    const int staticEval = inCheck ? -Infinity : Evaluate(m_position);

    // Far above beta at a shallow depth: the opponent will not allow this line.
    if (!pvNode && !inCheck && depth <= 3 && staticEval - 120 * depth >= beta && std::abs(beta) < Mate - MaxPly)
        return staticEval;

    // Null move: if passing still fails high, a real move would too.
    if (allowNull && !pvNode && !inCheck && depth >= 3 && staticEval >= beta && m_position.HasNonPawnMaterial(m_position.SideToMove())) {
        const int reduction = 2 + depth / 4;
        Position::Undo undo;
        m_position.MakeNullMove(undo);
        m_keys.push_back(m_position.Key());
        const int score = -Negamax(depth - 1 - reduction, -beta, -beta + 1, ply + 1, false);
        m_keys.pop_back();
        m_position.UnmakeNullMove(undo);
        if (m_aborted)
            return 0;
        if (score >= beta)
            return score >= Mate - MaxPly ? beta : score;
    }

    MoveList moves;
    m_position.GenerateMoves(moves);
    OrderMoves(moves, ttMove, ply);

    const int originalAlpha = alpha;
    int best = -Infinity;
    Move bestMove;
    int legal = 0;

    for (Move move : moves) {
        const bool quiet = !m_position.IsCapture(move) && move.promotion == PieceType::None;
        Position::Undo undo;
        if (!m_position.MakeMove(move, undo))
            continue;
        ++legal;
        m_keys.push_back(m_position.Key());

        int score;
        if (legal == 1) {
            score = -Negamax(depth - 1, -beta, -alpha, ply + 1, true);
        }
        else {
            // Late quiet moves get a shallower look first.
            int reduction = 0;
            if (depth >= 3 && quiet && !inCheck && legal > 3 && !m_position.InCheck())
                reduction = 1 + (legal > 8 ? 1 : 0) + (depth > 6 ? 1 : 0);
            score = -Negamax(depth - 1 - reduction, -alpha - 1, -alpha, ply + 1, true);
            if (score > alpha && reduction > 0)
                score = -Negamax(depth - 1, -alpha - 1, -alpha, ply + 1, true);
            if (score > alpha && score < beta)
                score = -Negamax(depth - 1, -beta, -alpha, ply + 1, true);
        }

        m_keys.pop_back();
        m_position.UnmakeMove(move, undo);
        if (m_aborted)
            return 0;

        if (score > best) {
            best = score;
            bestMove = move;
            if (score > alpha) {
                alpha = score;
                if (alpha >= beta) {
                    if (quiet && ply < MaxPly) {
                        if (m_killers[ply][0] != move) {
                            m_killers[ply][1] = m_killers[ply][0];
                            m_killers[ply][0] = move;
                        }
                        int& history = m_history[std::to_underlying(m_position.SideToMove())][move.from][move.to];
                        history = std::min(history + depth * depth, 500'000);
                    }
                    break;
                }
            }
        }
    }

    if (legal == 0)
        return inCheck ? -Mate + ply : 0;

    Store(depth, best, best >= beta ? LowerBound : best > originalAlpha ? ExactBound : UpperBound, bestMove, ply);
    return best;
}

SearchResult Engine::Search(const Position& root, std::span<const std::uint64_t> history, const SearchLimits& limits, std::stop_token stop)
{
    const auto start = std::chrono::steady_clock::now();
    m_deadline = start + std::chrono::milliseconds(limits.milliseconds);
    m_stop = std::move(stop);
    m_aborted = false;
    m_nodes = 0;
    m_position = root;
    m_keys.assign(history.begin(), history.end());
    m_keys.push_back(root.Key());
    m_killers = {};
    for (auto& side : m_history)
        for (auto& from : side)
            for (int& value : from)
                value /= 8;

    struct RootMove {
        Move move;
        int score = -Infinity;
    };
    std::vector<RootMove> rootMoves;
    for (Move move : root.LegalMoves())
        rootMoves.push_back({move});
    if (rootMoves.empty())
        return {};
    {
        MoveList ordered;
        for (const RootMove& rootMove : rootMoves)
            ordered.Add(rootMove.move);
        OrderMoves(ordered, 0, 0);
        for (int i = 0; i < ordered.count; ++i)
            rootMoves[i].move = ordered.moves[i];
    }

    SearchResult result{rootMoves.front().move, 0, 0, 0};
    if (rootMoves.size() == 1 && limits.randomMargin == 0)
        return result;

    const bool exactScores = limits.randomMargin > 0;
    for (int depth = 1; depth <= limits.maxDepth; ++depth) {
        int alpha = -Infinity;
        const int beta = Infinity;
        Move iterationBest;
        int iterationScore = -Infinity;
        bool complete = true;

        for (std::size_t i = 0; i < rootMoves.size(); ++i) {
            RootMove& rootMove = rootMoves[i];
            Position::Undo undo;
            m_position.MakeMove(rootMove.move, undo);
            m_keys.push_back(m_position.Key());
            int score;
            if (i == 0 || exactScores) {
                score = -Negamax(depth - 1, -beta, exactScores ? Infinity : -alpha, 1, true);
            }
            else {
                score = -Negamax(depth - 1, -alpha - 1, -alpha, 1, true);
                if (!m_aborted && score > alpha)
                    score = -Negamax(depth - 1, -beta, -alpha, 1, true);
            }
            m_keys.pop_back();
            m_position.UnmakeMove(rootMove.move, undo);

            if (m_aborted) {
                complete = false;
                break;
            }
            rootMove.score = score;
            if (score > iterationScore) {
                iterationScore = score;
                iterationBest = rootMove.move;
            }
            alpha = std::max(alpha, score);
        }

        // An unfinished iteration still counts if a move beat the previous best, which it searched first.
        if (!iterationBest.IsNull() && (complete || iterationScore > result.score || result.depth == 0)) {
            result.best = iterationBest;
            result.score = iterationScore;
        }
        if (!complete)
            break;
        result.depth = depth;

        std::ranges::stable_sort(rootMoves, [](const RootMove& a, const RootMove& b) { return a.score > b.score; });
        const auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed > std::chrono::milliseconds(limits.milliseconds) / 2 || IsMateScore(iterationScore))
            break;
    }

    if (exactScores && result.depth > 0) {
        // The weaker levels pick among the good moves, the better ones more often.
        std::mt19937_64 random(limits.seed ^ root.Key());
        std::vector<RootMove> candidates;
        for (const RootMove& rootMove : rootMoves)
            if (rootMove.score >= rootMoves.front().score - limits.randomMargin)
                candidates.push_back(rootMove);
        std::vector<double> weights;
        for (const RootMove& candidate : candidates)
            weights.push_back(1.0 + static_cast<double>(limits.randomMargin - (rootMoves.front().score - candidate.score)) / std::max(1, limits.randomMargin) * 3.0);
        std::discrete_distribution<std::size_t> pick(weights.begin(), weights.end());
        const RootMove& chosen = candidates[pick(random)];
        result.best = chosen.move;
        result.score = chosen.score;
    }
    result.nodes = m_nodes;
    return result;
}

// AiPlayer -----------------------------------------------------------------------------------------

void AiPlayer::Start(const Position& position, std::vector<std::uint64_t> history, const SearchLimits& limits)
{
    Cancel();
    m_busy = true;
    m_worker = std::jthread([this, position, history = std::move(history), limits](std::stop_token stop) {
        SearchResult result = m_engine.Search(position, history, limits, stop);
        if (!stop.stop_requested()) {
            std::scoped_lock lock(m_mutex);
            m_result = result;
        }
        m_busy = false;
    });
}

void AiPlayer::Cancel()
{
    if (m_worker.joinable()) {
        m_worker.request_stop();
        m_worker.join();
    }
    m_busy = false;
    std::scoped_lock lock(m_mutex);
    m_result.reset();
}

void AiPlayer::NewGame()
{
    Cancel();
    m_engine.Clear();
}

std::optional<SearchResult> AiPlayer::TakeResult()
{
    std::scoped_lock lock(m_mutex);
    return std::exchange(m_result, std::nullopt);
}

} // namespace chess
