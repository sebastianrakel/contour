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
#pragma once

#include <terminal_renderer/RenderTarget.h>
#include <terminal_renderer/TextureAtlas.h>

#include <crispy/StrongHash.h>
#include <crispy/StrongLRUHashtable.h>

#include <QtGui/QMatrix4x4>
#include <QtGui/QOpenGLExtraFunctions>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    #include <QtOpenGL/QOpenGLShaderProgram>
#else
    #include <QtGui/QOpenGLShaderProgram>
#endif

#include <memory>
#include <optional>
#include <unordered_map>

namespace contour::opengl
{

struct ShaderConfig;

class OpenGLRenderer final:
    public terminal::renderer::RenderTarget,
    public terminal::renderer::atlas::AtlasBackend,
    public QOpenGLExtraFunctions
{
    using AtlasTextureScreenshot = terminal::renderer::AtlasTextureScreenshot;

    using AtlasID = terminal::renderer::atlas::AtlasID;
    using AtlasTileID = terminal::renderer::atlas::AtlasTileID;

    using CreateAtlas = terminal::renderer::atlas::CreateAtlas;
    using UploadTile = terminal::renderer::atlas::UploadTile;
    using RenderTile = terminal::renderer::atlas::RenderTile;
    using DestroyAtlas = terminal::renderer::atlas::DestroyAtlas;

    AtlasID allocateAtlasID(int userdata);

  public:
    /**
     * @param _renderSize       Sets the render target's size in pixels.
     *                          This is the size that can be rendered to.
     * @param _textureAtlasSize size in pixels for the texture atlas. Must be power of two.
     * @param _tileSize         size in pixels for each tile. This should be the grid cell size.
     */
    OpenGLRenderer(ShaderConfig const& _textShaderConfig,
                   ShaderConfig const& _rectShaderConfig,
                   crispy::ImageSize _renderSize,
                   terminal::renderer::PageMargin _margin);

    ~OpenGLRenderer() override;

    // AtlasBackend implementation
    AtlasID createAtlas(CreateAtlas atlas) override;
    void uploadTile(UploadTile tile) override;
    void renderTile(RenderTile tile) override;
    void destroyAtlas(AtlasID atlasID) override;

    // RenderTarget implementation
    void setRenderSize(crispy::ImageSize _size) override;
    void setMargin(terminal::renderer::PageMargin _margin) noexcept override;
    std::vector<AtlasID> activeAtlasTextures() const override;
    std::optional<AtlasTextureScreenshot> readAtlas(AtlasID id) override;
    AtlasBackend& textureScheduler() override;
    void scheduleScreenshot(ScreenshotCallback _callback) override;
    void renderRectangle(int x, int y, Width, Height, RGBAColor color) override;
    void clear(terminal::RGBAColor _fillColor) override;
    void execute() override;

    std::pair<crispy::ImageSize, std::vector<uint8_t>> takeScreenshot();

    void clearCache() override;

  private:
    // private helper methods
    //
    void initialize();
    void initializeTextureRendering();
    void initializeRectRendering();
    int maxTextureDepth();
    int maxTextureSize();
    int maxTextureUnits();
    crispy::ImageSize renderBufferSize();

    crispy::ImageSize colorTextureSizeHint() noexcept;

    void executeRenderTextures();
    void executeCreateAtlas(CreateAtlas const& _param);
    void executeUploadTile(UploadTile const& _param);
    void executeRenderTile(RenderTile const& _param);
    void executeDestroyAtlas(AtlasID _atlasID);

    //? void renderRectangle(int _x, int _y, int _width, int _height, QVector4D const& _color);

    void bindTexture(GLuint _textureId);
    GLuint textureAtlasID(terminal::renderer::atlas::AtlasID _atlasID) const noexcept;
    void clearTextureAtlas(GLuint textureId,
                           terminal::ImageSize textureSize,
                           terminal::renderer::atlas::Format format);

    // -------------------------------------------------------------------------------------------
    // private data members
    //

    // {{{ scheduling data
    struct RenderBatch
    {
        std::vector<terminal::renderer::atlas::RenderTile> renderTiles;
        std::vector<GLfloat> buffer;
        uint32_t userdata = 0;

        void clear()
        {
            renderTiles.clear();
            buffer.clear();
        }
    };

    struct Scheduler
    {
        std::vector<terminal::renderer::atlas::CreateAtlas> createAtlases;
        std::vector<terminal::renderer::atlas::AtlasID> destroyAtlases;
        std::vector<terminal::renderer::atlas::UploadTile> uploadTiles;
        std::vector<RenderBatch> renderBatches;

        void clear()
        {
            createAtlases.clear();
            uploadTiles.clear();
            for (RenderBatch& batch: renderBatches)
                batch.clear();
            destroyAtlases.clear();
        }
    };

    Scheduler _scheduledExecutions;
    // }}}

    bool _initialized = false;
    crispy::ImageSize _renderTargetSize;
    QMatrix4x4 _projectionMatrix;

    terminal::renderer::PageMargin _margin {};
    terminal::ImageSize _textureAtlasSize {};
    terminal::ImageSize _tileSize {};
    terminal::ImageSize _relativeCellSize {}; // := _tileSize / _textureAtlasSize;

    std::unique_ptr<QOpenGLShaderProgram> _textShader;
    int _textProjectionLocation;

    // private data members for rendering textures
    //
    GLuint _vao {}; // Vertex Array Object, covering all buffer objects
    GLuint _vbo {}; // Buffer containing the vertex coordinates
    // TODO: GLuint ebo_{};

    GLuint _currentTextureId = std::numeric_limits<GLuint>::max();
    std::vector<GLuint> _textureIds;
    std::vector<terminal::renderer::atlas::AtlasProperties> _atlasProperties;

    // private data members for rendering filled rectangles
    //
    std::vector<GLfloat> _rectBuffer;
    std::unique_ptr<QOpenGLShaderProgram> _rectShader;
    GLint _rectProjectionLocation;
    GLuint _rectVAO;
    GLuint _rectVBO;

    std::optional<ScreenshotCallback> _pendingScreenshotCallback;

    // render state cache
    struct
    {
        terminal::RGBAColor backgroundColor {};
    } _renderStateCache;
};

} // namespace contour::opengl
