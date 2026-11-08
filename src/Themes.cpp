#include "Themes.h"

using math::Vec2;
using math::Vec3;

namespace {

Theme Walnut()
{
    Theme t{
        .nameEnglish = "Walnut",
        .nameRussian = "Орех",
        .lightSquare = {0.62f, 0.43f, 0.24f},
        .darkSquare = {0.17f, 0.085f, 0.04f},
        .boardRoughness = 0.32f,
        .boardClearcoat = 0.8f,
        .boardClearcoatRoughness = 0.07f,
        .boardPattern = 0,
        .reflection = 0.85f,
        .reflectionBlur = 1.6f,
        .frameColor = {0.09f, 0.045f, 0.022f},
        .frameRoughness = 0.38f,
        .frameClearcoat = 0.9f,
        .framePattern = 0,
        .inlayColor = {0.95f, 0.7f, 0.36f},
        .inlayMetallic = 1,
        .inlayRoughness = 0.28f,
        .white = {.albedo = {0.9f, 0.75f, 0.52f}, .roughness = 0.34f, .clearcoat = 0.85f, .clearcoatRoughness = 0.06f, .subsurface = 0.25f,
                  .pattern = 1, .patternStrength = 0.45f},
        .black = {.albedo = {0.028f, 0.018f, 0.014f}, .roughness = 0.3f, .clearcoat = 1.0f, .clearcoatRoughness = 0.045f, .pattern = 1,
                  .patternStrength = 0.6f},
        .tableColor = {0.035f, 0.075f, 0.055f},
        .tableRoughness = 0.95f,
        .tablePattern = 0,
        .tableSheen = 0.35f,
        .keyDirection = {-0.58f, 0.7f, 0.42f},
        .keyRadiance = {4.6f, 4.1f, 3.5f},
        .keySoftness = 0.045f,
        .ceiling = {0.035f, 0.033f, 0.034f},
        .horizon = {0.11f, 0.085f, 0.065f},
        .floor = {0.02f, 0.016f, 0.012f},
        .diffuseLight = 0.62f,
        .reflectedLight = 1.0f,
        .background = 0.55f,
        .fog = {0.035f, 0.027f, 0.022f},
        .exposure = 1.1f,
        .bloom = 0.045f,
        .vignette = 0.42f,
        .accent = {0.35f, 0.75f, 1.0f},
        .accentWarm = {1.0f, 0.78f, 0.32f},
    };
    t.panels[0] = {.direction = {-0.58f, 0.7f, 0.42f}, .halfSize = {0.42f, 0.3f}, .corner = 0.08f, .softness = 0.06f, .radiance = {7.5f, 6.6f, 5.6f}};
    t.panels[1] = {.direction = {0.85f, 0.3f, 0.3f}, .halfSize = {0.55f, 0.35f}, .corner = 0.1f, .softness = 0.15f, .radiance = {0.7f, 0.78f, 0.9f}};
    t.panels[2] = {.direction = {0.15f, 0.42f, -0.9f}, .halfSize = {1.3f, 0.07f}, .corner = 0.03f, .softness = 0.03f, .radiance = {5.5f, 5.0f, 4.4f}};
    t.panels[3] = {.direction = {0.0f, 1.0f, 0.05f}, .halfSize = {0.7f, 0.7f}, .corner = 0.3f, .softness = 0.3f, .radiance = {0.6f, 0.58f, 0.55f}};
    t.panels[4] = {.direction = {-0.9f, 0.25f, -0.4f}, .halfSize = {0.06f, 0.6f}, .corner = 0.03f, .softness = 0.03f, .radiance = {2.6f, 2.2f, 1.8f}};
    t.panelCount = 5;
    return t;
}

Theme Marble()
{
    Theme t{
        .nameEnglish = "Marble",
        .nameRussian = "Мрамор",
        .lightSquare = {0.78f, 0.77f, 0.74f},
        .darkSquare = {0.035f, 0.06f, 0.05f},
        .boardRoughness = 0.12f,
        .boardClearcoat = 0.3f,
        .boardClearcoatRoughness = 0.03f,
        .boardPattern = 1,
        .reflection = 1.0f,
        .reflectionBlur = 0.6f,
        .veinBrightness = 0.35f,
        .frameColor = {0.02f, 0.02f, 0.022f},
        .frameRoughness = 0.14f,
        .frameClearcoat = 0.5f,
        .framePattern = 1,
        .inlayColor = {0.98f, 0.8f, 0.45f},
        .inlayMetallic = 1,
        .inlayRoughness = 0.2f,
        .white = {.albedo = {0.8f, 0.79f, 0.76f}, .roughness = 0.18f, .clearcoat = 0.8f, .clearcoatRoughness = 0.04f, .subsurface = 0.35f,
                  .pattern = 3, .patternStrength = 0.25f},
        .black = {.albedo = {0.012f, 0.012f, 0.016f}, .roughness = 0.08f, .clearcoat = 1.0f, .clearcoatRoughness = 0.02f, .pattern = 0},
        .tableColor = {0.022f, 0.022f, 0.026f},
        .tableRoughness = 0.3f,
        .tablePattern = 3,
        .keyDirection = {-0.42f, 0.85f, 0.32f},
        .keyRadiance = {3.0f, 3.05f, 3.2f},
        .keySoftness = 0.05f,
        .ceiling = {0.05f, 0.055f, 0.065f},
        .horizon = {0.09f, 0.095f, 0.11f},
        .floor = {0.018f, 0.018f, 0.02f},
        .diffuseLight = 1.0f,
        .reflectedLight = 1.0f,
        .background = 0.5f,
        .fog = {0.03f, 0.032f, 0.038f},
        .exposure = 1.0f,
        .bloom = 0.04f,
        .vignette = 0.4f,
        .accent = {0.4f, 0.85f, 0.75f},
        .accentWarm = {0.95f, 0.75f, 0.35f},
    };
    t.panels[0] = {.direction = {-0.42f, 0.85f, 0.32f}, .halfSize = {0.5f, 0.5f}, .corner = 0.25f, .softness = 0.12f, .radiance = {6.0f, 6.1f, 6.4f}};
    t.panels[1] = {.direction = {0.9f, 0.2f, 0.25f}, .halfSize = {0.5f, 0.3f}, .corner = 0.1f, .softness = 0.12f, .radiance = {0.8f, 0.85f, 1.0f}};
    t.panels[2] = {.direction = {-0.1f, 0.35f, -0.92f}, .halfSize = {1.4f, 0.05f}, .corner = 0.02f, .softness = 0.02f, .radiance = {6.0f, 6.2f, 6.6f}};
    t.panels[3] = {.direction = {0.0f, 1.0f, 0.0f}, .halfSize = {0.8f, 0.8f}, .corner = 0.4f, .softness = 0.35f, .radiance = {0.9f, 0.92f, 1.0f}};
    t.panels[4] = {.direction = {0.6f, 0.3f, -0.75f}, .halfSize = {0.05f, 0.7f}, .corner = 0.02f, .softness = 0.02f, .radiance = {2.5f, 2.6f, 3.0f}};
    t.panelCount = 5;
    return t;
}

Theme Neon()
{
    Theme t{
        .nameEnglish = "Neon",
        .nameRussian = "Неон",
        .lightSquare = {0.11f, 0.17f, 0.24f},
        .darkSquare = {0.006f, 0.008f, 0.014f},
        .boardRoughness = 0.08f,
        .boardClearcoat = 0.6f,
        .boardClearcoatRoughness = 0.02f,
        .boardPattern = 2,
        .reflection = 1.0f,
        .reflectionBlur = 0.4f,
        .edgeGlow = 0.6f,
        .frameColor = {0.09f, 0.1f, 0.12f},
        .frameRoughness = 0.3f,
        .frameMetallic = 1,
        .framePattern = 2,
        .inlayColor = {0.2f, 0.75f, 1.0f},
        .inlayMetallic = 0,
        .inlayRoughness = 0.3f,
        .inlayGlow = 3.0f,
        .white = {.albedo = {0.82f, 0.86f, 0.9f}, .roughness = 0.22f, .clearcoat = 1.0f, .clearcoatRoughness = 0.03f, .subsurface = 0.5f,
                  .trim = {0.2f, 0.8f, 1.0f}, .trimStrength = 5.0f},
        .black = {.albedo = {0.36f, 0.36f, 0.42f}, .roughness = 0.26f, .metallic = 1, .pattern = 2, .patternStrength = 1.0f,
                  .trim = {1.0f, 0.3f, 0.75f}, .trimStrength = 5.0f},
        .tableColor = {0.004f, 0.006f, 0.01f},
        .tableRoughness = 0.5f,
        .tablePattern = 2,
        .gridGlow = 0.55f,
        .keyDirection = {-0.35f, 0.8f, 0.48f},
        .keyRadiance = {1.8f, 2.1f, 2.6f},
        .keySoftness = 0.03f,
        .ceiling = {0.01f, 0.012f, 0.025f},
        .horizon = {0.02f, 0.035f, 0.07f},
        .floor = {0.004f, 0.005f, 0.008f},
        .diffuseLight = 1.0f,
        .reflectedLight = 1.2f,
        .background = 0.7f,
        .fog = {0.006f, 0.01f, 0.02f},
        .exposure = 1.1f,
        .bloom = 0.07f,
        .vignette = 0.5f,
        .accent = {0.2f, 0.8f, 1.0f},
        .accentWarm = {1.0f, 0.35f, 0.75f},
    };
    t.panels[0] = {.direction = {-0.35f, 0.8f, 0.48f}, .halfSize = {0.35f, 0.25f}, .corner = 0.05f, .softness = 0.05f, .radiance = {3.5f, 4.2f, 5.0f}};
    t.panels[1] = {.direction = {0.95f, 0.12f, 0.2f}, .halfSize = {1.0f, 0.03f}, .corner = 0.02f, .softness = 0.02f, .radiance = {8.0f, 1.5f, 5.0f}};
    t.panels[2] = {.direction = {-0.95f, 0.12f, -0.2f}, .halfSize = {1.0f, 0.03f}, .corner = 0.02f, .softness = 0.02f, .radiance = {1.0f, 5.0f, 8.0f}};
    t.panels[3] = {.direction = {0.1f, 0.3f, -0.95f}, .halfSize = {1.4f, 0.025f}, .corner = 0.01f, .softness = 0.015f, .radiance = {8.0f, 4.0f, 1.0f}};
    t.panels[4] = {.direction = {0.0f, 1.0f, 0.0f}, .halfSize = {0.6f, 0.6f}, .corner = 0.3f, .softness = 0.3f, .radiance = {0.25f, 0.3f, 0.45f}};
    t.panels[5] = {.direction = {0.3f, 0.25f, 0.95f}, .halfSize = {0.9f, 0.025f}, .corner = 0.01f, .softness = 0.015f, .radiance = {1.0f, 6.0f, 7.0f}};
    t.panelCount = 6;
    return t;
}

} // namespace

const std::array<Theme, 3>& Themes()
{
    static const std::array<Theme, 3> themes{Walnut(), Marble(), Neon()};
    return themes;
}
