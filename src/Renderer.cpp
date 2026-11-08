#include "Renderer.h"

#include "Resources.h"
#include "Text.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>
#include <utility>

namespace render {

using namespace gl;
using namespace math;

namespace {

int g_glslVersion = 460;

constexpr int ShadowSize = 2048;
constexpr float ShadowRadius = 7.6f;
constexpr float ShadowNear = 4.0f, ShadowFar = 36.0f;
constexpr int EnvironmentSize = 512;
constexpr int IrradianceSize = 32;
constexpr int PrefilteredSize = 256;
constexpr int BrdfSize = 128;
constexpr int LabelSize = 2048;

// std140 layouts of the uniform blocks in common.glsl.
struct FrameBlock {
    Mat4 viewProjection;
    Mat4 inverseViewProjection;
    Mat4 lightViewProjection;
    Vec4 cameraPosition;
    Vec4 lightDirection;
    Vec4 lightColor;
    Vec4 shadowParams;
    Vec4 viewport;
    Vec4 environment;
    Vec4 accent;
    Vec4 accentWarm;
    Vec4 fog;
};

struct PassBlock {
    Vec4 eye;
    Vec4 mirror;
};

Vec4 V4(Vec3 v, float w) { return {v.x, v.y, v.z, w}; }

int LevelsFor(int size)
{
    int levels = 1;
    while ((size >> levels) > 0)
        ++levels;
    return levels;
}

void Dispatch(int width, int height, int layers)
{
    glDispatchCompute(static_cast<GLuint>((width + 7) / 8), static_cast<GLuint>((height + 7) / 8), static_cast<GLuint>(layers));
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
}

void SetFilter(GLuint texture, GLenum minFilter, GLenum magFilter, GLenum wrap)
{
    glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(minFilter));
    glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(magFilter));
    glTextureParameteri(texture, GL_TEXTURE_WRAP_S, static_cast<GLint>(wrap));
    glTextureParameteri(texture, GL_TEXTURE_WRAP_T, static_cast<GLint>(wrap));
    glTextureParameteri(texture, GL_TEXTURE_WRAP_R, static_cast<GLint>(wrap));
}

void CheckFramebuffer(const Framebuffer& framebuffer, const char* name)
{
    if (glCheckNamedFramebufferStatus(framebuffer.Id(), GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error(std::format("The {} framebuffer is not complete.", name));
}

} // namespace

// Program ------------------------------------------------------------------------------------------

void Program::SetVersion(int glVersion)
{
    g_glslVersion = glVersion >= 46 ? 460 : 450;
}

Program::Program(std::string name, std::initializer_list<Stage> stages) : m_name(std::move(name))
{
    const GLuint program = glCreateProgram();
    m_program = ProgramObject(program);
    std::vector<GLuint> shaders;
    for (const Stage& stage : stages) {
        std::string source = std::format("#version {} core\n", g_glslVersion);
        int index = 0;
        for (const wchar_t* resource : stage.sources) {
            source += std::format("#line 1 {}\n", index++);
            source += resources::Text(resource);
            source += '\n';
        }
        const GLuint shader = glCreateShader(stage.type);
        const char* text = source.c_str();
        glShaderSource(shader, 1, &text, nullptr);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            std::string log(4096, '\0');
            GLsizei length = 0;
            glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), &length, log.data());
            log.resize(static_cast<std::size_t>(length));
            glDeleteShader(shader);
            for (GLuint attached : shaders)
                glDeleteShader(attached);
            throw std::runtime_error(std::format("The {} shader does not compile:\n{}", m_name, log));
        }
        glAttachShader(program, shader);
        shaders.push_back(shader);
    }
    glLinkProgram(program);
    for (GLuint shader : shaders)
        glDeleteShader(shader);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        std::string log(4096, '\0');
        GLsizei length = 0;
        glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), &length, log.data());
        log.resize(static_cast<std::size_t>(length));
        throw std::runtime_error(std::format("The {} shaders do not link:\n{}", m_name, log));
    }
}

GLint Program::Location(const char* name) const
{
    const auto found = m_locations.find(name);
    if (found != m_locations.end())
        return found->second;
    const GLint location = glGetUniformLocation(m_program.Id(), name);
    m_locations.emplace(name, location);
    return location;
}

void Program::Set(const char* name, int value) const { glProgramUniform1i(Id(), Location(name), value); }
void Program::Set(const char* name, float value) const { glProgramUniform1f(Id(), Location(name), value); }
void Program::Set(const char* name, Vec2 value) const { glProgramUniform2f(Id(), Location(name), value.x, value.y); }
void Program::Set(const char* name, Vec3 value) const { glProgramUniform3f(Id(), Location(name), value.x, value.y, value.z); }
void Program::Set(const char* name, Vec4 value) const { glProgramUniform4f(Id(), Location(name), value.x, value.y, value.z, value.w); }
void Program::Set(const char* name, const Mat4& value) const { glProgramUniformMatrix4fv(Id(), Location(name), 1, GL_FALSE, value.Data()); }

void Program::SetMask(const char* name, std::uint64_t bits) const
{
    glProgramUniform2ui(Id(), Location(name), static_cast<GLuint>(bits & 0xFFFFFFFFu), static_cast<GLuint>(bits >> 32));
}

void Program::SetArray(const char* name, std::span<const Vec4> values) const
{
    if (!values.empty())
        glProgramUniform4fv(Id(), Location(name), static_cast<GLsizei>(values.size()), &values.front().x);
}

// Meshes -------------------------------------------------------------------------------------------

void Renderer::GpuMesh::Draw() const
{
    if (count == 0)
        return;
    glBindVertexArray(vertexArray.Id());
    glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
}

Renderer::GpuMesh Renderer::Upload(const pieces::Mesh& mesh)
{
    GpuMesh gpu;
    gpu.vertices = CreateBuffer(mesh.vertices.data(), mesh.vertices.size() * sizeof(pieces::Vertex));
    gpu.indices = CreateBuffer(mesh.indices.data(), mesh.indices.size() * sizeof(std::uint32_t));
    gpu.count = static_cast<GLsizei>(mesh.indices.size());
    GLuint array = 0;
    glCreateVertexArrays(1, &array);
    gpu.vertexArray = VertexArray(array);
    glVertexArrayVertexBuffer(array, 0, gpu.vertices.Id(), 0, sizeof(pieces::Vertex));
    glVertexArrayElementBuffer(array, gpu.indices.Id());
    const GLuint offsets[3] = {offsetof(pieces::Vertex, position), offsetof(pieces::Vertex, normal), offsetof(pieces::Vertex, occlusion)};
    const GLint sizes[3] = {3, 3, 1};
    for (GLuint attribute = 0; attribute < 3; ++attribute) {
        glEnableVertexArrayAttrib(array, attribute);
        glVertexArrayAttribFormat(array, attribute, sizes[attribute], GL_FLOAT, GL_FALSE, offsets[attribute]);
        glVertexArrayAttribBinding(array, attribute, 0);
    }
    return gpu;
}

Renderer::GpuMesh Renderer::Upload(const board::Mesh& mesh)
{
    GpuMesh gpu;
    if (mesh.indices.empty())
        return gpu;
    gpu.vertices = CreateBuffer(mesh.vertices.data(), mesh.vertices.size() * sizeof(board::Vertex));
    gpu.indices = CreateBuffer(mesh.indices.data(), mesh.indices.size() * sizeof(std::uint32_t));
    gpu.count = static_cast<GLsizei>(mesh.indices.size());
    GLuint array = 0;
    glCreateVertexArrays(1, &array);
    gpu.vertexArray = VertexArray(array);
    glVertexArrayVertexBuffer(array, 0, gpu.vertices.Id(), 0, sizeof(board::Vertex));
    glVertexArrayElementBuffer(array, gpu.indices.Id());
    const GLuint offsets[3] = {offsetof(board::Vertex, position), offsetof(board::Vertex, normal), offsetof(board::Vertex, uv)};
    const GLint sizes[3] = {3, 3, 2};
    for (GLuint attribute = 0; attribute < 3; ++attribute) {
        glEnableVertexArrayAttrib(array, attribute);
        glVertexArrayAttribFormat(array, attribute, sizes[attribute], GL_FLOAT, GL_FALSE, offsets[attribute]);
        glVertexArrayAttribBinding(array, attribute, 0);
    }
    return gpu;
}

// Setup --------------------------------------------------------------------------------------------

Renderer::Renderer(const std::array<pieces::PieceShape, 6>& shapes, int glVersion) : m_glVersion(glVersion)
{
    Program::SetVersion(glVersion);

    using Stage = Program::Stage;
    m_pieceProgram = Program("piece", {Stage{GL_VERTEX_SHADER, {L"COMMON_GLSL", L"PIECE_VERT"}},
                                       Stage{GL_FRAGMENT_SHADER, {L"COMMON_GLSL", L"LIGHTING_GLSL", L"PIECE_FRAG"}}});
    m_surfaceProgram = Program("surface", {Stage{GL_VERTEX_SHADER, {L"COMMON_GLSL", L"SURFACE_VERT"}},
                                           Stage{GL_FRAGMENT_SHADER, {L"COMMON_GLSL", L"LIGHTING_GLSL", L"SURFACE_FRAG"}}});
    m_glowProgram = Program("glow", {Stage{GL_VERTEX_SHADER, {L"COMMON_GLSL", L"SURFACE_VERT"}}, Stage{GL_FRAGMENT_SHADER, {L"COMMON_GLSL", L"GLOW_FRAG"}}});
    m_shadowProgram = Program("shadow", {Stage{GL_VERTEX_SHADER, {L"COMMON_GLSL", L"SHADOW_VERT"}}, Stage{GL_FRAGMENT_SHADER, {L"COMMON_GLSL", L"SHADOW_FRAG"}}});
    m_backgroundProgram = Program("background", {Stage{GL_VERTEX_SHADER, {L"FULLSCREEN_VERT"}}, Stage{GL_FRAGMENT_SHADER, {L"COMMON_GLSL", L"BACKGROUND_FRAG"}}});
    m_resolveProgram = Program("resolve", {Stage{GL_VERTEX_SHADER, {L"FULLSCREEN_VERT"}}, Stage{GL_FRAGMENT_SHADER, {L"COMMON_GLSL", L"RESOLVE_FRAG"}}});
    m_bloomDownProgram = Program("bloom", {Stage{GL_VERTEX_SHADER, {L"FULLSCREEN_VERT"}}, Stage{GL_FRAGMENT_SHADER, {L"COMMON_GLSL", L"BLOOM_DOWN_FRAG"}}});
    m_bloomUpProgram = Program("bloom", {Stage{GL_VERTEX_SHADER, {L"FULLSCREEN_VERT"}}, Stage{GL_FRAGMENT_SHADER, {L"COMMON_GLSL", L"BLOOM_UP_FRAG"}}});
    m_compositeProgram = Program("composite", {Stage{GL_VERTEX_SHADER, {L"FULLSCREEN_VERT"}}, Stage{GL_FRAGMENT_SHADER, {L"COMMON_GLSL", L"COMPOSITE_FRAG"}}});
    m_environmentProgram = Program("environment", {Stage{GL_COMPUTE_SHADER, {L"IBL_COMMON_GLSL", L"ENVIRONMENT_COMP"}}});
    m_irradianceProgram = Program("irradiance", {Stage{GL_COMPUTE_SHADER, {L"IBL_COMMON_GLSL", L"IRRADIANCE_COMP"}}});
    m_prefilterProgram = Program("prefilter", {Stage{GL_COMPUTE_SHADER, {L"IBL_COMMON_GLSL", L"PREFILTER_COMP"}}});
    m_brdfProgram = Program("brdf", {Stage{GL_COMPUTE_SHADER, {L"IBL_COMMON_GLSL", L"BRDF_COMP"}}});

    for (int i = 0; i < 6; ++i) {
        m_pieceMeshes[i] = Upload(shapes[i].detailed);
        m_simplePieceMeshes[i] = Upload(shapes[i].simple);
        m_pieceHeights[i] = shapes[i].height;
    }
    m_squares = Upload(board::BuildSquares());
    m_frame = Upload(board::BuildFrame());
    m_table = Upload(board::BuildTable(40.0f));
    GLuint emptyArray = 0;
    glCreateVertexArrays(1, &emptyArray);
    m_emptyVertexArray = VertexArray(emptyArray);

    m_frameData = CreateBuffer(nullptr, sizeof(FrameBlock), GL_DYNAMIC_STORAGE_BIT);
    m_passData = CreateBuffer(nullptr, sizeof(PassBlock), GL_DYNAMIC_STORAGE_BIT);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_frameData.Id());
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, m_passData.Id());

    // The shadow map, read both with comparison (for filtering) and without (to find the casters).
    m_shadowMap = CreateTexture(GL_TEXTURE_2D);
    glTextureStorage2D(m_shadowMap.Id(), 1, GL_DEPTH_COMPONENT32F, ShadowSize, ShadowSize);
    m_shadowFramebuffer = CreateFramebuffer();
    glNamedFramebufferTexture(m_shadowFramebuffer.Id(), GL_DEPTH_ATTACHMENT, m_shadowMap.Id(), 0);
    glNamedFramebufferDrawBuffer(m_shadowFramebuffer.Id(), GL_NONE);
    CheckFramebuffer(m_shadowFramebuffer, "shadow");
    GLuint samplers[2] = {};
    glCreateSamplers(2, samplers);
    m_shadowCompareSampler = Sampler(samplers[0]);
    m_shadowDepthSampler = Sampler(samplers[1]);
    for (GLuint sampler : samplers) {
        glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glSamplerParameteri(samplers[0], GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glSamplerParameteri(samplers[0], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glSamplerParameteri(samplers[0], GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glSamplerParameteri(samplers[0], GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glSamplerParameteri(samplers[1], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glSamplerParameteri(samplers[1], GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    CreateEnvironmentTextures();

    // The coordinates inlaid in the frame.
    const std::vector<std::uint8_t> labels = text::RenderBoardLabels(LabelSize, board::FrameOuter, board::FrameInner);
    m_labels = CreateTexture(GL_TEXTURE_2D);
    glTextureStorage2D(m_labels.Id(), LevelsFor(LabelSize), GL_R8, LabelSize, LabelSize);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage2D(m_labels.Id(), 0, 0, 0, LabelSize, LabelSize, GL_RED, GL_UNSIGNED_BYTE, labels.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glGenerateTextureMipmap(m_labels.Id());
    SetFilter(m_labels.Id(), GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    GLfloat anisotropy = 1;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &anisotropy);
    glTextureParameterf(m_labels.Id(), GL_TEXTURE_MAX_ANISOTROPY, std::min(anisotropy, 8.0f));

    GLint maxSamples = 4, maxColorSamples = 4, maxDepthSamples = 4;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    glGetIntegerv(GL_MAX_COLOR_TEXTURE_SAMPLES, &maxColorSamples);
    glGetIntegerv(GL_MAX_DEPTH_TEXTURE_SAMPLES, &maxDepthSamples);
    m_samples = std::max(1, std::min({4, maxSamples, maxColorSamples, maxDepthSamples}));
}

Renderer::~Renderer() = default;

void Renderer::CreateEnvironmentTextures()
{
    auto cube = [](int size, int levels) {
        Texture texture = CreateTexture(GL_TEXTURE_CUBE_MAP);
        glTextureStorage2D(texture.Id(), levels, GL_RGBA16F, size, size);
        SetFilter(texture.Id(), levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
        return texture;
    };
    m_environment = cube(EnvironmentSize, LevelsFor(EnvironmentSize));
    m_irradiance = cube(IrradianceSize, 1);
    m_prefiltered = cube(PrefilteredSize, m_prefilteredLevels);

    m_brdf = CreateTexture(GL_TEXTURE_2D);
    glTextureStorage2D(m_brdf.Id(), 1, GL_RG16F, BrdfSize, BrdfSize);
    SetFilter(m_brdf.Id(), GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glUseProgram(m_brdfProgram.Id());
    glBindImageTexture(0, m_brdf.Id(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);
    Dispatch(BrdfSize, BrdfSize, 1);
}

// The studio light of the theme, baked into the light probes.
void Renderer::BakeEnvironment()
{
    const Program& environment = m_environmentProgram;
    environment.Set("uCeiling", m_theme.ceiling);
    environment.Set("uHorizon", m_theme.horizon);
    environment.Set("uFloor", m_theme.floor);
    std::array<Vec4, 8> directions{}, shapes{}, colors{};
    for (int i = 0; i < m_theme.panelCount; ++i) {
        const StudioPanel& panel = m_theme.panels[i];
        directions[i] = V4(panel.direction, panel.roll);
        shapes[i] = {panel.halfSize.x, panel.halfSize.y, panel.corner, panel.softness};
        colors[i] = V4(panel.radiance, panel.falloff);
    }
    environment.SetArray("uPanelDirection", directions);
    environment.SetArray("uPanelShape", shapes);
    environment.SetArray("uPanelColor", colors);
    environment.Set("uPanelCount", m_theme.panelCount);
    environment.Use();
    glBindImageTexture(0, m_environment.Id(), 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    Dispatch(EnvironmentSize, EnvironmentSize, 6);
    glGenerateTextureMipmap(m_environment.Id());

    glBindTextureUnit(0, m_environment.Id());
    m_irradianceProgram.Set("uSourceLod", 4.0f);
    m_irradianceProgram.Use();
    glBindImageTexture(0, m_irradiance.Id(), 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    Dispatch(IrradianceSize, IrradianceSize, 6);

    m_prefilterProgram.Set("uSourceSize", static_cast<float>(EnvironmentSize));
    m_prefilterProgram.Use();
    for (int level = 0; level < m_prefilteredLevels; ++level) {
        const int size = PrefilteredSize >> level;
        m_prefilterProgram.Set("uRoughness", static_cast<float>(level) / static_cast<float>(m_prefilteredLevels - 1));
        glBindImageTexture(0, m_prefiltered.Id(), level, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        Dispatch(size, size, 6);
    }
}

void Renderer::SetTheme(const Theme& theme)
{
    m_theme = theme;

    const Vec3 toLight = Normalize(theme.keyDirection);
    const Vec3 up = std::abs(toLight.y) > 0.99f ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
    const Vec3 center{0.5f, 0, 0};
    const Mat4 view = Mat4::LookAt(center + toLight * 20.0f, center, up);
    const Mat4 projection = Mat4::Orthographic(-ShadowRadius, ShadowRadius, -ShadowRadius, ShadowRadius, ShadowNear, ShadowFar);
    m_lightViewProjection = projection * view;

    BakeEnvironment();
}

void Renderer::Resize(int width, int height)
{
    width = std::max(width, 1);
    height = std::max(height, 1);
    if (width == m_width && height == m_height)
        return;
    m_width = width;
    m_height = height;
    CreateScreenTargets();
}

void Renderer::CreateScreenTargets()
{
    m_sceneColor = CreateTexture(GL_TEXTURE_2D_MULTISAMPLE);
    glTextureStorage2DMultisample(m_sceneColor.Id(), m_samples, GL_RGBA16F, m_width, m_height, GL_TRUE);
    m_sceneDepth = CreateTexture(GL_TEXTURE_2D_MULTISAMPLE);
    glTextureStorage2DMultisample(m_sceneDepth.Id(), m_samples, GL_DEPTH_COMPONENT32F, m_width, m_height, GL_TRUE);
    m_sceneFramebuffer = CreateFramebuffer();
    glNamedFramebufferTexture(m_sceneFramebuffer.Id(), GL_COLOR_ATTACHMENT0, m_sceneColor.Id(), 0);
    glNamedFramebufferTexture(m_sceneFramebuffer.Id(), GL_DEPTH_ATTACHMENT, m_sceneDepth.Id(), 0);
    CheckFramebuffer(m_sceneFramebuffer, "scene");

    m_resolved = CreateTexture(GL_TEXTURE_2D);
    glTextureStorage2D(m_resolved.Id(), 1, GL_RGBA16F, m_width, m_height);
    SetFilter(m_resolved.Id(), GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    m_resolvedFramebuffer = CreateFramebuffer();
    glNamedFramebufferTexture(m_resolvedFramebuffer.Id(), GL_COLOR_ATTACHMENT0, m_resolved.Id(), 0);
    CheckFramebuffer(m_resolvedFramebuffer, "resolve");

    // The reflections at half size, blurred through their mip levels.
    const int reflectionWidth = std::max(1, m_width / 2), reflectionHeight = std::max(1, m_height / 2);
    m_reflectionLevels = std::min(5, LevelsFor(std::max(reflectionWidth, reflectionHeight)));
    m_reflectionColor = CreateTexture(GL_TEXTURE_2D);
    glTextureStorage2D(m_reflectionColor.Id(), m_reflectionLevels, GL_RGBA16F, reflectionWidth, reflectionHeight);
    SetFilter(m_reflectionColor.Id(), GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
    m_reflectionDepth = CreateTexture(GL_TEXTURE_2D);
    glTextureStorage2D(m_reflectionDepth.Id(), 1, GL_DEPTH_COMPONENT24, reflectionWidth, reflectionHeight);
    m_reflectionFramebuffer = CreateFramebuffer();
    glNamedFramebufferTexture(m_reflectionFramebuffer.Id(), GL_COLOR_ATTACHMENT0, m_reflectionColor.Id(), 0);
    glNamedFramebufferTexture(m_reflectionFramebuffer.Id(), GL_DEPTH_ATTACHMENT, m_reflectionDepth.Id(), 0);
    CheckFramebuffer(m_reflectionFramebuffer, "reflection");

    for (int level = 0; level < BloomLevels; ++level) {
        const int width = std::max(1, m_width >> (level + 1)), height = std::max(1, m_height >> (level + 1));
        m_bloomSizes[level] = {width, height};
        m_bloom[level] = CreateTexture(GL_TEXTURE_2D);
        glTextureStorage2D(m_bloom[level].Id(), 1, GL_R11F_G11F_B10F, width, height);
        SetFilter(m_bloom[level].Id(), GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE);
        m_bloomFramebuffers[level] = CreateFramebuffer();
        glNamedFramebufferTexture(m_bloomFramebuffers[level].Id(), GL_COLOR_ATTACHMENT0, m_bloom[level].Id(), 0);
        CheckFramebuffer(m_bloomFramebuffers[level], "bloom");
    }
}

// Frame --------------------------------------------------------------------------------------------

void Renderer::UpdateFrameData(const Scene& scene)
{
    FrameBlock block;
    block.viewProjection = scene.projection * scene.view;
    block.inverseViewProjection = block.viewProjection.Inverse();
    block.lightViewProjection = m_lightViewProjection;
    block.cameraPosition = V4(scene.eye, scene.time);
    block.lightDirection = V4(Normalize(m_theme.keyDirection), 2 * ShadowRadius / ShadowSize);
    block.lightColor = V4(m_theme.keyRadiance, m_theme.keySoftness);
    block.shadowParams = {ShadowFar - ShadowNear, 0.00025f, 0, 0};
    block.viewport = {static_cast<float>(m_width), static_cast<float>(m_height), 1.0f / static_cast<float>(m_width), 1.0f / static_cast<float>(m_height)};
    block.environment = {m_theme.diffuseLight, m_theme.reflectedLight, static_cast<float>(m_prefilteredLevels - 1), m_theme.background};
    block.accent = V4(m_theme.accent, 1);
    block.accentWarm = V4(m_theme.accentWarm, 1);
    block.fog = V4(m_theme.fog, 0.05f);
    glNamedBufferSubData(m_frameData.Id(), 0, sizeof(block), &block);
}

void Renderer::SetPass(Vec3 eye, bool shadows, bool mirror, bool display)
{
    const PassBlock block{V4(eye, shadows ? 0.0f : 1.0f), {0, mirror ? 1.0f : 0.0f, display ? 1.0f : 0.0f, m_theme.exposure}};
    glNamedBufferSubData(m_passData.Id(), 0, sizeof(block), &block);
}

void Renderer::BindLighting(bool reflections) const
{
    glBindTextureUnit(0, m_shadowMap.Id());
    glBindSampler(0, m_shadowCompareSampler.Id());
    glBindTextureUnit(1, m_shadowMap.Id());
    glBindSampler(1, m_shadowDepthSampler.Id());
    glBindTextureUnit(2, m_irradiance.Id());
    glBindTextureUnit(3, m_prefiltered.Id());
    glBindTextureUnit(4, m_brdf.Id());
    glBindTextureUnit(5, reflections ? m_reflectionColor.Id() : 0);
    glBindTextureUnit(6, m_labels.Id());
}

void Renderer::Render(const Scene& scene)
{
    UpdateFrameData(scene);
    DrawShadowMap(scene);
    DrawReflections(scene);
    DrawMainPass(scene);
    PostProcess(scene);
}

void Renderer::DrawShadowMap(const Scene& scene)
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFramebuffer.Id());
    glViewport(0, 0, ShadowSize, ShadowSize);
    const float farDepth = 1;
    glClearNamedFramebufferfv(m_shadowFramebuffer.Id(), GL_DEPTH, 0, &farDepth);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 3.0f);

    m_shadowProgram.Use();
    m_shadowProgram.Set("uModel", Mat4::Identity());
    m_shadowProgram.Set("uDissolve", 0.0f);
    m_frame.Draw();
    m_squares.Draw();
    for (const PieceDraw& piece : scene.pieces) {
        m_shadowProgram.Set("uModel", piece.model);
        m_shadowProgram.Set("uDissolve", piece.dissolve);
        m_simplePieceMeshes[std::to_underlying(piece.type) - 1].Draw();
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
}

void Renderer::DrawReflections(const Scene& scene)
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_reflectionFramebuffer.Id());
    glViewport(0, 0, std::max(1, m_width / 2), std::max(1, m_height / 2));
    const float clear[4] = {0, 0, 0, 0};
    const float farDepth = 1;
    glClearNamedFramebufferfv(m_reflectionFramebuffer.Id(), GL_COLOR, 0, clear);
    glClearNamedFramebufferfv(m_reflectionFramebuffer.Id(), GL_DEPTH, 0, &farDepth);

    // The pieces turned upside down under the board, seen from the camera, as if from below the board.
    SetPass({scene.eye.x, -scene.eye.y, scene.eye.z}, false, true);
    BindLighting(false);
    glEnable(GL_CLIP_DISTANCE0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CW);
    DrawPieces(scene, false);
    glFrontFace(GL_CCW);
    glDisable(GL_CLIP_DISTANCE0);
    glGenerateTextureMipmap(m_reflectionColor.Id());
}

void Renderer::SetPieceMaterial(const PieceDraw& piece) const
{
    const PieceLook& look = piece.color == chess::Color::White ? m_theme.white : m_theme.black;
    const Program& program = m_pieceProgram;
    program.Set("uModel", piece.model);
    program.Set("uAlbedo", look.albedo);
    program.Set("uSurface", Vec4{look.roughness, look.metallic, look.clearcoat, look.clearcoatRoughness});
    program.Set("uStyle", Vec4{look.subsurface, look.patternStrength, look.pattern, piece.ground});
    program.Set("uGlow", V4(piece.glowColor, piece.glow));
    program.Set("uTrim", V4(look.trim, look.trimStrength));
    program.Set("uDissolve", Vec4{piece.dissolve, m_theme.accent.x, m_theme.accent.y, m_theme.accent.z});
}

void Renderer::DrawPieces(const Scene& scene, bool detailed)
{
    m_pieceProgram.Use();
    for (const PieceDraw& piece : scene.pieces) {
        SetPieceMaterial(piece);
        const int index = std::to_underlying(piece.type) - 1;
        (detailed ? m_pieceMeshes : m_simplePieceMeshes)[index].Draw();
    }
}

void Renderer::SetSurfaceMaterial(const Scene& scene) const
{
    const Program& p = m_surfaceProgram;
    const Theme& t = m_theme;
    p.Set("uLightSquare", t.lightSquare);
    p.Set("uDarkSquare", t.darkSquare);
    p.Set("uBoardSurface", Vec4{t.boardRoughness, t.boardClearcoat, t.boardClearcoatRoughness, t.boardPattern});
    p.Set("uBoardReflection", Vec4{t.reflection, t.reflectionBlur, t.veinBrightness, t.edgeGlow});
    p.Set("uFrameColor", t.frameColor);
    p.Set("uFrameSurface", Vec4{t.frameRoughness, t.frameMetallic, t.frameClearcoat, t.framePattern});
    p.Set("uInlayColor", t.inlayColor);
    p.Set("uInlay", Vec4{t.inlayMetallic, t.inlayRoughness, t.inlayGlow, 0});
    p.Set("uTableColor", t.tableColor);
    p.Set("uTableSurface", Vec4{t.tableRoughness, t.tablePattern, t.tableSheen, t.gridGlow});
    p.SetMask("uSelected", scene.marks.selected);
    p.SetMask("uQuiet", scene.marks.quiet);
    p.SetMask("uCapture", scene.marks.capture);
    p.SetMask("uLastMove", scene.marks.lastMove);
    p.SetMask("uCheck", scene.marks.check);
    p.SetMask("uHover", scene.marks.hover);
    std::vector<Vec4> discs;
    for (const ContactShadow& contact : scene.contacts)
        if (discs.size() < 48)
            discs.push_back({contact.x, contact.z, contact.radius, contact.strength});
    p.SetArray("uDiscs", discs);
    p.Set("uDiscCount", static_cast<int>(discs.size()));
}

void Renderer::DrawMainPass(const Scene& scene)
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFramebuffer.Id());
    glViewport(0, 0, m_width, m_height);
    const float clear[4] = {m_theme.fog.x, m_theme.fog.y, m_theme.fog.z, 1};
    const float farDepth = 1;
    glClearNamedFramebufferfv(m_sceneFramebuffer.Id(), GL_COLOR, 0, clear);
    glClearNamedFramebufferfv(m_sceneFramebuffer.Id(), GL_DEPTH, 0, &farDepth);

    SetPass(scene.eye, true, false);
    BindLighting(true);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    SetSurfaceMaterial(scene);
    m_surfaceProgram.Use();
    m_surfaceProgram.Set("uKind", 2);
    m_table.Draw();
    m_surfaceProgram.Set("uKind", 1);
    m_frame.Draw();
    m_surfaceProgram.Set("uKind", 0);
    m_squares.Draw();

    DrawPieces(scene, true);

    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    m_backgroundProgram.Use();
    glBindVertexArray(m_emptyVertexArray.Id());
    glDrawArrays(GL_TRIANGLES, 0, 3);

    if (scene.arrow) {
        const Arrow& arrow = *scene.arrow;
        if (m_arrow.count == 0 || Length(arrow.from - m_arrowFrom) > 1e-4f || Length(arrow.to - m_arrowTo) > 1e-4f) {
            m_arrow = Upload(board::BuildArrow(arrow.from, arrow.to));
            m_arrowFrom = arrow.from;
            m_arrowTo = arrow.to;
        }
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        m_glowProgram.Set("uColor", V4(arrow.color, arrow.alpha));
        m_glowProgram.Use();
        m_arrow.Draw();
        glDisable(GL_BLEND);
    }
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
}

void Renderer::PostProcess(const Scene& scene)
{
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(m_emptyVertexArray.Id());

    glBindFramebuffer(GL_FRAMEBUFFER, m_resolvedFramebuffer.Id());
    glViewport(0, 0, m_width, m_height);
    m_resolveProgram.Set("uSamples", m_samples);
    m_resolveProgram.Use();
    glBindTextureUnit(0, m_sceneColor.Id());
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Bloom: halve down to a small image, then add the levels back up.
    m_bloomDownProgram.Use();
    for (int level = 0; level < BloomLevels; ++level) {
        const auto [width, height] = m_bloomSizes[level];
        glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFramebuffers[level].Id());
        glViewport(0, 0, width, height);
        const int sourceWidth = level == 0 ? m_width : m_bloomSizes[level - 1][0];
        const int sourceHeight = level == 0 ? m_height : m_bloomSizes[level - 1][1];
        glBindTextureUnit(0, level == 0 ? m_resolved.Id() : m_bloom[level - 1].Id());
        m_bloomDownProgram.Set("uSourceTexel", Vec2{1.0f / static_cast<float>(sourceWidth), 1.0f / static_cast<float>(sourceHeight)});
        m_bloomDownProgram.Set("uFirst", level == 0 ? 1 : 0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    m_bloomUpProgram.Use();
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (int level = BloomLevels - 2; level >= 0; --level) {
        const auto [width, height] = m_bloomSizes[level];
        glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFramebuffers[level].Id());
        glViewport(0, 0, width, height);
        glBindTextureUnit(0, m_bloom[level + 1].Id());
        m_bloomUpProgram.Set("uSourceTexel", Vec2{1.0f / static_cast<float>(m_bloomSizes[level + 1][0]), 1.0f / static_cast<float>(m_bloomSizes[level + 1][1])});
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glDisable(GL_BLEND);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_width, m_height);
    const Program& composite = m_compositeProgram;
    composite.Set("uExposure", m_theme.exposure);
    composite.Set("uBloomStrength", m_theme.bloom);
    composite.Set("uVignette", m_theme.vignette);
    std::array<Vec4, 4> panels{}, styles{};
    const int count = static_cast<int>(std::min<std::size_t>(scene.panels.size(), 4));
    for (int i = 0; i < count; ++i) {
        const FrostedPanel& panel = scene.panels[i];
        panels[i] = {panel.x + panel.width / 2, panel.y + panel.height / 2, panel.width / 2, panel.height / 2};
        styles[i] = {panel.radius, panel.opacity, 0, 0};
    }
    composite.SetArray("uPanels", panels);
    composite.SetArray("uPanelStyle", styles);
    composite.Set("uPanelCount", count);
    composite.Use();
    glBindTextureUnit(0, m_resolved.Id());
    glBindTextureUnit(1, m_bloom[0].Id());
    glBindTextureUnit(2, m_bloom[2].Id());
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void Renderer::DrawPieceIcon(chess::PieceType type, chess::Color color, float x, float y, float size, float angle)
{
    const int index = std::to_underlying(type) - 1;
    const float height = m_pieceHeights[index];
    const int pixels = std::max(1, static_cast<int>(size));
    const int left = static_cast<int>(x), bottom = m_height - static_cast<int>(y) - pixels;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(left, bottom, pixels, pixels);
    glEnable(GL_SCISSOR_TEST);
    glScissor(left, bottom, pixels, pixels);
    glClear(GL_DEPTH_BUFFER_BIT);

    const Vec3 target{0, height * 0.5f, 0};
    const Vec3 eye = target + Vec3{0, height * 0.32f, 1} * (1.55f * std::max(height, 0.85f) + 0.6f);
    const Mat4 viewProjection = Mat4::Perspective(0.62f, 1, 0.1f, 20) * Mat4::LookAt(eye, target, {0, 1, 0});
    glNamedBufferSubData(m_frameData.Id(), 0, sizeof(Mat4), &viewProjection);
    SetPass(eye, false, false, true);
    BindLighting(false);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    PieceDraw piece;
    piece.type = type;
    piece.color = color;
    piece.model = Mat4::RotationY(angle);
    piece.ground = -100;
    m_pieceProgram.Use();
    SetPieceMaterial(piece);
    m_pieceMeshes[index].Draw();

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, m_width, m_height);
}

} // namespace render
