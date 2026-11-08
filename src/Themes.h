#pragma once

#include "MathTypes.h"

#include <array>
#include <string_view>

// The looks of the set: the board, the pieces, the table and the studio light around them.
struct PieceLook {
    math::Vec3 albedo;
    float roughness = 0.3f;
    float metallic = 0;
    float clearcoat = 0;
    float clearcoatRoughness = 0.05f;
    float subsurface = 0;
    float pattern = 0;  // 0 none, 1 wood, 2 turned metal, 3 stone
    float patternStrength = 0;
    math::Vec3 trim{};  // a glowing ring round the foot
    float trimStrength = 0;
};

struct StudioPanel {
    math::Vec3 direction; // toward the panel
    float roll = 0;
    math::Vec2 halfSize;  // as tangents of the angles
    float corner = 0.05f;
    float softness = 0.05f;
    math::Vec3 radiance;
    float falloff = 0.3f;
};

struct Theme {
    std::string_view nameEnglish;
    std::string_view nameRussian;

    // The board.
    math::Vec3 lightSquare;
    math::Vec3 darkSquare;
    float boardRoughness = 0.3f;
    float boardClearcoat = 0;
    float boardClearcoatRoughness = 0.05f;
    float boardPattern = 0; // 0 wood, 1 marble, 2 glass
    float reflection = 0.5f;
    float reflectionBlur = 1.5f;
    float veinBrightness = 0.5f;
    float edgeGlow = 0;
    math::Vec3 frameColor;
    float frameRoughness = 0.35f;
    float frameMetallic = 0;
    float frameClearcoat = 0;
    float framePattern = 0; // 0 wood, 1 marble, 2 metal
    math::Vec3 inlayColor;
    float inlayMetallic = 1;
    float inlayRoughness = 0.3f;
    float inlayGlow = 0;

    PieceLook white;
    PieceLook black;

    math::Vec3 tableColor;
    float tableRoughness = 0.9f;
    float tablePattern = 0; // 0 felt, 1 wood, 2 glowing grid, 3 stone
    float tableSheen = 0;
    float gridGlow = 0;

    // The light.
    math::Vec3 keyDirection;
    math::Vec3 keyRadiance;
    float keySoftness = 0.04f; // tangent of the light's angular radius
    math::Vec3 ceiling, horizon, floor;
    std::array<StudioPanel, 6> panels{};
    int panelCount = 0;
    float diffuseLight = 1;
    float reflectedLight = 1;
    float background = 0.6f;
    math::Vec3 fog;
    float exposure = 1;
    float bloom = 0.05f;
    float vignette = 0.35f;

    math::Vec3 accent;     // highlights and the interface
    math::Vec3 accentWarm; // the last move
};

const std::array<Theme, 3>& Themes();
