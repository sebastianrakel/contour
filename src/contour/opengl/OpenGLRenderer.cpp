/**
 * This file is part of the "libterminal" project
 *   Copyright (c) 2019-2021 Christian Parpart <christian@parpart.family>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
// TODO(pr) #include <contour/helper.h>
#include <contour/helper.h>
#include <contour/opengl/OpenGLRenderer.h>
#include <contour/opengl/ShaderConfig.h>

#include <terminal_renderer/TextureAtlas.h>

#include <crispy/algorithm.h>
#include <crispy/utils.h>

#include <range/v3/all.hpp>

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

using std::array;
using std::min;
using std::nullopt;
using std::optional;
using std::pair;
using std::string;
using std::vector;

using terminal::Height;
using terminal::ImageSize;
using terminal::RGBAColor;
using terminal::Width;

namespace atlas = terminal::renderer::atlas;

namespace contour::opengl
{

namespace atlas = terminal::renderer::atlas;

#if !defined(NDEBUG)
    #define CHECKED_GL(code)                                                      \
        do                                                                        \
        {                                                                         \
            (code);                                                               \
            GLenum err {};                                                        \
            while ((err = glGetError()) != GL_NO_ERROR)                           \
                LOGSTORE(DisplayLog)("OpenGL error {} for call: {}", err, #code); \
        } while (0)
#else
    #define CHECKED_GL(code) \
        do                   \
        {                    \
            (code);          \
        } while (0)
#endif

namespace
{
    array<GLfloat, 4> normalize(RGBAColor color) noexcept
    {
        return std::array<GLfloat, 4> { static_cast<GLfloat>(color.red()) / 255.f,
                                        static_cast<GLfloat>(color.green()) / 255.f,
                                        static_cast<GLfloat>(color.blue()) / 255.f,
                                        static_cast<GLfloat>(color.alpha()) / 255.f };
    }

    template <typename T, typename Fn>
    inline void bound(T& _bindable, Fn&& _callable)
    {
        _bindable.bind();
        try
        {
            _callable();
        }
        catch (...)
        {
            _bindable.release();
            throw;
        }
        _bindable.release();
    }

    template <typename F>
    inline void checkedGL(F&& region) noexcept
    {
        region();
        auto err = GLenum {};
        while ((err = glGetError()) != GL_NO_ERROR)
            LOGSTORE(DisplayLog)("OpenGL error {} for call.", err);
    }

    int glFormat(atlas::Format _format)
    {
        switch (_format)
        {
        case atlas::Format::RGBA: return GL_RGBA;
        case atlas::Format::RGB: return GL_RGB;
        case atlas::Format::Red: return GL_RED;
        }
        return GL_RED;
    }

    QMatrix4x4 ortho(float left, float right, float bottom, float top)
    {
        constexpr float nearPlane = -1.0f;
        constexpr float farPlane = 1.0f;

        QMatrix4x4 mat;
        mat.ortho(left, right, bottom, top, nearPlane, farPlane);
        return mat;
    }
} // namespace

constexpr int MaxColorTextureSize = 2048;

/**
 * Text rendering input:
 *  - vec3 screenCoord    (x/y/z)
 *  - vec4 textureCoord   (x/y and w/h)
 *  - vec4 textColor      (r/g/b/a)
 *
 */

OpenGLRenderer::OpenGLRenderer(ShaderConfig const& textShaderConfig,
                               ShaderConfig const& rectShaderConfig,
                               ImageSize renderSize,
                               terminal::renderer::PageMargin margin):
    _renderTargetSize { renderSize },
    _projectionMatrix { ortho(0.0f,
                              float(*renderSize.width), // left, right
                              0.0f,
                              float(*renderSize.height) // bottom, top
                              ) },
    _margin { margin },
    _textShader { createShader(textShaderConfig) },
    _textProjectionLocation { _textShader->uniformLocation("vs_projection") },
    // textureScheduler_ {
    //     std::make_unique<TextureScheduler>()
    // }, // TODO(pr) ensure it's initialized and always updated
    // texture
    // rect
    _rectShader { createShader(rectShaderConfig) },
    _rectProjectionLocation { _rectShader->uniformLocation("u_projection") }
{
    initialize();

    setRenderSize(renderSize);

    assert(_textProjectionLocation != -1);

    CHECKED_GL(glEnable(GL_BLEND));
    CHECKED_GL(glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE));
    // glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    //  //glBlendFunc(GL_SRC1_COLOR, GL_ONE_MINUS_SRC1_COLOR);

    bound(*_textShader, [&]() {
        CHECKED_GL(_textShader->setUniformValue(
            "fs_textureAtlas", 0)); // GL_TEXTURE0 (used to be passed via userdata from atlas allocator)
        auto constexpr textureAtlasWidth = 1024.f; // TODO(pr)
        CHECKED_GL(_textShader->setUniformValue("pixel_x", 1.0f / textureAtlasWidth));
    });

    initializeRectRendering();
    initializeTextureRendering();
}

crispy::ImageSize OpenGLRenderer::colorTextureSizeHint() noexcept
{
    return ImageSize { Width(min(MaxColorTextureSize, maxTextureSize())),
                       Height(min(MaxColorTextureSize, maxTextureSize())) };
}

void OpenGLRenderer::setRenderSize(ImageSize renderSize)
{
    _renderTargetSize = renderSize;
    _projectionMatrix = ortho(0.0f,
                              float(*_renderTargetSize.width), // left, right
                              0.0f,
                              float(*_renderTargetSize.height) // bottom, top
    );
}

void OpenGLRenderer::setMargin(terminal::renderer::PageMargin margin) noexcept
{
    _margin = margin;
}

atlas::AtlasBackend& OpenGLRenderer::textureScheduler()
{
    return *this;
}

void OpenGLRenderer::initializeRectRendering()
{
    CHECKED_GL(glGenVertexArrays(1, &_rectVAO));
    CHECKED_GL(glBindVertexArray(_rectVAO));

    CHECKED_GL(glGenBuffers(1, &_rectVBO));
    CHECKED_GL(glBindBuffer(GL_ARRAY_BUFFER, _rectVBO));
    CHECKED_GL(glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STREAM_DRAW));

    auto constexpr BufferStride = 7 * sizeof(GLfloat);
    auto const VertexOffset = (void const*) (0 * sizeof(GLfloat));
    auto const ColorOffset = (void const*) (3 * sizeof(GLfloat));

    // 0 (vec3): vertex buffer
    CHECKED_GL(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, BufferStride, VertexOffset));
    CHECKED_GL(glEnableVertexAttribArray(0));

    // 1 (vec4): color buffer
    CHECKED_GL(glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, BufferStride, ColorOffset));
    CHECKED_GL(glEnableVertexAttribArray(1));
}

void OpenGLRenderer::initializeTextureRendering()
{
    CHECKED_GL(glGenVertexArrays(1, &_vao));
    CHECKED_GL(glBindVertexArray(_vao));

    auto constexpr BufferStride = (3 + 4 + 4) * sizeof(GLfloat);
    auto constexpr VertexOffset = (void const*) 0;
    auto const TexCoordOffset = (void const*) (3 * sizeof(GLfloat));
    auto const ColorOffset = (void const*) (7 * sizeof(GLfloat));

    CHECKED_GL(glGenBuffers(1, &_vbo));
    CHECKED_GL(glBindBuffer(GL_ARRAY_BUFFER, _vbo));
    CHECKED_GL(glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STREAM_DRAW));

    // 0 (vec3): vertex buffer
    CHECKED_GL(glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, BufferStride, VertexOffset));
    CHECKED_GL(glEnableVertexAttribArray(0));

    // 1 (vec4): texture coordinates buffer
    CHECKED_GL(glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, BufferStride, TexCoordOffset));
    CHECKED_GL(glEnableVertexAttribArray(1));

    // 2 (vec4): color buffer
    CHECKED_GL(glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, BufferStride, ColorOffset));
    CHECKED_GL(glEnableVertexAttribArray(2));

    // setup EBO
    // glGenBuffers(1, &_ebo);
    // glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _ebo);
    // static const GLuint indices[6] = { 0, 1, 3, 1, 2, 3 };
    // glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // glVertexAttribDivisor(0, 1); // TODO: later for instanced rendering
}

OpenGLRenderer::~OpenGLRenderer()
{
    LOGSTORE(DisplayLog)("~OpenGLRenderer");
    CHECKED_GL(glDeleteVertexArrays(1, &_rectVAO));
    CHECKED_GL(glDeleteBuffers(1, &_rectVBO));
}

void OpenGLRenderer::initialize()
{
    if (!_initialized)
    {
        _initialized = true;
        initializeOpenGLFunctions();
    }
}

void OpenGLRenderer::clearCache()
{
}

int OpenGLRenderer::maxTextureDepth()
{
    initialize();

    GLint value = {};
    CHECKED_GL(glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &value));
    return static_cast<int>(value);
}

int OpenGLRenderer::maxTextureSize()
{
    initialize();

    GLint value = {};
    CHECKED_GL(glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value));
    return static_cast<int>(value);
}

void OpenGLRenderer::clearTextureAtlas(GLuint textureId, ImageSize textureSize, atlas::Format format)
{
    bindTexture(textureId);

    auto constexpr target = GL_TEXTURE_2D;
    auto constexpr levelOfDetail = 0;
    // auto constexpr depth = 1;
    auto constexpr type = GL_UNSIGNED_BYTE;
    // auto constexpr x0 = 0;
    // auto constexpr y0 = 0;
    // auto constexpr z0 = 0;

    std::vector<uint8_t> stub;
    // {{{ fill stub
    stub.resize(textureSize.area() * 4 /*RGBA*/);
    auto t = stub.begin();
    switch (format)
    {
    case atlas::Format::Red:
        for (auto i = 0; i < textureSize.area(); ++i)
            *t++ = 0x40;
        break;
    case atlas::Format::RGB:
        for (auto i = 0; i < textureSize.area(); ++i)
        {
            *t++ = 0x00;
            *t++ = 0x00;
            *t++ = 0x80;
        }
        break;
    case atlas::Format::RGBA:
        for (auto i = 0; i < textureSize.area(); ++i)
        {
            *t++ = 0x00;
            *t++ = 0x80;
            *t++ = 0x00;
            *t++ = 0x80;
        }
        break;
    }
    assert(t == stub.end());
    // }}}

    GLenum const glFmt = glFormat(format);
    GLint constexpr UnusedParam = 0;
    CHECKED_GL(glTexImage2D(target,
                            levelOfDetail,
                            glFmt,
                            unbox<int>(textureSize.width),
                            unbox<int>(textureSize.height),
                            UnusedParam,
                            glFmt,
                            type,
                            stub.data()));
}

// {{{ AtlasBackend impl
atlas::AtlasID OpenGLRenderer::createAtlas(atlas::CreateAtlas atlas)
{
    _scheduledExecutions.createAtlases.emplace_back(atlas);
    return atlas::AtlasID {}; // TODO(pr) GL value
}

void OpenGLRenderer::uploadTile(atlas::UploadTile tile)
{
    _scheduledExecutions.uploadTiles.emplace_back(std::move(tile));
}

void OpenGLRenderer::destroyAtlas(atlas::AtlasID _atlas)
{
    _scheduledExecutions.destroyAtlases.push_back(_atlas);
}

void OpenGLRenderer::renderTile(atlas::RenderTile tile)
{
    RenderBatch& batch = _scheduledExecutions.renderBatches[0]; // TODO(pr)

    // atlas texture Vertices to locate the tile
    auto const x = static_cast<GLfloat>(tile.tileLocation.x.value);
    auto const y = static_cast<GLfloat>(tile.tileLocation.y.value);
    auto const z = static_cast<GLfloat>(0);
    // GLfloat const w = tile.w;

    // tile bitmap size on target render surface
    GLfloat const r = unbox<GLfloat>(_tileSize.width); // r/s: target size
    GLfloat const s = unbox<GLfloat>(_tileSize.height);

    // accumulated TexCoords
    // NB: rx/ry could be stored in RenderTile (as part of the LRU-cache entry)
    GLfloat const rx = x / unbox<GLfloat>(_textureAtlasSize.width);
    GLfloat const ry = y / unbox<GLfloat>(_textureAtlasSize.height);
    GLfloat const w = unbox<GLfloat>(_relativeCellSize.width);
    GLfloat const h = unbox<GLfloat>(_relativeCellSize.height);
    GLfloat const i = 0;    // tile.texture.get().z;
    GLfloat const u = 0.0f; // userdata for given texture atlas

    // color
    GLfloat const cr = tile.color[0];
    GLfloat const cg = tile.color[1];
    GLfloat const cb = tile.color[2];
    GLfloat const ca = tile.color[3];

    // buffer contains
    // - 3 vertex coordinates (XYZ)
    // - 4 texture coordinates (XYIU), I is unused currently, U selects which texture to use
    // - 4 color values (RGBA)

    // clang-format off
    GLfloat const vertices[6 * 11] = {
        // first triangle
        // <X      Y      Z> <X       Y       I  U>  <R   G   B   A>
        x,         y + s, z, rx,      ry + h, i, u,  cr, cg, cb, ca, // left top
        x,         y,     z, rx,      ry,     i, u,  cr, cg, cb, ca, // left bottom
        x + r,     y,     z, rx + w,  ry,     i, u,  cr, cg, cb, ca, // right bottom

        // second triangle
        x,         y + s, z, rx,      ry + h, i, u, cr, cg, cb, ca, // left top
        x + r,     y,     z, rx + w,  ry,     i, u, cr, cg, cb, ca, // right bottom
        x + r,     y + s, z, rx + w,  ry + h, i, u, cr, cg, cb, ca, // right top
    };
    // clang-format on

    batch.renderTiles.emplace_back(tile);
    crispy::copy(vertices, back_inserter(batch.buffer));
}
// }}}

// {{{ executor impl
ImageSize OpenGLRenderer::renderBufferSize()
{
#if 0
    return renderTargetSize_;
#else
    auto width = unbox<GLint>(_renderTargetSize.width);
    auto height = unbox<GLint>(_renderTargetSize.height);
    CHECKED_GL(glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width));
    CHECKED_GL(glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &height));
    return ImageSize { Width(width), Height(height) };
#endif
}

void OpenGLRenderer::execute()
{
    // FIXME
    // glEnable(GL_BLEND);
    // glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE);
    // glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    // glBlendFunc(GL_SRC1_COLOR, GL_ONE_MINUS_SRC1_COLOR);

    // render filled rects
    //
    if (!_rectBuffer.empty())
    {
        bound(*_rectShader, [&]() {
            _rectShader->setUniformValue(_rectProjectionLocation, _projectionMatrix);

            glBindVertexArray(_rectVAO);
            glBindBuffer(GL_ARRAY_BUFFER, _rectVBO);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(_rectBuffer.size() * sizeof(GLfloat)),
                         _rectBuffer.data(),
                         GL_STREAM_DRAW);

            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(_rectBuffer.size() / 7));
            glBindVertexArray(0);
        });
        _rectBuffer.clear();
    }

    // render textures
    //
    bound(*_textShader, [&]() {
        // TODO: only upload when it actually DOES change
        _textShader->setUniformValue(_textProjectionLocation, _projectionMatrix);
        executeRenderTextures();
    });

    if (_pendingScreenshotCallback)
    {
        auto result = takeScreenshot();
        _pendingScreenshotCallback.value()(result.second, result.first);
        _pendingScreenshotCallback.reset();
    }
}

void OpenGLRenderer::executeRenderTextures()
{
    _currentTextureId = std::numeric_limits<int>::max();

    // LOGSTORE(DisplayLog)(
    //     "OpenGLRenderer::executeRenderTextures() upload={} render={}",
    //     oldTextureScheduler_->uploadTextures.size(),
    //     oldTextureScheduler_->renderTextures.size()
    // );

    // potentially create new atlases
    for (auto const& params: _scheduledExecutions.createAtlases)
        executeCreateAtlas(params);

    // potentially upload any new textures
    for (auto const& params: _scheduledExecutions.uploadTiles)
        executeUploadTile(params);

    // upload vertices and render
    for (size_t i = 0; i < _scheduledExecutions.renderBatches.size(); ++i)
    {
        RenderBatch& batch = _scheduledExecutions.renderBatches[i];
        if (batch.renderTiles.empty())
            continue;

        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + batch.userdata));
        bindTexture(_textureIds[static_cast<size_t>(i)]);
        glBindVertexArray(_vao);

        // upload buffer
        // clang-format off
        glBindBuffer(GL_ARRAY_BUFFER, _vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizei>(batch.buffer.size() * sizeof(GLfloat)), batch.buffer.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch.renderTiles.size() * 6));
        // clang-format on
    }

    // destroy any pending atlases that were meant to be destroyed
    for (auto const& params: _scheduledExecutions.destroyAtlases)
        executeDestroyAtlas(params);

    _scheduledExecutions.clear();
}

void OpenGLRenderer::executeCreateAtlas(atlas::CreateAtlas const& param)
{
    GLuint textureId {};
    CHECKED_GL(glGenTextures(1, &textureId));
    bindTexture(textureId);

    _textureIds[param.atlas.value] = textureId;

    CHECKED_GL(glTexParameteri(GL_TEXTURE_2D,
                               GL_TEXTURE_MAG_FILTER,
                               GL_NEAREST)); // NEAREST, because LINEAR yields borders at the edges
    CHECKED_GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    CHECKED_GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE));
    CHECKED_GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    CHECKED_GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

    clearTextureAtlas(textureId, param.size, param.format);
}

void OpenGLRenderer::executeUploadTile(atlas::UploadTile const& param)
{
    auto const textureId = _textureIds[param.location.atlasID.value];
    auto const x0 = param.location.x.value;
    auto const y0 = param.location.y.value;
    // auto const z0 = texture.z;

    // LOGSTORE(DisplayLog)("({}): {}", textureId, param);

    auto constexpr target = GL_TEXTURE_2D;
    auto constexpr levelOfDetail = 0;
    // auto constexpr depth = 1;
    auto constexpr type = GL_UNSIGNED_BYTE;
    auto constexpr format = GL_RGBA;
    bindTexture(textureId);

    CHECKED_GL(glPixelStorei(GL_UNPACK_ALIGNMENT, 4));

    CHECKED_GL(glTexSubImage2D(target,
                               levelOfDetail,
                               x0,
                               y0,
                               _tileSize.width.value,
                               _tileSize.height.value,
                               format,
                               type,
                               param.bitmap.data()));
}

GLuint OpenGLRenderer::textureAtlasID(atlas::AtlasID atlasID) const noexcept
{
    return _textureIds[static_cast<size_t>(atlasID.value)];
}

void OpenGLRenderer::executeDestroyAtlas(atlas::AtlasID atlasID)
{
    GLuint const textureId = _textureIds.at(atlasID.value);
    glDeleteTextures(1, &textureId);
}

void OpenGLRenderer::bindTexture(GLuint textureId)
{
    if (_currentTextureId != textureId)
    {
        glBindTexture(GL_TEXTURE_2D, textureId);
        _currentTextureId = textureId;
    }
}

void OpenGLRenderer::renderRectangle(int ix, int iy, Width width, Height height, RGBAColor color)
{
    GLfloat const x = ix;
    GLfloat const y = iy;
    GLfloat const z = 0.0f;
    GLfloat const r = width.value;
    GLfloat const s = height.value;
    GLfloat const cr = color.red() / 255.f;
    GLfloat const cg = color.green() / 255.f;
    GLfloat const cb = color.blue() / 255.f;
    GLfloat const ca = color.alpha() / 255.f;

    // clang-format off
    GLfloat const vertices[6 * 7] = {
        // first triangle
        x,     y + s, z, cr, cg, cb, ca,
        x,     y,     z, cr, cg, cb, ca,
        x + r, y,     z, cr, cg, cb, ca,

        // second triangle
        x,     y + s, z, cr, cg, cb, ca,
        x + r, y,     z, cr, cg, cb, ca,
        x + r, y + s, z, cr, cg, cb, ca
    };
    // clang-format on

    crispy::copy(vertices, back_inserter(_rectBuffer));
}

vector<atlas::AtlasID> OpenGLRenderer::activeAtlasTextures() const
{
    auto result = vector<atlas::AtlasID> {};

    for (size_t i = 0; i < _textureIds.size(); ++i)
        if (_textureIds[i])
            result.push_back(atlas::AtlasID { static_cast<uint32_t>(i) });

    return result;
}

optional<terminal::renderer::AtlasTextureScreenshot> OpenGLRenderer::readAtlas(atlas::AtlasID atlasID)
{
    // NB: to get all atlas pages, call this from instance base id up to and including current
    // instance id of the given allocator.

    auto const textureId = _textureIds[atlasID.value];
    auto const& atlasProperties = _atlasProperties[atlasID.value];

    terminal::renderer::AtlasTextureScreenshot output {};
    output.atlasName = atlasProperties.name;
    output.atlasInstanceId = 0;
    output.size = atlasProperties.imageSize;
    output.format = atlas::Format::RGBA;
    output.buffer.resize(atlasProperties.imageSize.area());

    // Reading texture data to host CPU (including for RGB textures) only works via framebuffers
    auto fbo = GLuint {};
    CHECKED_GL(glGenFramebuffers(1, &fbo));
    CHECKED_GL(glBindFramebuffer(GL_FRAMEBUFFER, fbo));
    CHECKED_GL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureId, 0));
    CHECKED_GL(glReadPixels(
        0, 0, *output.size.width, *output.size.height, GL_RGBA, GL_UNSIGNED_BYTE, output.buffer.data()));
    CHECKED_GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    CHECKED_GL(glDeleteFramebuffers(1, &fbo));

    return { output };
}

void OpenGLRenderer::scheduleScreenshot(ScreenshotCallback callback)
{
    _pendingScreenshotCallback = std::move(callback);
}

pair<ImageSize, vector<uint8_t>> OpenGLRenderer::takeScreenshot()
{
    ImageSize const imageSize = renderBufferSize();

    vector<uint8_t> buffer;
    buffer.resize(*imageSize.width * *imageSize.height * 4);

    LOGSTORE(DisplayLog)("Capture screenshot ({}/{}).", imageSize, _renderTargetSize);

    CHECKED_GL(
        glReadPixels(0, 0, *imageSize.width, *imageSize.height, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data()));

    return { imageSize, buffer };
}

void OpenGLRenderer::clear(terminal::RGBAColor fillColor)
{
    if (fillColor != _renderStateCache.backgroundColor)
    {
        auto const clearColor = normalize(fillColor);
        glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
        _renderStateCache.backgroundColor = fillColor;
    }

    glClear(GL_COLOR_BUFFER_BIT);
}

// }}}

} // namespace contour::opengl
