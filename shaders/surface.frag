// The board's squares with their highlights, the frame with the coordinates, and the table.

in vec3 vWorld;
in vec3 vNormal;
in vec2 vUv;

uniform int uKind; // 0 squares, 1 frame, 2 table

uniform vec3 uLightSquare;
uniform vec3 uDarkSquare;
uniform vec4 uBoardSurface;    // roughness, clearcoat, clearcoat roughness, pattern (0 wood, 1 marble, 2 glass)
uniform vec4 uBoardReflection; // x: reflection strength, y: reflection blur, z: vein color brightness, w: edge glow
uniform vec3 uFrameColor;
uniform vec4 uFrameSurface;    // roughness, metallic, clearcoat, pattern
uniform vec3 uInlayColor;
uniform vec4 uInlay;           // x: metallic, y: roughness, z: glow
uniform vec3 uTableColor;
uniform vec4 uTableSurface;    // roughness, pattern (0 felt, 1 wood, 2 neon grid, 3 stone), sheen, grid glow

uniform uvec2 uSelected;
uniform uvec2 uQuiet;
uniform uvec2 uCapture;
uniform uvec2 uLastMove;
uniform uvec2 uCheck;
uniform uvec2 uHover;
uniform vec4 uDiscs[48]; // the feet of the pieces: xz, radius, darkness
uniform int uDiscCount;

layout(binding = 5) uniform sampler2D uReflection;
layout(binding = 6) uniform sampler2D uLabels;

layout(location = 0) out vec4 fragColor;

const float FrameOuter = 4.62;

bool Bit(uvec2 mask, int square)
{
    uint word = square < 32 ? mask.x : mask.y;
    return ((word >> uint(square & 31)) & 1u) != 0u;
}

// Wood with its grain along x: growth lines of uneven spacing and strength, fine fibres and a soft figure.
vec3 Wood(vec2 p, vec3 color, float seed)
{
    p += vec2(seed * 31.7, seed * 17.3);
    // Detail finer than a pixel fades to its average instead of flickering.
    float footprint = length(fwidth(p));
    // The growth lines: their spacing drifts across the grain, and they wander a little along it.
    float t = p.y * 7.0 + 2.5 * Fbm(vec2(p.y * 0.8, seed * 5.0)) + 0.8 * Fbm(vec2(p.x * 0.3, p.y * 1.2)) + 0.2 * sin(p.x * 1.1 + seed * 6.0);
    float ring = floor(t);
    float phase = t - ring;
    float width = 0.2 + 0.3 * Hash12(vec2(ring, seed));
    float late = smoothstep(0.0, 0.08, phase) * (1.0 - smoothstep(width, width + 0.25, phase));
    late *= 0.35 + 0.9 * Hash12(vec2(ring, seed + 7.0));
    late = mix(late, 0.3, smoothstep(0.04, 0.12, footprint));
    float fibers = 0.5 * Noise(vec2(p.x * 3.0, p.y * 260.0)) + 0.5 * Noise(vec2(p.x * 7.0, p.y * 600.0));
    fibers = mix(fibers, 0.5, smoothstep(0.001, 0.004, footprint));
    float figure = Noise(p * vec2(0.8, 2.2) + seed * 9.0);
    return color * (1.0 - 0.2 * late) * (0.9 + 0.16 * fibers) * (0.9 + 0.2 * figure);
}

vec3 Marble(vec2 p, vec3 color, vec3 veinColor, float seed)
{
    p += seed * 10.0;
    float warp = Fbm(p * 1.3) * 4.5;
    float wave = abs(sin((p.x * 1.4 + p.y * 0.9 + warp) * 2.6));
    float vein = pow(1.0 - wave, 14.0) + 0.35 * pow(1.0 - abs(sin((p.x * 3.1 - p.y * 2.2 + warp * 1.7) * 3.0)), 30.0);
    vec3 c = color * (0.9 + 0.2 * Fbm(p * 2.7 + 5.0));
    return mix(c, veinColor, Saturate(vein) * 0.75);
}

float ContactShadow(vec2 xz)
{
    float shade = 1.0;
    for (int i = 0; i < uDiscCount; ++i) {
        vec4 disc = uDiscs[i];
        float d = length(xz - disc.xy);
        shade *= 1.0 - disc.w * (1.0 - smoothstep(disc.z * 0.55, disc.z * 1.9, d));
    }
    return shade;
}

void main()
{
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uEye.xyz - vWorld);
    float time = uCameraPosition.w;
    Material m = DefaultMaterial(vec3(0.5), 0.5);
    vec3 glow = vec3(0.0);
    vec4 planar = vec4(0.0);

    if (uKind == 0) {
        vec2 board = vec2(vWorld.x + 4.0, 4.0 - vWorld.z); // files and ranks
        ivec2 cell = ivec2(clamp(floor(board), 0.0, 7.0));
        int square = cell.y * 8 + cell.x;
        vec2 local = fract(board) - 0.5;
        bool light = ((cell.x + cell.y) & 1) == 1;
        float seed = Hash12(vec2(float(square), 3.7));
        int pattern = int(uBoardSurface.w + 0.5);

        vec3 base = light ? uLightSquare : uDarkSquare;
        if (pattern == 0) {
            // Each square cut from its own board, the grain turning from square to square.
            vec2 grain = (cell.x + cell.y) % 2 == 0 ? local : vec2(local.y, -local.x);
            m.albedo = Wood(grain * vec2(1.0, 1.0) + vec2(0.0, seed * 3.0), base, seed);
        }
        else if (pattern == 1) {
            vec3 vein = light ? base * 0.45 : mix(base, vec3(0.85), uBoardReflection.z);
            m.albedo = Marble(vWorld.xz * 0.7 + seed * 0.3, base, vein, light ? 1.0 : 4.0);
        }
        else {
            m.albedo = base * (0.9 + 0.2 * Noise(vWorld.xz * 6.0));
        }
        m.roughness = uBoardSurface.x;
        m.clearcoat = uBoardSurface.y;
        m.clearcoatRoughness = uBoardSurface.z;

        // The joints between the squares, and their eased edges.
        float edge = 0.5 - max(abs(local.x), abs(local.y));
        float joint = 1.0 - smoothstep(0.0, 0.007, edge);
        m.albedo *= 1.0 - 0.45 * joint * (pattern == 2 ? 0.0 : 1.0);
        float bevel = 1.0 - smoothstep(0.0, 0.016, edge);
        vec2 outward = abs(local.x) > abs(local.y) ? vec2(sign(local.x), 0.0) : vec2(0.0, -sign(local.y));
        N = normalize(N + vec3(outward.x, 0.0, outward.y) * bevel * 0.35);
        if (pattern == 2)
            glow += uAccent.rgb * uBoardReflection.w * joint * 1.4;

        // The highlights.
        float pulse = 0.5 + 0.5 * sin(time * 3.5);
        if (Bit(uLastMove, square))
            m.albedo = mix(m.albedo, uAccentWarm.rgb * 0.75, 0.4);
        if (Bit(uHover, square)) {
            m.albedo *= 1.12;
            glow += uAccent.rgb * 0.5 * smoothstep(0.38, 0.5, max(abs(local.x), abs(local.y)));
        }
        if (Bit(uSelected, square)) {
            float frame = smoothstep(0.3, 0.5, max(abs(local.x), abs(local.y)));
            glow += uAccent.rgb * (0.18 + 1.2 * frame) * (0.8 + 0.2 * pulse);
        }
        if (Bit(uQuiet, square)) {
            float d = length(local);
            glow += uAccent.rgb * ((1.0 - smoothstep(0.085, 0.105, d)) * 1.8 + 0.12 * (1.0 - smoothstep(0.0, 0.35, d))) * (0.85 + 0.15 * pulse);
        }
        if (Bit(uCapture, square)) {
            float d = length(local);
            float ring = 1.0 - smoothstep(0.012, 0.03, abs(d - 0.43));
            float corners = smoothstep(0.62, 0.68, length(abs(local) + 0.04));
            glow += uAccent.rgb * (ring * 1.9 + corners * 0.9) * (0.85 + 0.15 * pulse);
        }
        if (Bit(uCheck, square)) {
            float d = length(local);
            glow += vec3(1.0, 0.1, 0.03) * (1.0 - smoothstep(0.0, 0.65, d)) * (0.9 + 0.7 * pulse);
        }

        // The pieces mirrored in the polish, added over the shading below.
        vec2 screen = gl_FragCoord.xy * uViewport.zw + N.xz * 0.012;
        planar = textureLod(uReflection, screen, uBoardReflection.y);
        m.occlusion = ContactShadow(vWorld.xz);
    }
    else if (uKind == 1) {
        int pattern = int(uFrameSurface.w + 0.5);
        bool alongX = abs(vWorld.z) > abs(vWorld.x);
        vec2 grain = alongX ? vec2(vWorld.x, vWorld.z + vWorld.y) : vec2(vWorld.z, vWorld.x + vWorld.y);
        if (pattern == 0)
            m.albedo = Wood(grain * vec2(0.35, 1.2), uFrameColor, 0.37);
        else if (pattern == 1)
            m.albedo = Marble(vWorld.xz * 0.5 + vWorld.y, uFrameColor, uFrameColor * 3.0 + 0.08, 2.0);
        else
            m.albedo = uFrameColor * (0.94 + 0.12 * Noise(grain * vec2(2.0, 300.0)));
        m.roughness = uFrameSurface.x;
        m.metallic = uFrameSurface.y;
        m.clearcoat = uFrameSurface.z;
        m.clearcoatRoughness = 0.06;

        // The inlaid coordinates and lines, on the top.
        float label = texture(uLabels, (vWorld.xz + FrameOuter) / (2.0 * FrameOuter)).r;
        if (N.y > 0.9) {
            float ring = max(abs(vWorld.x), abs(vWorld.z));
            float lines = (1.0 - smoothstep(0.006, 0.011, abs(ring - 4.1))) + (1.0 - smoothstep(0.004, 0.008, abs(ring - 4.52)));
            float inlay = Saturate(max(label, lines));
            m.albedo = mix(m.albedo, uInlayColor, inlay);
            m.metallic = mix(m.metallic, uInlay.x, inlay);
            m.roughness = mix(m.roughness, uInlay.y, inlay);
            glow += uInlayColor * inlay * uInlay.z;
        }
        m.occlusion = ContactShadow(vWorld.xz) * mix(0.65, 1.0, smoothstep(-0.32, -0.12, vWorld.y));
    }
    else {
        int pattern = int(uTableSurface.y + 0.5);
        vec2 p = vWorld.xz;
        float distance = length(p);
        if (pattern == 0) {
            // Felt.
            float fibers = 0.6 * Noise(p * 260.0) + 0.4 * Noise(p * 90.0 + 3.0);
            m.albedo = uTableColor * (0.8 + 0.35 * fibers) * (0.9 + 0.2 * Fbm(p * 0.6));
        }
        else if (pattern == 1) {
            m.albedo = Wood(p * vec2(0.25, 0.9), uTableColor, 0.81);
        }
        else if (pattern == 2) {
            // The glowing grid of the first version of the game.
            m.albedo = uTableColor;
            vec2 grid = abs(fract(p - 0.5) - 0.5) / fwidth(p);
            float minor = 1.0 - Saturate(min(grid.x, grid.y) - 0.5);
            vec2 major2 = abs(fract(p * 0.2 - 0.5) - 0.5) / fwidth(p * 0.2);
            float major = 1.0 - Saturate(min(major2.x, major2.y) - 0.8);
            float fade = exp(-distance * 0.16);
            glow += uAccent.rgb * (minor * 0.35 + major * 1.1) * fade * uTableSurface.w;
        }
        else {
            m.albedo = Marble(p * 0.22, uTableColor, uTableColor * 1.7, 6.0);
        }
        m.roughness = uTableSurface.x;
        // The light falls on the board; the table darkens away from it.
        m.albedo *= mix(1.0, 0.18, smoothstep(5.5, 15.0, distance));
        m.occlusion = ContactShadow(p);
        // Darker under and around the board.
        float board = max(abs(p.x), abs(p.y));
        m.occlusion *= mix(0.35, 1.0, smoothstep(FrameOuter - 0.05, FrameOuter + 1.1, board));
        if (uTableSurface.z > 0.0) {
            // The sheen of cloth at grazing angles.
            m.emissive += uTableColor * uTableSurface.z * pow(1.0 - Saturate(dot(N, V)), 4.0) * 0.5;
        }
    }

    float shadow = Shadow(vWorld, N);
    vec3 color = Shade(m, vWorld, N, V, shadow) + glow;
    if (uKind == 0) {
        float fresnel = mix(0.1, 1.0, pow(1.0 - Saturate(dot(N, V)), 5.0));
        color = mix(color, planar.rgb, planar.a * fresnel * uBoardReflection.x);
    }
    fragColor = vec4(ApplyFog(color, vWorld), 1.0);
}
