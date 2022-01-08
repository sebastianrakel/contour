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
using terminal::Width;

using atlas = terminal::renderer::atlas;

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

constexpr int MaxMonochromeTextureSize = 1024;
constexpr int MaxColorTextureSize = 2048;
constexpr int MaxInstanceCount = 24;

/**
 * Text rendering input:
 *  - vec3 screenCoord    (x/y/z)
 *  - vec4 textureCoord   (x/y and w/h)
 *  - vec4 textColor      (r/g/b/a)
 *
 */
struct OpenGLRenderer::TextureScheduler: public terminal::renderer::atlas::AtlasBackend
{
    struct RenderBatch
    {
        std::vector<atlas::RenderTile> renderTiles;
        std::vector<GLfloat> buffer;
        uint32_t userdata = 0;

        void clear()
        {
            renderTiles.clear();
            buffer.clear();
        }
    };

    // input properties
    ImageSize textureSize {};
    ImageSize cellSize {};         // used to span the tile over this extent
    ImageSize relativeCellSize {}; // relative cell size with respect to the atlas size

    // work state
    std::vector<atlas::CreateAtlas> createAtlases;
    std::vector<atlas::AtlasID> destroyAtlases;
    std::vector<atlas::UploadTile> uploadTiles;
    std::vector<RenderBatch> renderBatches;

    atlas::AtlasID createAtlas(atlas::CreateAtlas atlas) override
    {
        createAtlases.emplace_back(atlas);
        return atlas::AtlasID {}; // TODO(pr) GL value
    }

    void uploadTile(atlas::UploadTile tile) override { uploadTiles.emplace_back(std::move(tile)); }

    void destroyAtlas(atlas::AtlasID _atlas) override { destroyAtlases.push_back(_atlas); }

    uint32_t userdataForAtlas(atlas::AtlasID atlas) const noexcept
    {
        return 0; // TODO(pr)
    }

    void renderTile(atlas::RenderTile tile) override
    {
        RenderBatch& batch = renderBatches[0]; // TODO(pr)

        // Vertices
        auto const x = static_cast<GLfloat>(tile.location.x.value);
        auto const y = static_cast<GLfloat>(tile.location.y.value);
        auto const z = static_cast<GLfloat>(0);
        // GLfloat const w = tile.w;
        GLfloat const r = *tile.texture.get().targetSize.width;
        GLfloat const s = *tile.texture.get().targetSize.height;

        // TexCoords
        GLfloat const rx = tile.texture.get().relativeX;
        GLfloat const ry = tile.texture.get().relativeY;
        GLfloat const w = unbox<float>(relativeCellSize.width);
        GLfloat const h = unbox<float>(relativeCellSize.height);
        GLfloat const i = 0; // tile.texture.get().z;
        GLfloat const u = userdataForAtlas(tile.location.atlasID];

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
};

struct OpenGLRenderer::OldTextureScheduler: public atlas::AtlasBackend
{
    struct RenderBatch
    {
        std::vector<atlas::RenderTexture> renderTextures;
        std::vector<GLfloat> buffer;
        int user = 0;

        void clear()
        {
            renderTextures.clear();
            buffer.clear();
        }
    };

    std::vector<atlas::CreateAtlas> createAtlases;
    std::vector<atlas::UploadTexture> uploadTextures;
    std::vector<RenderBatch> renderBatches;
    std::vector<atlas::AtlasID> destroyAtlases;

    std::list<int> atlasIDs_;
    std::list<int> unusedAtlasIDs_;
    int nextAtlasID_ = 0;

    atlas::AtlasID allocateAtlasID(int _user)
    {
        // The allocated atlas ID is not the OpenGL texture ID but
        // an internal one that is used to reference into renderBatches, but
        // can be used to map to OpenGL texture IDs.

        auto const id = atlas::AtlasID { nextAtlasID_++ };
        atlasIDs_.push_back(id.value);
        if (renderBatches.size() <= static_cast<size_t>(id.value))
            renderBatches.resize(id.value + 10);

        renderBatches[id.value].user = _user;

        return id;
    }

    atlas::AtlasID createAtlas(ImageSize _size, atlas::Format _format, int _user) override
    {
        auto const id = allocateAtlasID(_user);
        createAtlases.emplace_back(atlas::CreateAtlas { id, _size, _format, _user });
        return id;
    }

    void uploadTexture(atlas::UploadTexture _texture) override
    {
        uploadTextures.emplace_back(std::move(_texture));
    }

    void renderTexture(atlas::RenderTexture _render) override
    {
        // This is factored out of renderTexture() to make sure it's not writing to anything else
        addRenderTextureToBatch(_render, renderBatches.at(_render.texture.get().atlas.value));
    }

    static void addRenderTextureToBatch(atlas::RenderTexture _render, RenderBatch& _batch)
    {
        // Vertices
        GLfloat const x = _render.x;
        GLfloat const y = _render.y;
        GLfloat const z = _render.z;
        // GLfloat const w = _render.w;
        GLfloat const r = *_render.texture.get().targetSize.width;
        GLfloat const s = *_render.texture.get().targetSize.height;

        // TexCoords
        GLfloat const rx = _render.texture.get().relativeX;
        GLfloat const ry = _render.texture.get().relativeY;
        GLfloat const w = _render.texture.get().relativeWidth;
        GLfloat const h = _render.texture.get().relativeHeight;
        GLfloat const i = 0; // _render.texture.get().z;
        GLfloat const u = _render.texture.get().user;

        // color
        GLfloat const cr = _render.color[0];
        GLfloat const cg = _render.color[1];
        GLfloat const cb = _render.color[2];
        GLfloat const ca = _render.color[3];

        // clang-format off
        GLfloat const vertices[6 * 11] = {
        // <X      Y      Z> <X       Y       I  U>  <R   G   B   A>
            // first triangle
            x,     y + s, z, rx,     ry + h,  i, u, cr, cg, cb, ca, // left top
            x,     y,     z, rx,     ry,      i, u, cr, cg, cb, ca, // left bottom
            x + r, y,     z, rx + w, ry,      i, u, cr, cg, cb, ca, // right bottom

            // second triangle
            x,     y + s, z, rx,     ry + h, i, u, cr, cg, cb, ca, // left top
            x + r, y,     z, rx + w, ry,     i, u, cr, cg, cb, ca, // right bottom
            x + r, y + s, z, rx + w, ry + h, i, u, cr, cg, cb, ca, // right top
        };
        // buffer contains
        // - 3 vertex coordinates (XYZ)
        // - 4 texture coordinates (XYIU), I is unused currently, U selects which texture to use
        // - 4 color values (RGBA)
        // clang-format on

        _batch.renderTextures.emplace_back(_render);
        crispy::copy(vertices, back_inserter(_batch.buffer));
    }

    void destroyAtlas(atlas::AtlasID _atlas) override { destroyAtlases.push_back(_atlas); }
};

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

OpenGLRenderer::OpenGLRenderer(ShaderConfig const& _textShaderConfig,
                               ShaderConfig const& _rectShaderConfig,
                               ImageSize _renderSize,
                               ImageSize _textureAtlasSize,
                               ImageSize _tileSize,
                               terminal::renderer::PageMargin _margin):
    renderTargetSize_ { _renderSize },
    projectionMatrix_ { ortho(0.0f,
                              float(*_renderSize.width), // left, right
                              0.0f,
                              float(*_renderSize.height) // bottom, top
                              ) },
    margin_ { _margin },
    textShader_ { createShader(_textShaderConfig) },
    textProjectionLocation_ { textShader_->uniformLocation("vs_projection") },
    // texture
    textureAtlas_ { *textureScheduler_,
                    terminal::renderer::atlas::Atlas {
                        _textureAtlasSize,
                        _tileSize,
                        "textureAtlas",
                        terminal::renderer::atlas::Format::RGBA,
                        0, // reserved tile count
                        0  // userdata
                    } },
    // rect
    rectShader_ { createShader(_rectShaderConfig) },
    rectProjectionLocation_ { rectShader_->uniformLocation("u_projection") }
{
    initialize();

    setRenderSize(_renderSize);

    assert(textProjectionLocation_ != -1);

    CHECKED_GL(glEnable(GL_BLEND));
    CHECKED_GL(glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE));
    // glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    //  //glBlendFunc(GL_SRC1_COLOR, GL_ONE_MINUS_SRC1_COLOR);

    bound(*textShader_, [&]() {
        CHECKED_GL(textShader_->setUniformValue("fs_monochromeTextures", monochromeAtlasAllocator_.user()));
        CHECKED_GL(textShader_->setUniformValue("fs_colorTextures", coloredAtlasAllocator_.user()));
        CHECKED_GL(textShader_->setUniformValue("fs_lcdTexture", lcdAtlasAllocator_.user()));
        CHECKED_GL(textShader_->setUniformValue("pixel_x", 1.0f / float(*lcdAtlasAllocator_.size().width)));
    });

    initializeRectRendering();
    initializeTextureRendering();
}

crispy::ImageSize OpenGLRenderer::colorTextureSizeHint()
{
    return ImageSize { Width(min(MaxColorTextureSize, maxTextureSize())),
                       Height(min(MaxColorTextureSize, maxTextureSize())) };
}

crispy::ImageSize OpenGLRenderer::monochromeTextureSizeHint()
{
    return ImageSize { Width(min(MaxMonochromeTextureSize, maxTextureSize())),
                       Height(min(MaxMonochromeTextureSize, maxTextureSize())) };
}

void OpenGLRenderer::setRenderSize(ImageSize _renderSize)
{
    renderTargetSize_ = _renderSize;
    projectionMatrix_ = ortho(0.0f,
                              float(*renderTargetSize_.width), // left, right
                              0.0f,
                              float(*renderTargetSize_.height) // bottom, top
    );
}

void OpenGLRenderer::setMargin(terminal::renderer::PageMargin _margin) noexcept
{
    margin_ = _margin;
}

atlas::TextureAtlasAllocator& OpenGLRenderer::monochromeAtlasAllocator() noexcept
{
    return monochromeAtlasAllocator_;
}

atlas::TextureAtlasAllocator& OpenGLRenderer::coloredAtlasAllocator() noexcept
{
    return coloredAtlasAllocator_;
}

atlas::TextureAtlasAllocator& OpenGLRenderer::lcdAtlasAllocator() noexcept
{
    return lcdAtlasAllocator_;
}

atlas::AtlasBackend& OpenGLRenderer::textureScheduler()
{
    return *oldTextureScheduler_;
}

void OpenGLRenderer::initializeRectRendering()
{
    CHECKED_GL(glGenVertexArrays(1, &rectVAO_));
    CHECKED_GL(glBindVertexArray(rectVAO_));

    CHECKED_GL(glGenBuffers(1, &rectVBO_));
    CHECKED_GL(glBindBuffer(GL_ARRAY_BUFFER, rectVBO_));
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
    CHECKED_GL(glGenVertexArrays(1, &vao_));
    CHECKED_GL(glBindVertexArray(vao_));

    auto constexpr BufferStride = (3 + 4 + 4) * sizeof(GLfloat);
    auto constexpr VertexOffset = (void const*) 0;
    auto const TexCoordOffset = (void const*) (3 * sizeof(GLfloat));
    auto const ColorOffset = (void const*) (7 * sizeof(GLfloat));

    CHECKED_GL(glGenBuffers(1, &vbo_));
    CHECKED_GL(glBindBuffer(GL_ARRAY_BUFFER, vbo_));
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
    // glGenBuffers(1, &ebo_);
    // glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    // static const GLuint indices[6] = { 0, 1, 3, 1, 2, 3 };
    // glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // glVertexAttribDivisor(0, 1); // TODO: later for instanced rendering
}

OpenGLRenderer::~OpenGLRenderer()
{
    LOGSTORE(DisplayLog)("~OpenGLRenderer");
    CHECKED_GL(glDeleteVertexArrays(1, &rectVAO_));
    CHECKED_GL(glDeleteBuffers(1, &rectVBO_));
}

void OpenGLRenderer::initialize()
{
    if (!initialized_)
    {
        initialized_ = true;
        initializeOpenGLFunctions();
    }
}

void OpenGLRenderer::clearCache()
{
    monochromeAtlasAllocator_.clear();
    coloredAtlasAllocator_.clear();
    lcdAtlasAllocator_.clear();
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

void OpenGLRenderer::clearTextureAtlas(GLuint _textureId, int _width, int _height, atlas::Format _format)
{
    bindTexture(_textureId);

    auto constexpr target = GL_TEXTURE_2D;
    auto constexpr levelOfDetail = 0;
    // auto constexpr depth = 1;
    auto constexpr type = GL_UNSIGNED_BYTE;
    // auto constexpr x0 = 0;
    // auto constexpr y0 = 0;
    // auto constexpr z0 = 0;

    std::vector<uint8_t> stub;
    stub.resize(_width * _height * atlas::element_count(_format)); // {{{ fill stub
    auto t = stub.begin();
    switch (_format)
    {
    case atlas::Format::Red:
        for (auto i = 0; i < _width * _height; ++i)
            *t++ = 0x40;
        break;
    case atlas::Format::RGB:
        for (auto i = 0; i < _width * _height; ++i)
        {
            *t++ = 0x00;
            *t++ = 0x00;
            *t++ = 0x80;
        }
        break;
    case atlas::Format::RGBA:
        for (auto i = 0; i < _width * _height; ++i)
        {
            *t++ = 0x00;
            *t++ = 0x80;
            *t++ = 0x00;
            *t++ = 0x80;
        }
        break;
    }
    assert(t == stub.end()); // }}}

    GLenum const glFmt = glFormat(_format);
    GLint constexpr UnusedParam = 0;
    CHECKED_GL(
        glTexImage2D(target, levelOfDetail, glFmt, _width, _height, UnusedParam, glFmt, type, stub.data()));
}

void OpenGLRenderer::createAtlas(atlas::CreateAtlas const& _param)
{
    GLuint textureId {};
    CHECKED_GL(glGenTextures(1, &textureId));
    bindTexture(textureId);

    CHECKED_GL(glTexParameteri(GL_TEXTURE_2D,
                               GL_TEXTURE_MAG_FILTER,
                               GL_NEAREST)); // NEAREST, because LINEAR yields borders at the edges
    CHECKED_GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    CHECKED_GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE));
    CHECKED_GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    CHECKED_GL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

    clearTextureAtlas(textureId, *_param.size.width, *_param.size.height, _param.format);

    atlasMap_.insert(pair { _param.atlas, textureId });
}

void OpenGLRenderer::uploadTexture(atlas::UploadTexture const& _param)
{
    auto const& texture = _param.texture.get();
    auto const key = texture.atlas;
    [[maybe_unused]] auto const textureIdIter = atlasMap_.find(key);
    assert(textureIdIter != atlasMap_.end() && "Texture ID not found in atlas map!");
    auto const textureId = atlasMap_.at(key);
    auto const x0 = texture.offset.x;
    auto const y0 = texture.offset.y;
    // auto const z0 = texture.z;

    // LOGSTORE(DisplayLog)("({}): {}", textureId, _param);

    auto constexpr target = GL_TEXTURE_2D;
    auto constexpr levelOfDetail = 0;
    // auto constexpr depth = 1;
    auto constexpr type = GL_UNSIGNED_BYTE;

    bindTexture(textureId);

    switch (_param.format)
    {
    case atlas::Format::RGB:
    case atlas::Format::Red: CHECKED_GL(glPixelStorei(GL_UNPACK_ALIGNMENT, 1)); break;
    case atlas::Format::RGBA: CHECKED_GL(glPixelStorei(GL_UNPACK_ALIGNMENT, 4)); break;
    }

    CHECKED_GL(glTexSubImage2D(target,
                               levelOfDetail,
                               x0,
                               y0,
                               *texture.bitmapSize.width,
                               *texture.bitmapSize.height,
                               glFormat(_param.format),
                               type,
                               _param.data.data()));
}

GLuint OpenGLRenderer::textureAtlasID(atlas::AtlasID _atlasID) const noexcept
{
    auto const it = atlasMap_.find(_atlasID);
    if (it == atlasMap_.end())
        return 0;

    return it->second;
}

void OpenGLRenderer::destroyAtlas(atlas::AtlasID _atlasID)
{
    if (auto const it = atlasMap_.find(_atlasID); it != atlasMap_.end())
    {
        GLuint const textureId = it->second;
        atlasMap_.erase(it);
        glDeleteTextures(1, &textureId);
    }
}

void OpenGLRenderer::bindTexture(GLuint _textureId)
{
    if (currentTextureId_ != _textureId)
    {
        glBindTexture(GL_TEXTURE_2D, _textureId);
        currentTextureId_ = _textureId;
    }
}

void OpenGLRenderer::renderRectangle(
    int _x, int _y, int _width, int _height, float _r, float _g, float _b, float _a)
{
    GLfloat const x = _x;
    GLfloat const y = _y;
    GLfloat const z = 0.0f;
    GLfloat const r = _width;
    GLfloat const s = _height;
    GLfloat const cr = _r;
    GLfloat const cg = _g;
    GLfloat const cb = _b;
    GLfloat const ca = _a;

    GLfloat const vertices[6 * 7] = { // first triangle
                                      x,
                                      y + s,
                                      z,
                                      cr,
                                      cg,
                                      cb,
                                      ca,
                                      x,
                                      y,
                                      z,
                                      cr,
                                      cg,
                                      cb,
                                      ca,
                                      x + r,
                                      y,
                                      z,
                                      cr,
                                      cg,
                                      cb,
                                      ca,

                                      // second triangle
                                      x,
                                      y + s,
                                      z,
                                      cr,
                                      cg,
                                      cb,
                                      ca,
                                      x + r,
                                      y,
                                      z,
                                      cr,
                                      cg,
                                      cb,
                                      ca,
                                      x + r,
                                      y + s,
                                      z,
                                      cr,
                                      cg,
                                      cb,
                                      ca
    };

    crispy::copy(vertices, back_inserter(rectBuffer_));
}

std::optional<terminal::renderer::AtlasTextureScreenshot> OpenGLRenderer::readAtlas()
{
    // NB: to get all atlas pages, call this from instance base id up to and including current
    // instance id of the given allocator.

    auto const textureId = textureAtlasID(_instanceID);

    terminal::renderer::AtlasTextureScreenshot output {};
    output.atlasName = _allocator.name();
    output.atlasInstanceId = _instanceID.value;
    output.size = _allocator.size();
    output.format = atlas::Format::RGBA;
    output.buffer.resize(*_allocator.size().width * *_allocator.size().height * 4);

    // Reading texture data to host CPU (including for RGB textures) only works via framebuffers
    GLuint fbo;
    CHECKED_GL(glGenFramebuffers(1, &fbo));
    CHECKED_GL(glBindFramebuffer(GL_FRAMEBUFFER, fbo));
    CHECKED_GL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureId, 0));
    CHECKED_GL(glReadPixels(
        0, 0, *output.size.width, *output.size.height, GL_RGBA, GL_UNSIGNED_BYTE, output.buffer.data()));
    CHECKED_GL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
    CHECKED_GL(glDeleteFramebuffers(1, &fbo));

    return output;
}

void OpenGLRenderer::scheduleScreenshot(ScreenshotCallback _callback)
{
    pendingScreenshotCallback_ = std::move(_callback);
}

pair<ImageSize, vector<uint8_t>> OpenGLRenderer::takeScreenshot()
{
    ImageSize const imageSize = renderBufferSize();

    vector<uint8_t> buffer;
    buffer.resize(*imageSize.width * *imageSize.height * 4);

    LOGSTORE(DisplayLog)("Capture screenshot ({}/{}).", imageSize, renderTargetSize_);

    CHECKED_GL(
        glReadPixels(0, 0, *imageSize.width, *imageSize.height, GL_RGBA, GL_UNSIGNED_BYTE, buffer.data()));

    return { imageSize, buffer };
}

void OpenGLRenderer::clear(terminal::RGBAColor _fillColor)
{
    if (_fillColor != renderStateCache_.backgroundColor)
    {
        auto const clearColor = array<float, 4> { float(_fillColor.red()) / 255.0f,
                                                  float(_fillColor.green()) / 255.0f,
                                                  float(_fillColor.blue()) / 255.0f,
                                                  float(_fillColor.alpha()) / 255.0f };
        glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
        renderStateCache_.backgroundColor = _fillColor;
    }

    glClear(GL_COLOR_BUFFER_BIT);
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
    if (!rectBuffer_.empty())
    {
        bound(*rectShader_, [&]() {
            rectShader_->setUniformValue(rectProjectionLocation_, projectionMatrix_);

            glBindVertexArray(rectVAO_);
            glBindBuffer(GL_ARRAY_BUFFER, rectVBO_);
            glBufferData(
                GL_ARRAY_BUFFER, rectBuffer_.size() * sizeof(GLfloat), rectBuffer_.data(), GL_STREAM_DRAW);

            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(rectBuffer_.size() / 7));
            glBindVertexArray(0);
        });
        rectBuffer_.clear();
    }

    // render textures
    //
    bound(*textShader_, [&]() {
        // TODO: only upload when it actually DOES change
        textShader_->setUniformValue(textProjectionLocation_, projectionMatrix_);
        executeRenderTextures();
    });

    if (pendingScreenshotCallback_)
    {
        auto result = takeScreenshot();
        pendingScreenshotCallback_.value()(result.second, result.first);
        pendingScreenshotCallback_.reset();
    }
}

ImageSize OpenGLRenderer::renderBufferSize()
{
#if 0
    return renderTargetSize_;
#else
    auto width = unbox<GLint>(renderTargetSize_.width);
    auto height = unbox<GLint>(renderTargetSize_.height);
    CHECKED_GL(glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width));
    CHECKED_GL(glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &height));
    return ImageSize { Width(width), Height(height) };
#endif
}

void OpenGLRenderer::executeRenderTextures()
{
    currentTextureId_ = std::numeric_limits<int>::max();

    // LOGSTORE(DisplayLog)(
    //     "OpenGLRenderer::executeRenderTextures() upload={} render={}",
    //     oldTextureScheduler_->uploadTextures.size(),
    //     oldTextureScheduler_->renderTextures.size()
    // );

    // potentially create new atlases
    for (auto const& params: oldTextureScheduler_->createAtlases)
        createAtlas(params);
    oldTextureScheduler_->createAtlases.clear();

    // potentially upload any new textures
    for (auto const& params: oldTextureScheduler_->uploadTextures)
        uploadTexture(params);
    oldTextureScheduler_->uploadTextures.clear();

    // upload vertices and render
    for (size_t i = 0; i < oldTextureScheduler_->renderBatches.size(); ++i)
    {
        auto& batch = oldTextureScheduler_->renderBatches[i];
        if (batch.renderTextures.empty())
            continue;

        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + batch.user));
        bindTexture(textureAtlasID(atlas::AtlasID { static_cast<int>(i) }));
        glBindVertexArray(vao_);

        // upload buffer
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(
            GL_ARRAY_BUFFER, batch.buffer.size() * sizeof(GLfloat), batch.buffer.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch.renderTextures.size() * 6));

        batch.clear();
    }

    // destroy any pending atlases that were meant to be destroyed
    for (auto const& params: oldTextureScheduler_->destroyAtlases)
        destroyAtlas(params);
    oldTextureScheduler_->destroyAtlases.clear();
}

} // namespace contour::opengl
