#pragma once

#include "Chess.h"
#include "MathTypes.h"

#include <cstdint>
#include <vector>

// The board, its frame, the table and the hint arrow. A square is one unit; the playing surface is the
// plane y = 0, a1 is at the left front corner seen from White, and White sits on the +z side.
namespace board {

constexpr float FrameInner = 4.0f;
constexpr float FrameOuter = 4.62f;
constexpr float FrameTop = 0.03f;
constexpr float TableHeight = -0.32f;

struct Vertex {
    math::Vec3 position;
    math::Vec3 normal;
    math::Vec2 uv;
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

inline math::Vec3 SquareCenter(chess::Square square)
{
    return {static_cast<float>(chess::FileOf(square)) - 3.5f, 0, 3.5f - static_cast<float>(chess::RankOf(square))};
}

// The square under a point of the playing surface, or -1.
inline int SquareAt(math::Vec3 point)
{
    const int file = static_cast<int>(std::floor(point.x + 4));
    const int rank = static_cast<int>(std::floor(4 - point.z));
    return chess::IsInside(file, rank) ? chess::MakeSquare(file, rank) : -1;
}

Mesh BuildSquares();
Mesh BuildFrame();
Mesh BuildTable(float radius);
// An arrow lying on the board from one square's center to another's.
Mesh BuildArrow(math::Vec3 from, math::Vec3 to);

} // namespace board
