#pragma once

#include "Chess.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <thread>
#include <vector>

namespace chess {

struct SearchLimits {
    int maxDepth = 64;
    int milliseconds = 1000; // iterative deepening stops when this runs out
    int randomMargin = 0;    // a random move among those this many centipawns from the best, for the weaker levels
    std::uint64_t seed = 0;
};

struct SearchResult {
    Move best;
    int score = 0; // centipawns for the side to move
    int depth = 0;
    std::uint64_t nodes = 0;
};

// An alpha-beta searcher: iterative deepening, principal variation search, a transposition table,
// null-move pruning, late move reductions and a quiescence search over captures.
class Engine {
public:
    explicit Engine(int hashMegabytes = 32);

    SearchResult Search(const Position& root, std::span<const std::uint64_t> history, const SearchLimits& limits, std::stop_token stop);
    void Clear();

    // Material and position, in centipawns for the side to move.
    static int Evaluate(const Position& position);

    static constexpr int Mate = 31000;
    static bool IsMateScore(int score) { return std::abs(score) >= Mate - 256; }

private:
    struct Entry {
        std::uint64_t key = 0;
        std::uint16_t move = 0;
        std::int16_t score = 0;
        std::int8_t depth = 0;
        std::uint8_t bound = 0;
    };

    int Negamax(int depth, int alpha, int beta, int ply, bool allowNull);
    int Quiescence(int alpha, int beta, int ply);
    bool IsDraw() const;
    bool ShouldStop();
    void OrderMoves(MoveList& moves, std::uint16_t ttMove, int ply) const;
    int MoveScore(Move move, std::uint16_t ttMove, int ply) const;
    void Store(int depth, int score, int bound, Move move, int ply);

    std::vector<Entry> m_table;
    Position m_position;
    std::vector<std::uint64_t> m_keys; // the game's and the search path's, for repetitions
    std::array<std::array<Move, 2>, 128> m_killers{};
    std::array<std::array<std::array<int, 64>, 64>, 2> m_history{};
    std::uint64_t m_nodes = 0;
    std::chrono::steady_clock::time_point m_deadline;
    std::stop_token m_stop;
    bool m_aborted = false;
};

// Runs an engine on a thread of its own; the window polls for the result.
class AiPlayer {
public:
    explicit AiPlayer(int hashMegabytes = 32) : m_engine(hashMegabytes) {}
    ~AiPlayer() { Cancel(); }
    AiPlayer(const AiPlayer&) = delete;
    AiPlayer& operator=(const AiPlayer&) = delete;

    void Start(const Position& position, std::vector<std::uint64_t> history, const SearchLimits& limits);
    void Cancel();
    void NewGame();
    bool Busy() const { return m_busy; }
    std::optional<SearchResult> TakeResult();

private:
    Engine m_engine;
    std::jthread m_worker;
    std::mutex m_mutex;
    std::optional<SearchResult> m_result;
    std::atomic<bool> m_busy = false;
};

} // namespace chess
