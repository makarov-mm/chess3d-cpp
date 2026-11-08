#include "BoardView.h"

#include "BoardGeometry.h"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace math;
using chess::Color;
using chess::PieceType;

namespace {

float Ease(float t)
{
    return t * t * t * (t * (t * 6 - 15) + 10);
}

int Index(PieceType type) { return std::to_underlying(type) - 1; }

} // namespace

BoardView::Piece* BoardView::At(int square)
{
    for (Piece& piece : m_pieces)
        if (piece.square == square)
            return &piece;
    return nullptr;
}

// Pieces taken lie in rows beside the board, on the right of the player who took them.
Vec3 BoardView::TableSlot(Color color, int slot) const
{
    const float column = static_cast<float>(slot / 8), row = static_cast<float>(slot % 8);
    if (color == Color::Black)
        return {5.35f + column * 0.68f, board::TableHeight, 3.55f - row * 0.62f};
    return {-5.35f - column * 0.68f, board::TableHeight, -3.55f + row * 0.62f};
}

float BoardView::GroundAt(Vec3 position)
{
    return std::abs(position.x) < board::FrameOuter && std::abs(position.z) < board::FrameOuter ? 0.0f : board::TableHeight;
}

// The pieces face the other side; the knights look sideways toward the middle, showing their profiles.
float BoardView::FacingAt(chess::Piece piece, int square)
{
    if (piece.type == PieceType::Knight)
        return chess::FileOf(square) < 4 ? Pi / 2 : -Pi / 2;
    return piece.color == Color::White ? Pi : 0.0f;
}

void BoardView::MoveTo(Piece& piece, Vec3 target, float duration, float arc, float delay)
{
    piece.yawStart = piece.yawEnd = piece.yaw;
    const Vec3 travel = target - piece.position;
    if (piece.type == PieceType::Knight && travel.x * travel.x + travel.z * travel.z > 0.25f) {
        // A knight turns to look where it jumps.
        const float heading = std::atan2(travel.x, travel.z);
        piece.yawEnd = piece.yaw + std::remainder(heading - piece.yaw, 2 * Pi);
    }
    piece.start = piece.position;
    piece.end = target;
    piece.arc = arc;
    piece.progress = 0;
    piece.duration = duration;
    piece.delay = delay;
    piece.held = false;
}

void BoardView::Fade(Piece& piece, float to, float duration, float delay)
{
    piece.dissolveFrom = piece.dissolve;
    piece.dissolveTo = to;
    piece.dissolveProgress = 0;
    piece.dissolveDuration = duration;
    piece.dissolveDelay = delay;
}

void BoardView::Place(chess::Piece piece, int square, bool appear, float delay)
{
    Piece placed;
    placed.type = piece.type;
    placed.color = piece.color;
    placed.square = square;
    placed.position = placed.start = placed.end = board::SquareCenter(square);
    placed.yaw = placed.yawStart = placed.yawEnd = FacingAt(piece, square);
    if (appear) {
        placed.dissolve = 1;
        Fade(placed, 0, 0.45f, delay);
    }
    m_pieces.push_back(placed);
}

void BoardView::PutOnTable(chess::Piece piece, bool appear, float delay)
{
    Piece placed;
    placed.type = piece.type;
    placed.color = piece.color;
    placed.slot = m_tableCounts[std::to_underlying(piece.color)]++;
    placed.yaw = placed.yawStart = placed.yawEnd = piece.color == Color::White ? Pi : 0.0f;
    placed.position = placed.start = placed.end = TableSlot(piece.color, placed.slot);
    if (appear) {
        placed.dissolve = 1;
        Fade(placed, 0, 0.45f, delay);
    }
    m_pieces.push_back(placed);
}

void BoardView::Rebuild(const chess::Game& game, bool animate)
{
    if (animate) {
        for (Piece& piece : m_pieces) {
            Fade(piece, 1, 0.35f);
            piece.removeWhenGone = true;
            piece.square = -2;
        }
    }
    else {
        m_pieces.clear();
    }
    m_tableCounts = {};

    const chess::Position& position = game.Current();
    for (int square = 0; square < 64; ++square)
        if (!position.At(square).Empty())
            Place(position.At(square), square, animate, 0.3f + 0.012f * static_cast<float>(square));
    for (const chess::MoveRecord& record : game.Moves())
        if (!record.captured.Empty())
            PutOnTable(record.captured, animate, 0.5f);
}

void BoardView::PlayMove(const chess::MoveRecord& record, bool forward)
{
    const chess::Move move = record.move;
    const auto distance = [](int a, int b) { return Length(board::SquareCenter(a) - board::SquareCenter(b)); };
    const auto travel = [&](PieceType type, float squares) { return type == PieceType::Knight ? 0.46f : std::min(0.26f + 0.035f * squares, 0.5f); };
    const auto lift = [&](PieceType type, float squares) { return type == PieceType::Knight ? 0.75f : 0.08f + 0.025f * squares; };

    const int rank = chess::RankOf(move.from);
    const bool kingside = move.flag == chess::MoveFlag::CastleKingside;
    const int rookFrom = chess::MakeSquare(kingside ? 7 : 0, rank), rookTo = chess::MakeSquare(kingside ? 5 : 3, rank);
    const float moveTime = travel(record.piece.type, distance(move.from, move.to));

    if (forward) {
        if (!record.captured.Empty()) {
            if (Piece* victim = At(record.capturedSquare)) {
                victim->square = -1;
                victim->slot = m_tableCounts[std::to_underlying(victim->color)]++;
                MoveTo(*victim, TableSlot(victim->color, victim->slot), 0.7f, 1.3f, moveTime * 0.45f);
            }
        }
        if (Piece* mover = At(move.from)) {
            mover->square = move.to;
            MoveTo(*mover, board::SquareCenter(move.to), moveTime, lift(record.piece.type, distance(move.from, move.to)));
            if (move.promotion != PieceType::None) {
                Fade(*mover, 1, 0.45f, moveTime);
                mover->removeWhenGone = true;
                mover->square = -2;
                Place({move.promotion, record.piece.color}, move.to, true, moveTime + 0.1f);
            }
        }
        if (move.IsCastling()) {
            if (Piece* rook = At(rookFrom)) {
                rook->square = rookTo;
                MoveTo(*rook, board::SquareCenter(rookTo), 0.42f, 0.7f, 0.05f);
            }
        }
        return;
    }

    if (move.promotion != PieceType::None) {
        if (Piece* promoted = At(move.to)) {
            Fade(*promoted, 1, 0.35f);
            promoted->removeWhenGone = true;
            promoted->square = -2;
        }
        Place(record.piece, move.from, true, 0.15f);
    }
    else if (Piece* mover = At(move.to)) {
        mover->square = move.from;
        MoveTo(*mover, board::SquareCenter(move.from), moveTime, lift(record.piece.type, distance(move.from, move.to)));
    }
    if (move.IsCastling()) {
        if (Piece* rook = At(rookTo)) {
            rook->square = rookFrom;
            MoveTo(*rook, board::SquareCenter(rookFrom), 0.42f, 0.7f, 0.05f);
        }
    }
    if (!record.captured.Empty()) {
        const int side = std::to_underlying(record.captured.color);
        for (Piece& piece : m_pieces) {
            if (piece.square == -1 && piece.color == record.captured.color && piece.slot == m_tableCounts[side] - 1) {
                --m_tableCounts[side];
                piece.square = record.capturedSquare;
                piece.slot = -1;
                MoveTo(piece, board::SquareCenter(record.capturedSquare), 0.65f, 1.3f, 0.05f);
                break;
            }
        }
    }
}

void BoardView::Update(float seconds)
{
    for (Piece& piece : m_pieces) {
        if (piece.held)
            continue;
        if (piece.delay > 0) {
            piece.delay -= seconds;
        }
        else if (piece.progress < 1) {
            piece.progress = std::min(1.0f, piece.progress + seconds / piece.duration);
            const float t = Ease(piece.progress);
            piece.position = Lerp(piece.start, piece.end, t) + Vec3{0, piece.arc * std::sin(Pi * piece.progress), 0};
            piece.yaw = Lerp(piece.yawStart, piece.yawEnd, t);
        }
        if (piece.dissolveDelay > 0) {
            piece.dissolveDelay -= seconds;
        }
        else if (piece.dissolveProgress < 1) {
            piece.dissolveProgress = std::min(1.0f, piece.dissolveProgress + seconds / piece.dissolveDuration);
            piece.dissolve = Lerp(piece.dissolveFrom, piece.dissolveTo, piece.dissolveProgress);
        }
    }
    std::erase_if(m_pieces, [](const Piece& piece) { return piece.removeWhenGone && piece.dissolveProgress >= 1 && piece.dissolveDelay <= 0; });
}

bool BoardView::Animating() const
{
    return std::ranges::any_of(m_pieces, [](const Piece& piece) {
        return piece.progress < 1 || piece.delay > 0 || piece.dissolveProgress < 1 || piece.dissolveDelay > 0;
    });
}

void BoardView::Hold(chess::Square square, Vec3 position)
{
    if (Piece* piece = At(square)) {
        piece->held = true;
        piece->progress = 1;
        piece->position = position;
    }
}

void BoardView::Release(chess::Square square)
{
    if (Piece* piece = At(square))
        MoveTo(*piece, board::SquareCenter(square), 0.22f, 0.05f);
}

int BoardView::Pick(Vec3 origin, Vec3 direction) const
{
    int best = -1;
    float nearest = 1e9f;
    for (const Piece& piece : m_pieces) {
        if (piece.square < 0 || piece.dissolve > 0.5f)
            continue;
        const float radius = m_radii[Index(piece.type)] * 0.85f;
        const float height = m_heights[Index(piece.type)];
        const Vec3 o = origin - piece.position;
        // The upright cylinder round the piece.
        const float a = direction.x * direction.x + direction.z * direction.z;
        const float b = 2 * (o.x * direction.x + o.z * direction.z);
        const float c = o.x * o.x + o.z * o.z - radius * radius;
        float hit = 1e9f;
        if (a > 1e-8f) {
            const float discriminant = b * b - 4 * a * c;
            if (discriminant >= 0) {
                const float t = (-b - std::sqrt(discriminant)) / (2 * a);
                const float y = o.y + direction.y * t;
                if (t > 0 && y >= 0 && y <= height)
                    hit = t;
            }
        }
        // Its top.
        if (std::abs(direction.y) > 1e-6f) {
            const float t = (height - o.y) / direction.y;
            const float x = o.x + direction.x * t, z = o.z + direction.z * t;
            if (t > 0 && x * x + z * z <= radius * radius)
                hit = std::min(hit, t);
        }
        if (hit < nearest) {
            nearest = hit;
            best = piece.square;
        }
    }
    return best;
}

std::vector<render::PieceDraw> BoardView::Draws(int selected, int hovered, Vec3 accent, float time) const
{
    std::vector<render::PieceDraw> draws;
    draws.reserve(m_pieces.size());
    for (const Piece& piece : m_pieces) {
        render::PieceDraw draw;
        draw.type = piece.type;
        draw.color = piece.color;
        draw.model = Mat4::Translation(piece.position) * Mat4::RotationY(piece.yaw);
        draw.ground = GroundAt(piece.position);
        draw.dissolve = piece.dissolve;
        draw.glowColor = accent;
        if (piece.square >= 0 && piece.square == selected)
            draw.glow = 0.8f + 0.2f * std::sin(time * 4.0f);
        else if (piece.square >= 0 && piece.square == hovered)
            draw.glow = 0.35f;
        draws.push_back(draw);
    }
    return draws;
}

std::vector<render::ContactShadow> BoardView::Contacts() const
{
    std::vector<render::ContactShadow> contacts;
    for (const Piece& piece : m_pieces) {
        const float height = piece.position.y - GroundAt(piece.position);
        const float strength = 0.72f * (1 - SmoothStep(0.0f, 0.8f, height)) * (1 - piece.dissolve);
        if (strength > 0.01f)
            contacts.push_back({piece.position.x, piece.position.z, m_radii[Index(piece.type)] * (1.05f + height * 0.6f), strength});
    }
    return contacts;
}
