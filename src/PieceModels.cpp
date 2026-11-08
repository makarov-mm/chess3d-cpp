#include "PieceModels.h"

#include <cmath>
#include <limits>
#include <thread>
#include <utility>

namespace pieces {

using namespace math;

namespace {

// Distance functions ------------------------------------------------------------------------------

float SmoothMin(float a, float b, float k)
{
    const float h = std::max(k - std::abs(a - b), 0.0f) / k;
    return std::min(a, b) - h * h * k * 0.25f;
}

float SmoothMax(float a, float b, float k) { return -SmoothMin(-a, -b, k); }

// The profile plane of a turned shape: distance from the axis, and height.
Vec2 Lathe(Vec3 p) { return {std::sqrt(p.x * p.x + p.z * p.z), p.y}; }

float Circle(Vec2 p, Vec2 center, float radius) { return Length(p - center) - radius; }

float Box(Vec2 p, Vec2 center, Vec2 halfSize, float rounding)
{
    const float dx = std::abs(p.x - center.x) - halfSize.x + rounding;
    const float dy = std::abs(p.y - center.y) - halfSize.y + rounding;
    return Length(Vec2{std::max(dx, 0.0f), std::max(dy, 0.0f)}) + std::min(std::max(dx, dy), 0.0f) - rounding;
}

// A cone section around the axis, from radius r0 at height y0 to radius r1 at height y1.
float Cone(Vec2 p, float y0, float r0, float y1, float r1)
{
    const float halfHeight = (y1 - y0) / 2;
    const Vec2 q{std::abs(p.x), p.y - (y0 + y1) / 2};
    const Vec2 k1{r1, halfHeight};
    const Vec2 k2{r1 - r0, 2 * halfHeight};
    const Vec2 ca{q.x - std::min(q.x, q.y < 0 ? r0 : r1), std::abs(q.y) - halfHeight};
    const Vec2 cb = q - k1 + k2 * std::clamp(Dot(k1 - q, k2) / Dot(k2, k2), 0.0f, 1.0f);
    const float sign = cb.x < 0 && ca.y < 0 ? -1.0f : 1.0f;
    return sign * std::sqrt(std::min(Dot(ca, ca), Dot(cb, cb)));
}

// The same with its rims rounded off.
float RoundedCone(Vec2 p, float y0, float r0, float y1, float r1, float rounding)
{
    return Cone(p, y0 + rounding, r0 - rounding, y1 - rounding, r1 - rounding) - rounding;
}

float Ellipse(Vec2 p, Vec2 center, Vec2 radii)
{
    const Vec2 q{(p.x - center.x) / radii.x, (p.y - center.y) / radii.y};
    const float k0 = Length(q);
    const float k1 = Length(Vec2{q.x / radii.x, q.y / radii.y});
    return k1 > 1e-6f ? k0 * (k0 - 1) / k1 : -std::min(radii.x, radii.y);
}

// A capsule whose radius changes from ra at a to rb at b, in 2D or 3D.
template <typename V>
float RoundCone(V p, V a, V b, float ra, float rb)
{
    const V ba = b - a;
    const float l2 = Dot(ba, ba);
    const float rr = ra - rb;
    const float a2 = l2 - rr * rr;
    const float il2 = 1.0f / l2;
    const V pa = p - a;
    const float y = Dot(pa, ba);
    const float z = y - l2;
    const V w = pa * l2 - ba * y;
    const float x2 = Dot(w, w);
    const float y2 = y * y * l2;
    const float z2 = z * z * l2;
    const float k = (rr > 0 ? 1.0f : -1.0f) * rr * rr * x2;
    if ((z > 0 ? 1.0f : -1.0f) * a2 * z2 > k)
        return std::sqrt(x2 + z2) * il2 - rb;
    if ((y > 0 ? 1.0f : -1.0f) * a2 * y2 < k)
        return std::sqrt(x2 + y2) * il2 - ra;
    return (std::sqrt(x2 * a2 * il2) + y * rr) * il2 - ra;
}

float Sphere(Vec3 p, Vec3 center, float radius) { return Length(p - center) - radius; }

float Box(Vec3 p, Vec3 halfSize, float rounding)
{
    const Vec3 q{std::abs(p.x) - halfSize.x + rounding, std::abs(p.y) - halfSize.y + rounding, std::abs(p.z) - halfSize.z + rounding};
    return Length(Vec3{std::max(q.x, 0.0f), std::max(q.y, 0.0f), std::max(q.z, 0.0f)}) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f) -
           rounding;
}

// A 2D shape in the plane of y and z, given thickness across x, with rounded edges.
float Extrude(float distance2D, float x, float halfWidth, float rounding)
{
    const float wx = distance2D + rounding;
    const float wy = std::abs(x) - halfWidth + rounding;
    return std::min(std::max(wx, wy), 0.0f) + Length(Vec2{std::max(wx, 0.0f), std::max(wy, 0.0f)}) - rounding;
}

Vec3 RotateZ(Vec3 p, float angle)
{
    const float c = std::cos(angle), s = std::sin(angle);
    return {c * p.x - s * p.y, s * p.x + c * p.y, p.z};
}

// p turned about the y axis into the sector of the nearest of `count` copies around it; the copies sit on +x.
Vec3 RepeatAround(Vec3 p, int count, float offset = 0)
{
    const float sector = 2 * Pi / static_cast<float>(count);
    const float angle = std::atan2(p.z, p.x) - offset;
    const float local = angle - sector * std::round(angle / sector);
    const float radius = std::sqrt(p.x * p.x + p.z * p.z);
    return {radius * std::cos(local), p.y, radius * std::sin(local)};
}

// The foot every piece stands on: a rounded disc, a bead and a step.
float Foot(Vec2 q, float radius)
{
    float d = Box(q, {0, 0.034f}, {radius, 0.034f}, 0.016f);
    d = SmoothMin(d, Circle(q, {radius * 0.88f, 0.078f}, 0.022f), 0.014f);
    d = SmoothMin(d, Box(q, {0, 0.105f}, {radius * 0.76f, 0.017f}, 0.008f), 0.012f);
    return d;
}

float Collar(Vec2 q, float y, float radius, float halfHeight)
{
    return Box(q, {0, y}, {radius, halfHeight}, halfHeight * 0.65f);
}

// The pieces ---------------------------------------------------------------------------------------

float Pawn(Vec3 p)
{
    const Vec2 q = Lathe(p);
    float d = Foot(q, 0.25f);
    d = SmoothMin(d, Cone(q, 0.10f, 0.165f, 0.45f, 0.072f), 0.06f);
    d = SmoothMin(d, Collar(q, 0.452f, 0.142f, 0.021f), 0.03f);
    d = SmoothMin(d, Circle(q, {0, 0.6f}, 0.132f), 0.045f);
    return d;
}

float Rook(Vec3 p)
{
    const Vec2 q = Lathe(p);
    float d = Foot(q, 0.27f);
    d = SmoothMin(d, Cone(q, 0.10f, 0.2f, 0.64f, 0.158f), 0.06f);
    d = SmoothMin(d, Collar(q, 0.635f, 0.2f, 0.022f), 0.025f);
    d = SmoothMin(d, RoundedCone(q, 0.655f, 0.185f, 0.86f, 0.212f, 0.012f), 0.02f);
    d = SmoothMax(d, -Box(q, {0, 0.98f}, {0.13f, 0.2f}, 0.0f), 0.02f); // the hollow top
    const Vec3 notchSpace = RepeatAround(p, 5, Pi / 2);
    const float notch = Box(notchSpace - Vec3{0.2f, 0.9f, 0}, {0.12f, 0.115f, 0.036f}, 0.01f);
    return SmoothMax(d, -notch, 0.016f);
}

float Knight(Vec3 p)
{
    const Vec2 q = Lathe(p);
    float d = Foot(q, 0.28f);
    d = SmoothMin(d, Cone(q, 0.10f, 0.21f, 0.2f, 0.175f), 0.04f);
    d = SmoothMin(d, Collar(q, 0.2f, 0.192f, 0.02f), 0.02f);

    // The head: a side outline in the plane of z (forward) and y, given thickness across x.
    const Vec2 s{p.z, p.y};
    float outline = RoundCone(s, Vec2{0.02f, 0.27f}, Vec2{-0.04f, 0.72f}, 0.165f, 0.12f); // neck
    outline = SmoothMin(outline, RoundCone(s, Vec2{-0.015f, 0.855f}, Vec2{0.255f, 0.655f}, 0.118f, 0.07f), 0.09f); // head
    outline = SmoothMin(outline, Circle(s, {0.09f, 0.37f}, 0.115f), 0.09f); // chest
    outline = SmoothMax(outline, -Circle(s, {0.225f, 0.47f}, 0.085f), 0.06f); // throat
    outline = SmoothMax(outline, -Circle(s, {-0.33f, 0.5f}, 0.16f), 0.08f);  // the hollow of the back of the neck
    const float halfWidth = Lerp(0.118f, 0.074f, Saturate((p.z + 0.03f) / 0.3f)) + 0.012f * Saturate((0.62f - p.y) / 0.3f);
    float head = Extrude(outline, p.x, halfWidth, 0.055f);

    // The mane, a wavy ridge down the back of the neck.
    const float maneOutline = RoundCone(s, Vec2{-0.125f, 0.33f}, Vec2{-0.085f, 0.9f}, 0.055f, 0.048f) - 0.006f * std::sin(p.y * 70.0f);
    head = SmoothMin(head, Extrude(maneOutline, p.x, 0.045f, 0.035f), 0.035f);

    // The ears.
    const Vec3 ear{std::abs(p.x), p.y, p.z};
    head = SmoothMin(head, RoundCone(ear, Vec3{0.045f, 0.93f, -0.035f}, Vec3{0.058f, 1.035f, -0.075f}, 0.034f, 0.008f), 0.03f);

    // The eyes and the nostrils.
    head = SmoothMax(head, -Sphere(ear, {0.118f, 0.82f, 0.05f}, 0.028f), 0.012f);
    head = SmoothMax(head, -Sphere(ear, {0.045f, 0.655f, 0.315f}, 0.022f), 0.01f);

    return SmoothMin(d, head, 0.05f);
}

float Bishop(Vec3 p)
{
    const Vec2 q = Lathe(p);
    float d = Foot(q, 0.27f);
    d = SmoothMin(d, Cone(q, 0.10f, 0.175f, 0.66f, 0.068f), 0.07f);
    d = SmoothMin(d, Collar(q, 0.655f, 0.15f, 0.019f), 0.025f);
    d = SmoothMin(d, Collar(q, 0.692f, 0.112f, 0.014f), 0.015f);
    float mitre = Ellipse(q, {0, 0.865f}, {0.128f, 0.19f});
    mitre = SmoothMin(mitre, Cone(q, 0.93f, 0.08f, 1.075f, 0.0f), 0.06f);
    d = SmoothMin(d, mitre, 0.035f);
    d = SmoothMin(d, Circle(q, {0, 1.095f}, 0.034f), 0.022f);

    // The slit, a slanting cut into the front of the mitre.
    const Vec3 slit = RotateZ(p - Vec3{0, 0.92f, 0.1f}, 0.6f);
    return SmoothMax(d, -Box(slit, {0.2f, 0.014f, 0.11f}, 0.008f), 0.012f);
}

float Queen(Vec3 p)
{
    const Vec2 q = Lathe(p);
    float d = Foot(q, 0.29f);
    d = SmoothMin(d, Cone(q, 0.10f, 0.185f, 0.82f, 0.078f), 0.08f);
    d = SmoothMin(d, Collar(q, 0.81f, 0.16f, 0.019f), 0.028f);
    d = SmoothMin(d, Collar(q, 0.848f, 0.125f, 0.014f), 0.016f);

    // The coronet: a flaring cup with eight points, each with a ball.
    float crown = RoundedCone(q, 0.86f, 0.095f, 1.09f, 0.178f, 0.01f);
    crown = SmoothMax(crown, -Box(q, {0, 1.2f}, {0.128f, 0.13f}, 0.0f), 0.015f);
    const Vec3 between = RepeatAround(p, 8, Pi / 8);
    crown = SmoothMax(crown, -Sphere(between, {0.19f, 1.155f, 0}, 0.075f), 0.012f);
    d = SmoothMin(d, crown, 0.025f);
    const Vec3 point = RepeatAround(p, 8);
    d = SmoothMin(d, Sphere(point, {0.158f, 1.103f, 0}, 0.024f), 0.012f);

    d = SmoothMin(d, Ellipse(q, {0, 1.075f}, {0.112f, 0.075f}), 0.02f);
    d = SmoothMin(d, Circle(q, {0, 1.2f}, 0.042f), 0.03f);
    return d;
}

float King(Vec3 p)
{
    const Vec2 q = Lathe(p);
    float d = Foot(q, 0.3f);
    d = SmoothMin(d, Cone(q, 0.10f, 0.195f, 0.9f, 0.082f), 0.08f);
    d = SmoothMin(d, Collar(q, 0.89f, 0.165f, 0.019f), 0.028f);
    d = SmoothMin(d, Collar(q, 0.93f, 0.13f, 0.014f), 0.016f);
    d = SmoothMin(d, Cone(q, 0.94f, 0.1f, 1.12f, 0.162f), 0.03f);
    d = SmoothMin(d, Ellipse(q, {0, 1.12f}, {0.16f, 0.045f}), 0.02f);
    d = SmoothMin(d, Ellipse(q, {0, 1.15f}, {0.115f, 0.075f}), 0.02f);

    const float upright = Box(p - Vec3{0, 1.315f, 0}, {0.027f, 0.12f, 0.027f}, 0.011f);
    const float arms = Box(p - Vec3{0, 1.345f, 0}, {0.088f, 0.027f, 0.027f}, 0.011f);
    return SmoothMin(d, std::min(upright, arms), 0.035f);
}

// Meshing ------------------------------------------------------------------------------------------

struct Bounds {
    Vec3 low, high;
};

Bounds PieceBounds(chess::PieceType type)
{
    switch (type) {
    case chess::PieceType::Pawn: return {{-0.28f, -0.03f, -0.28f}, {0.28f, 0.78f, 0.28f}};
    case chess::PieceType::Rook: return {{-0.3f, -0.03f, -0.3f}, {0.3f, 0.9f, 0.3f}};
    case chess::PieceType::Knight: return {{-0.31f, -0.03f, -0.31f}, {0.31f, 1.08f, 0.38f}};
    case chess::PieceType::Bishop: return {{-0.3f, -0.03f, -0.3f}, {0.3f, 1.16f, 0.3f}};
    case chess::PieceType::Queen: return {{-0.32f, -0.03f, -0.32f}, {0.32f, 1.28f, 0.32f}};
    default: return {{-0.33f, -0.03f, -0.33f}, {0.33f, 1.5f, 0.33f}};
    }
}

template <typename Sdf>
Vec3 Gradient(const Sdf& sdf, Vec3 p)
{
    // The tetrahedron technique: four samples.
    constexpr float h = 0.0006f;
    const Vec3 a{1, -1, -1}, b{-1, -1, 1}, c{-1, 1, -1}, d{1, 1, 1};
    return (a * sdf(p + a * h) + b * sdf(p + b * h) + c * sdf(p + c * h) + d * sdf(p + d * h)) / (4 * h);
}

template <typename Sdf>
float Occlusion(const Sdf& sdf, Vec3 p, Vec3 n)
{
    float occlusion = 0, weight = 1;
    for (int i = 0; i < 5; ++i) {
        const float h = 0.01f + 0.11f * static_cast<float>(i) / 4;
        occlusion += (h - sdf(p + n * h)) * weight;
        weight *= 0.92f;
    }
    return Saturate(1 - 2.6f * occlusion);
}

// Surface nets: a vertex in each grid cell the surface passes through, placed on the surface, and a
// quad across each grid edge the surface crosses. Only cells near the surface are sampled finely.
template <typename Sdf>
Mesh Polygonize(const Sdf& sdf, Bounds bounds, float step)
{
    constexpr int Block = 4; // fine cells per coarse cell along each axis
    auto cellsAlong = [&](float extent) {
        const int cells = static_cast<int>(std::ceil(extent / step));
        return (cells + Block - 1) / Block * Block;
    };
    const int cx = cellsAlong(bounds.high.x - bounds.low.x);
    const int cy = cellsAlong(bounds.high.y - bounds.low.y);
    const int cz = cellsAlong(bounds.high.z - bounds.low.z);
    const int nx = cx + 1, ny = cy + 1, nz = cz + 1; // grid points
    const Vec3 origin = bounds.low;

    auto point = [&](int i, int j, int k) { return origin + Vec3{static_cast<float>(i), static_cast<float>(j), static_cast<float>(k)} * step; };

    // Coarse cells that the surface may cross: some corner is within the cell's diagonal of it.
    const int bx = cx / Block, by = cy / Block, bz = cz / Block;
    const float coarseStep = step * Block;
    std::vector<float> coarse(static_cast<std::size_t>((bx + 1) * (by + 1) * (bz + 1)));
    for (int k = 0; k <= bz; ++k)
        for (int j = 0; j <= by; ++j)
            for (int i = 0; i <= bx; ++i)
                coarse[(k * (by + 1) + j) * (bx + 1) + i] = sdf(point(i * Block, j * Block, k * Block));

    const float reach = coarseStep * 1.75f;
    std::vector<int> nearBlocks;
    for (int k = 0; k < bz; ++k)
        for (int j = 0; j < by; ++j)
            for (int i = 0; i < bx; ++i) {
                float closest = std::numeric_limits<float>::max();
                for (int corner = 0; corner < 8; ++corner) {
                    const int ii = i + (corner & 1), jj = j + ((corner >> 1) & 1), kk = k + (corner >> 2);
                    closest = std::min(closest, std::abs(coarse[(kk * (by + 1) + jj) * (bx + 1) + ii]));
                }
                if (closest <= reach)
                    nearBlocks.push_back((k * by + j) * bx + i);
            }

    std::vector<float> values(static_cast<std::size_t>(nx) * ny * nz, std::numeric_limits<float>::quiet_NaN());
    auto value = [&](int i, int j, int k) {
        float& v = values[(static_cast<std::size_t>(k) * ny + j) * nx + i];
        if (std::isnan(v))
            v = sdf(point(i, j, k));
        return v;
    };
    auto cellIndex = [&](int i, int j, int k) { return (static_cast<std::size_t>(k) * cy + j) * cx + i; };

    Mesh mesh;
    std::vector<std::int32_t> cellVertex(static_cast<std::size_t>(cx) * cy * cz, -1);
    std::vector<std::array<int, 3>> activeCells;

    static constexpr int Edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (int block : nearBlocks) {
        const int bi = block % bx, bj = (block / bx) % by, bk = block / (bx * by);
        for (int k = bk * Block; k < (bk + 1) * Block; ++k)
            for (int j = bj * Block; j < (bj + 1) * Block; ++j)
                for (int i = bi * Block; i < (bi + 1) * Block; ++i) {
                    float corner[8];
                    int inside = 0;
                    for (int c = 0; c < 8; ++c) {
                        corner[c] = value(i + (c & 1), j + ((c >> 1) & 1), k + (c >> 2));
                        inside += corner[c] < 0 ? 1 : 0;
                    }
                    if (inside == 0 || inside == 8)
                        continue;

                    // The mean of the points where the surface crosses the cell's edges...
                    Vec3 sum{};
                    int crossings = 0;
                    for (const auto& [a, b] : Edges) {
                        if ((corner[a] < 0) == (corner[b] < 0))
                            continue;
                        const float t = corner[a] / (corner[a] - corner[b]);
                        const Vec3 pa{static_cast<float>(a & 1), static_cast<float>((a >> 1) & 1), static_cast<float>(a >> 2)};
                        const Vec3 pb{static_cast<float>(b & 1), static_cast<float>((b >> 1) & 1), static_cast<float>(b >> 2)};
                        sum += Lerp(pa, pb, t);
                        ++crossings;
                    }
                    Vec3 position = point(i, j, k) + sum * (step / static_cast<float>(crossings));

                    // ...moved onto the surface.
                    for (int iteration = 0; iteration < 3; ++iteration) {
                        const Vec3 gradient = Gradient(sdf, position);
                        const float squared = Dot(gradient, gradient);
                        if (squared < 1e-12f)
                            break;
                        position -= gradient * (sdf(position) / squared);
                    }
                    const Vec3 normal = Normalize(Gradient(sdf, position));
                    cellVertex[cellIndex(i, j, k)] = static_cast<std::int32_t>(mesh.vertices.size());
                    mesh.vertices.push_back({position, normal, Occlusion(sdf, position, normal)});
                    activeCells.push_back({i, j, k});
                }
    }

    // A quad for each crossed edge leaving a cell's lowest corner, joining the four cells around it.
    for (auto [i, j, k] : activeCells) {
        const int base[3] = {i, j, k};
        for (int axis = 0; axis < 3; ++axis) {
            const int u = (axis + 1) % 3, v = (axis + 2) % 3;
            if (base[u] == 0 || base[v] == 0)
                continue;
            int next[3] = {i, j, k};
            ++next[axis];
            if (next[axis] > (axis == 0 ? cx : axis == 1 ? cy : cz))
                continue;
            const float v0 = value(i, j, k);
            const float v1 = value(next[0], next[1], next[2]);
            if ((v0 < 0) == (v1 < 0))
                continue;

            auto cellAt = [&](int du, int dv) {
                int c[3] = {i, j, k};
                c[u] -= du;
                c[v] -= dv;
                return cellVertex[cellIndex(c[0], c[1], c[2])];
            };
            std::array<std::int32_t, 4> quad{cellAt(0, 0), cellAt(1, 0), cellAt(1, 1), cellAt(0, 1)};
            if (std::ranges::any_of(quad, [](std::int32_t index) { return index < 0; }))
                continue;
            if (v0 >= 0)
                std::swap(quad[1], quad[3]); // the surface faces the other way

            const auto& vertices = mesh.vertices;
            const float diagonal02 = Length(vertices[quad[0]].position - vertices[quad[2]].position);
            const float diagonal13 = Length(vertices[quad[1]].position - vertices[quad[3]].position);
            const auto add = [&](int a, int b, int c) {
                mesh.indices.push_back(static_cast<std::uint32_t>(quad[a]));
                mesh.indices.push_back(static_cast<std::uint32_t>(quad[b]));
                mesh.indices.push_back(static_cast<std::uint32_t>(quad[c]));
            };
            if (diagonal02 <= diagonal13) {
                add(0, 1, 2);
                add(0, 2, 3);
            }
            else {
                add(1, 2, 3);
                add(1, 3, 0);
            }
        }
    }
    return mesh;
}

} // namespace

float PieceDistance(chess::PieceType type, Vec3 p)
{
    switch (type) {
    case chess::PieceType::Pawn: return Pawn(p);
    case chess::PieceType::Knight: return Knight(p);
    case chess::PieceType::Bishop: return Bishop(p);
    case chess::PieceType::Rook: return Rook(p);
    case chess::PieceType::Queen: return Queen(p);
    case chess::PieceType::King: return King(p);
    default: return 1;
    }
}

std::array<PieceShape, 6> BuildPieceShapes(float detail)
{
    std::array<PieceShape, 6> shapes;
    {
        std::vector<std::jthread> workers;
        for (int index = 0; index < 6; ++index) {
            workers.emplace_back([&shapes, index, detail] {
                const auto type = static_cast<chess::PieceType>(index + 1);
                const auto sdf = [type](Vec3 p) { return PieceDistance(type, p); };
                const Bounds bounds = PieceBounds(type);
                PieceShape& shape = shapes[index];
                shape.detailed = Polygonize(sdf, bounds, 0.0072f / detail);
                shape.simple = Polygonize(sdf, bounds, 0.014f / detail);
                for (const Vertex& vertex : shape.detailed.vertices) {
                    shape.height = std::max(shape.height, vertex.position.y);
                    shape.radius = std::max(shape.radius, std::sqrt(vertex.position.x * vertex.position.x + vertex.position.z * vertex.position.z));
                }
            });
        }
    }
    return shapes;
}

} // namespace pieces
