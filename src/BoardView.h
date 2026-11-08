#pragma once

#include "Chess.h"
#include "MathTypes.h"
#include "Renderer.h"

#include <array>
#include <vector>

// The pieces as they stand on the board, and as they move: sliding, hopping, flying off to the table when
// taken, and dissolving when a pawn is promoted or a new game is set up.
class BoardView {
public:
    BoardView(const std::array<float, 6>& heights, const std::array<float, 6>& radii) : m_heights(heights), m_radii(radii) {}

    // Sets out the game's position and the pieces taken so far.
    void Rebuild(const chess::Game& game, bool animate);
    // A move played (forward) or taken back.
    void PlayMove(const chess::MoveRecord& record, bool forward);
    void Update(float seconds);
    bool Animating() const;

    // A piece held by the mouse, and let go again.
    void Hold(chess::Square square, math::Vec3 position);
    void Release(chess::Square square);

    // The square of the piece a ray hits first, or -1.
    int Pick(math::Vec3 origin, math::Vec3 direction) const;

    std::vector<render::PieceDraw> Draws(int selected, int hovered, math::Vec3 accent, float time) const;
    std::vector<render::ContactShadow> Contacts() const;

private:
    struct Piece {
        chess::PieceType type = chess::PieceType::Pawn;
        chess::Color color = chess::Color::White;
        int square = -1; // -1 on the table, -2 when leaving
        int slot = -1;   // on the table
        math::Vec3 position, start, end;
        float yaw = 0, yawStart = 0, yawEnd = 0; // the way the piece faces
        float arc = 0, progress = 1, duration = 0.3f, delay = 0;
        float dissolve = 0, dissolveFrom = 0, dissolveTo = 0, dissolveProgress = 1, dissolveDuration = 0.3f, dissolveDelay = 0;
        bool held = false;
        bool removeWhenGone = false;
    };

    Piece* At(int square);
    math::Vec3 TableSlot(chess::Color color, int slot) const;
    static float GroundAt(math::Vec3 position);
    static float FacingAt(chess::Piece piece, int square);
    static void MoveTo(Piece& piece, math::Vec3 target, float duration, float arc, float delay = 0);
    static void Fade(Piece& piece, float to, float duration, float delay = 0);
    void Place(chess::Piece piece, int square, bool appear, float delay);
    void PutOnTable(chess::Piece piece, bool appear, float delay);

    std::vector<Piece> m_pieces;
    std::array<int, 2> m_tableCounts{};
    std::array<float, 6> m_heights;
    std::array<float, 6> m_radii;
};
