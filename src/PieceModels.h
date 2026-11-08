#pragma once

#include "Chess.h"
#include "MathTypes.h"

#include <array>
#include <cstdint>
#include <vector>

// The chess pieces, modeled as signed distance functions and turned into triangle meshes.
namespace pieces {

struct Vertex {
    math::Vec3 position;
    math::Vec3 normal;
    float occlusion = 1; // ambient light reaching the surface, from the shape's own creases
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct PieceShape {
    Mesh detailed; // for the camera
    Mesh simple;   // for the shadow map and the reflections
    float height = 0;
    float radius = 0; // of the base
};

// The six shapes by chess::PieceType - 1, built in parallel. Units are board squares; the base stands
// on y = 0 and the knight looks along +z.
std::array<PieceShape, 6> BuildPieceShapes(float detail = 1.0f);

// The signed distance to a piece, negative inside.
float PieceDistance(chess::PieceType type, math::Vec3 p);

} // namespace pieces
