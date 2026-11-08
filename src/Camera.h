#pragma once

#include "Chess.h"
#include "MathTypes.h"

// A camera circling the board. Its moves are eased: dragging, zooming and turning set where it heads.
class OrbitCamera {
public:
    void FaceSide(chess::Color side);
    void Flip();
    void ToggleTopView();
    void Reset(chess::Color side);
    void Rotate(float yawRadians, float pitchRadians);
    void Zoom(float steps);
    // The share of the window's width left for the board beside the panel: the view centers on it, and
    // widens when it is narrow, so the board and the pieces taken stay in sight.
    void SetSpace(float share)
    {
        m_targetShift = (share - 1) / 2;
        m_targetSpace = share;
    }

    void Update(float seconds);
    bool Moving() const;

    math::Vec3 Eye() const;
    math::Mat4 View() const;
    math::Mat4 Projection(float aspect) const;
    // The ray through a point of the window, in pixels from the top left.
    void Ray(float x, float y, float width, float height, math::Vec3& origin, math::Vec3& direction) const;
    // Which side the camera looks from, for orienting the board.
    chess::Color Side() const;

private:
    static constexpr float DefaultPitch = 0.84f;
    static constexpr float DefaultDistance = 15.5f;
    static constexpr float FieldOfView = 0.6f;
    static constexpr float MinAspect = 1.4f; // of the space for the board, below which the view widens

    float m_yaw = 0, m_pitch = DefaultPitch, m_distance = DefaultDistance, m_shift = 0, m_space = 1;
    float m_targetYaw = 0, m_targetPitch = DefaultPitch, m_targetDistance = DefaultDistance, m_targetShift = 0, m_targetSpace = 1;
    bool m_topView = false;
    float m_savedPitch = DefaultPitch;
};
