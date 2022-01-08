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

class OpenGLRenderer final: public terminal::renderer::RenderTarget, public QOpenGLExtraFunctions
{
  private:
    struct TextureScheduler;

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
                   crispy::ImageSize _textureAtlasSize,
                   crispy::ImageSize _tileSize,
                   terminal::renderer::PageMargin _margin);

    ~OpenGLRenderer() override;

    // Sets the render target's size in pixels.
    // This is the size that can be rendered to.
    void setRenderSize(crispy::ImageSize _size) override;

    void setMargin(terminal::renderer::PageMargin _margin) noexcept override;

    TextureAtlas& textureAtlas() override { return textureAtlas_; }

    std::vector<terminal::renderer::AtlasTextureScreenshot> readAtlas() override;

    terminal::renderer::atlas::AtlasBackend& textureScheduler() override;

    void scheduleScreenshot(ScreenshotCallback _callback) override;
    std::pair<crispy::ImageSize, std::vector<uint8_t>> takeScreenshot();

    void renderRectangle(int x, int y, Width, Height, RGBAColor color) override;

    void clear(terminal::RGBAColor _fillColor) override;
    void execute() override;

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

    crispy::ImageSize colorTextureSizeHint();
    crispy::ImageSize monochromeTextureSizeHint();

    void executeRenderTextures();
    void createAtlas(terminal::renderer::atlas::CreateAtlas const& _param);
    void uploadTexture(terminal::renderer::atlas::UploadTile const& _param);
    void renderTexture(terminal::renderer::atlas::RenderTile const& _param);
    void destroyAtlas(terminal::renderer::atlas::AtlasID _atlasID);

    void executeRenderRectangle(int _x, int _y, int _width, int _height, QVector4D const& _color);

    void bindTexture(GLuint _textureId);
    GLuint textureAtlasID(terminal::renderer::atlas::AtlasID _atlasID) const noexcept;
    void clearTextureAtlas(GLuint _textureId,
                           int _width,
                           int _height,
                           terminal::renderer::atlas::Format _format);

    // -------------------------------------------------------------------------------------------
    // private data members
    //
    bool initialized_ = false;
    crispy::ImageSize renderTargetSize_;
    QMatrix4x4 projectionMatrix_;

    terminal::renderer::PageMargin margin_ {};

    std::unique_ptr<QOpenGLShaderProgram> textShader_;
    int textProjectionLocation_;

    // private data members for rendering textures
    //
    GLuint vao_ {}; // Vertex Array Object, covering all buffer objects
    GLuint vbo_ {}; // Buffer containing the vertex coordinates
    // TODO: GLuint ebo_{};
    std::unordered_map<terminal::renderer::atlas::AtlasID, GLuint> atlasMap_; // maps atlas IDs to texture IDs
    GLuint currentTextureId_ = std::numeric_limits<GLuint>::max();
    std::unique_ptr<TextureScheduler> textureScheduler_;

    // private data members for rendering filled rectangles
    //
    std::vector<GLfloat> rectBuffer_;
    std::unique_ptr<QOpenGLShaderProgram> rectShader_;
    GLint rectProjectionLocation_;
    GLuint rectVAO_;
    GLuint rectVBO_;

    std::optional<ScreenshotCallback> pendingScreenshotCallback_;

    // render state cache
    struct
    {
        terminal::RGBAColor backgroundColor {};
    } renderStateCache_;
};

} // namespace contour::opengl
