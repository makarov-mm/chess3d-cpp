#pragma once

#include "BoardGeometry.h"
#include "Chess.h"
#include "Gl.h"
#include "MathTypes.h"
#include "PieceModels.h"
#include "Themes.h"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace render {

struct PieceDraw {
    chess::PieceType type = chess::PieceType::Pawn;
    chess::Color color = chess::Color::White;
    math::Mat4 model = math::Mat4::Identity();
    float ground = 0; // the height of what it stands on
    math::Vec3 glowColor;
    float glow = 0;
    float dissolve = 0;
};

// A soft dark patch round the foot of a piece.
struct ContactShadow {
    float x = 0, z = 0, radius = 0, strength = 0;
};

struct BoardMarks {
    std::uint64_t selected = 0, quiet = 0, capture = 0, lastMove = 0, check = 0, hover = 0;
};

struct Arrow {
    math::Vec3 from, to;
    math::Vec3 color;
    float alpha = 1;
};

// A rectangle of the screen behind which the scene is blurred, in pixels from the top left.
struct FrostedPanel {
    float x = 0, y = 0, width = 0, height = 0, radius = 0, opacity = 1;
};

struct Scene {
    math::Mat4 view = math::Mat4::Identity();
    math::Mat4 projection = math::Mat4::Identity();
    math::Vec3 eye;
    float time = 0;
    std::vector<PieceDraw> pieces;
    std::vector<ContactShadow> contacts;
    BoardMarks marks;
    std::optional<Arrow> arrow;
    std::vector<FrostedPanel> panels;
};

// A linked shader program, its sources taken from the executable's resources.
class Program {
public:
    struct Stage {
        gl::GLenum type;
        std::initializer_list<const wchar_t*> sources;
    };

    Program() = default;
    Program(std::string name, std::initializer_list<Stage> stages);
    // The GLSL version the shaders are compiled for, from the context's OpenGL version (46 for 4.6).
    static void SetVersion(int glVersion);

    gl::GLuint Id() const { return m_program.Id(); }
    void Use() const { gl::glUseProgram(m_program.Id()); }
    gl::GLint Location(const char* name) const;

    void Set(const char* name, int value) const;
    void Set(const char* name, float value) const;
    void Set(const char* name, math::Vec2 value) const;
    void Set(const char* name, math::Vec3 value) const;
    void Set(const char* name, math::Vec4 value) const;
    void Set(const char* name, const math::Mat4& value) const;
    void SetMask(const char* name, std::uint64_t bits) const;
    void SetArray(const char* name, std::span<const math::Vec4> values) const;

private:
    std::string m_name;
    gl::ProgramObject m_program;
    mutable std::unordered_map<std::string, gl::GLint> m_locations;
};

// Draws the scene into the window: shadows, reflections, the lit scene in high dynamic range with
// multisampling, then bloom and tone mapping. The interface is drawn on top afterwards.
class Renderer {
public:
    explicit Renderer(const std::array<pieces::PieceShape, 6>& shapes, int glVersion);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void SetTheme(const Theme& theme);
    void Resize(int width, int height);
    void Render(const Scene& scene);
    // A piece turning in a square of the screen, lit like the scene; for the promotion choice.
    void DrawPieceIcon(chess::PieceType type, chess::Color color, float x, float y, float size, float angle);

    int Width() const { return m_width; }
    int Height() const { return m_height; }

private:
    struct GpuMesh {
        gl::VertexArray vertexArray;
        gl::Buffer vertices;
        gl::Buffer indices;
        gl::GLsizei count = 0;
        void Draw() const;
    };

    static GpuMesh Upload(const pieces::Mesh& mesh);
    static GpuMesh Upload(const board::Mesh& mesh);
    void CreateEnvironmentTextures();
    void BakeEnvironment();
    void CreateScreenTargets();
    void UpdateFrameData(const Scene& scene);
    void SetPass(math::Vec3 eye, bool shadows, bool mirror, bool display = false);
    void BindLighting(bool reflections) const;
    void DrawShadowMap(const Scene& scene);
    void DrawReflections(const Scene& scene);
    void DrawMainPass(const Scene& scene);
    void DrawPieces(const Scene& scene, bool detailed);
    void SetPieceMaterial(const PieceDraw& piece) const;
    void SetSurfaceMaterial(const Scene& scene) const;
    void PostProcess(const Scene& scene);

    int m_glVersion;
    int m_width = 0, m_height = 0;
    int m_samples = 4;
    Theme m_theme;
    math::Mat4 m_lightViewProjection;

    std::array<GpuMesh, 6> m_pieceMeshes;
    std::array<GpuMesh, 6> m_simplePieceMeshes;
    std::array<float, 6> m_pieceHeights{};
    GpuMesh m_squares, m_frame, m_table, m_arrow;
    math::Vec3 m_arrowFrom, m_arrowTo;
    gl::VertexArray m_emptyVertexArray;

    Program m_pieceProgram, m_surfaceProgram, m_glowProgram, m_shadowProgram, m_backgroundProgram;
    Program m_resolveProgram, m_bloomDownProgram, m_bloomUpProgram, m_compositeProgram;
    Program m_environmentProgram, m_irradianceProgram, m_prefilterProgram, m_brdfProgram;

    gl::Buffer m_frameData, m_passData;

    gl::Texture m_shadowMap;
    gl::Framebuffer m_shadowFramebuffer;
    gl::Sampler m_shadowCompareSampler, m_shadowDepthSampler;
    gl::Texture m_environment, m_irradiance, m_prefiltered, m_brdf, m_labels;
    int m_prefilteredLevels = 6;

    gl::Texture m_sceneColor, m_sceneDepth, m_resolved;
    gl::Framebuffer m_sceneFramebuffer, m_resolvedFramebuffer;
    gl::Texture m_reflectionColor, m_reflectionDepth;
    gl::Framebuffer m_reflectionFramebuffer;
    int m_reflectionLevels = 1;
    static constexpr int BloomLevels = 6;
    std::array<gl::Texture, BloomLevels> m_bloom;
    std::array<gl::Framebuffer, BloomLevels> m_bloomFramebuffers;
    std::array<std::array<int, 2>, BloomLevels> m_bloomSizes{};
};

} // namespace render
