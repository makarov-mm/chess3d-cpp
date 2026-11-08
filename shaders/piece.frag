// A chess piece: ivory, boxwood, ebony, porcelain, stone or metal, with a selection glow and a dissolve.

in vec3 vWorld;
in vec3 vNormal;
in vec3 vLocal;
in float vOcclusion;

uniform vec3 uAlbedo;
uniform vec4 uSurface;   // roughness, metallic, clearcoat, clearcoat roughness
uniform vec4 uStyle;     // x: subsurface, y: pattern strength, z: pattern (0 none, 1 wood, 2 brushed metal, 3 stone), w: height of the ground
uniform vec4 uGlow;      // rgb, strength: the selection
uniform vec4 uTrim;      // rgb, strength: a glowing ring round the foot
uniform vec4 uDissolve;  // x: 0 solid to 1 gone, yzw: edge color

layout(location = 0) out vec4 fragColor;

void main()
{
    // Measured before anything is discarded, while the neighbouring pixels still run.
    vec3 N = normalize(vNormal);
    float spread = NormalSpread(N);
    vec3 localPixel = fwidth(vLocal);

    vec3 emissive = vec3(0.0);
    if (uDissolve.x > 0.0) {
        float n = Fbm(vLocal * 7.0 + 3.0);
        float threshold = uDissolve.x * 1.2 - 0.1;
        if (n < threshold)
            discard;
        emissive += uDissolve.yzw * (1.0 - smoothstep(0.0, 0.06, n - threshold)) * 8.0;
    }

    if (!gl_FrontFacing)
        N = -N;
    vec3 V = normalize(uEye.xyz - vWorld);

    Material m = DefaultMaterial(uAlbedo, uSurface.x);
    m.metallic = uSurface.y;
    m.clearcoat = uSurface.z;
    m.clearcoatRoughness = uSurface.w;
    m.subsurface = uStyle.x;

    float radius = length(vLocal.xz);
    int pattern = int(uStyle.z + 0.5);
    if (pattern == 1) {
        // Turned wood: growth rings cut lengthwise, off the piece's axis.
        float warp = Fbm(vLocal * vec3(3.0, 0.8, 3.0)) * 1.2;
        float rings = fract(length(vLocal.xz + vec2(0.9, 0.35)) * 14.0 + vLocal.y * 0.8 + warp);
        float grain = smoothstep(0.0, 0.25, rings) * smoothstep(1.0, 0.55, rings);
        float fibers = mix(Noise(vLocal * 140.0), 0.5, smoothstep(0.004, 0.012, length(localPixel)));
        m.albedo *= 1.0 - uStyle.y * (0.22 * grain + 0.1 * fibers);
    }
    else if (pattern == 2) {
        // Lathe-turned metal: fine circular marks.
        float marks = mix(Noise(vec2(vLocal.y * 900.0, atan(vLocal.z, vLocal.x) * 3.0)), 0.5, smoothstep(0.0005, 0.0015, localPixel.y));
        m.roughness = clamp(m.roughness + (marks - 0.5) * 0.12 * uStyle.y, 0.03, 1.0);
    }
    else if (pattern == 3) {
        // Veined stone.
        float warp = Fbm(vLocal * 3.5 + 11.0);
        float veins = abs(sin((vLocal.x * 2.0 + vLocal.y * 3.0 + vLocal.z + warp * 3.0) * 4.0));
        float vein = 1.0 - smoothstep(0.0, 0.08, veins);
        m.albedo = mix(m.albedo, m.albedo * 0.55 + vec3(0.12), vein * uStyle.y);
        m.albedo *= 0.94 + 0.12 * Fbm(vLocal * 22.0);
    }

    // Felt under the foot.
    if (vLocal.y < 0.004 && N.y < -0.8) {
        m = DefaultMaterial(vec3(0.03, 0.09, 0.06), 1.0);
    }

    // The ground darkens what faces it, near the foot most.
    float height = vWorld.y - uStyle.w;
    float ground = mix(0.42, 1.0, smoothstep(0.0, 0.4, height));
    m.occlusion = vOcclusion * mix(1.0, ground, Saturate(0.65 - N.y * 0.65));

    // A neon ring round the foot.
    float ring = LineCoverage(abs(vLocal.y - 0.079), 0.009, max(localPixel.y, 1e-5));
    m.emissive = emissive + uTrim.rgb * uTrim.a * ring * step(0.15, radius);

    m.roughness = WidenRoughness(m.roughness, spread);
    m.clearcoatRoughness = WidenRoughness(m.clearcoatRoughness, spread);
    float shadow = uEye.w > 0.5 ? 1.0 : Shadow(vWorld, N);
    vec3 color = Shade(m, vWorld, N, V, shadow);

    // The selection glow: a halo at the silhouette and a little light all over.
    float NoV = Saturate(dot(N, V));
    color += uGlow.rgb * uGlow.a * (0.06 + 1.6 * pow(1.0 - NoV, 3.0));

    if (uMirror.z > 0.5)
        fragColor = vec4(EncodeSrgb(Aces(color * uMirror.w)), 1.0); // drawn straight onto the interface
    else
        fragColor = vec4(ApplyFog(color, vWorld), 1.0);
}
