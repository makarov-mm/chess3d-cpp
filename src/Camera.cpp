#include "Camera.h"

#include <cmath>

using namespace math;

namespace {

constexpr float MinPitch = 0.22f, MaxPitch = 1.53f;
constexpr float MinDistance = 8.0f, MaxDistance = 26.0f;
const Vec3 Target{0, 0.15f, 0};

// The angle nearest to `current` that points the same way as `angle`.
float Unwrap(float angle, float current)
{
    while (angle - current > Pi)
        angle -= 2 * Pi;
    while (angle - current < -Pi)
        angle += 2 * Pi;
    return angle;
}

} // namespace

void OrbitCamera::FaceSide(chess::Color side)
{
    m_targetYaw = Unwrap(side == chess::Color::White ? 0.0f : Pi, m_targetYaw);
}

void OrbitCamera::Flip()
{
    m_targetYaw += Pi;
}

void OrbitCamera::ToggleTopView()
{
    m_topView = !m_topView;
    if (m_topView) {
        m_savedPitch = m_targetPitch;
        m_targetPitch = MaxPitch;
    }
    else {
        m_targetPitch = m_savedPitch;
    }
}

void OrbitCamera::Reset(chess::Color side)
{
    FaceSide(side);
    m_targetPitch = DefaultPitch;
    m_targetDistance = DefaultDistance;
    m_topView = false;
}

void OrbitCamera::Rotate(float yawRadians, float pitchRadians)
{
    m_targetYaw += yawRadians;
    m_targetPitch = std::clamp(m_targetPitch + pitchRadians, MinPitch, MaxPitch);
    m_topView = false;
}

void OrbitCamera::Zoom(float steps)
{
    m_targetDistance = std::clamp(m_targetDistance * std::pow(0.9f, steps), MinDistance, MaxDistance);
}

void OrbitCamera::Update(float seconds)
{
    const float ease = 1 - std::exp(-seconds * 9.0f);
    m_yaw += (m_targetYaw - m_yaw) * ease;
    m_pitch += (m_targetPitch - m_pitch) * ease;
    m_distance += (m_targetDistance - m_distance) * ease;
    m_shift += (m_targetShift - m_shift) * ease;
    m_space += (m_targetSpace - m_space) * ease;
}

bool OrbitCamera::Moving() const
{
    return std::abs(m_targetYaw - m_yaw) > 1e-4f || std::abs(m_targetPitch - m_pitch) > 1e-4f || std::abs(m_targetDistance - m_distance) > 1e-3f ||
           std::abs(m_targetShift - m_shift) > 1e-4f || std::abs(m_targetSpace - m_space) > 1e-4f;
}

Vec3 OrbitCamera::Eye() const
{
    const float horizontal = std::cos(m_pitch);
    return Target + Vec3{std::sin(m_yaw) * horizontal, std::sin(m_pitch), std::cos(m_yaw) * horizontal} * m_distance;
}

Mat4 OrbitCamera::View() const
{
    // Looking straight down, "up" on the screen points away from the player.
    const Vec3 up = Normalize(Vec3{-std::sin(m_yaw) * std::sin(m_pitch), std::cos(m_pitch), -std::cos(m_yaw) * std::sin(m_pitch)});
    return Mat4::LookAt(Eye(), Target, up);
}

Mat4 OrbitCamera::Projection(float aspect) const
{
    const float widen = std::max(1.0f, MinAspect / (aspect * m_space));
    Mat4 projection = Mat4::Perspective(2 * std::atan(std::tan(FieldOfView / 2) * widen), aspect, 0.2f, 120.0f);
    projection(0, 2) = -2 * m_shift; // shift in normalized coordinates, which span 2
    return projection;
}

void OrbitCamera::Ray(float x, float y, float width, float height, Vec3& origin, Vec3& direction) const
{
    const Mat4 inverse = (Projection(width / std::max(height, 1.0f)) * View()).Inverse();
    const float nx = x / width * 2 - 1, ny = 1 - y / height * 2;
    const Vec3 nearPoint = inverse.TransformPoint({nx, ny, -1});
    const Vec3 farPoint = inverse.TransformPoint({nx, ny, 1});
    origin = nearPoint;
    direction = Normalize(farPoint - nearPoint);
}

chess::Color OrbitCamera::Side() const
{
    return std::cos(m_yaw) >= 0 ? chess::Color::White : chess::Color::Black;
}
