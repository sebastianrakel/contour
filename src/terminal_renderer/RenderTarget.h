/**
 * This file is part of the "libterminal" project
 *   Copyright (c) 2019-2020 Christian Parpart <christian@parpart.family>
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

#include <terminal/Color.h>
#include <terminal/Grid.h> // cell attribs
#include <terminal/primitives.h>

#include <terminal_renderer/GridMetrics.h>
#include <terminal_renderer/TextureAtlas.h>

#include <crispy/size.h>
#include <crispy/stdfs.h>

#include <unicode/utf8.h>

#include <array>
#include <memory>
#include <optional>
#include <vector>

namespace terminal::renderer
{

/**
 * Contains the read-out of the state of an texture atlas.
 */
struct AtlasTextureScreenshot
{
    std::string atlasName;
    int atlasInstanceId;
    ImageSize size;
    atlas::Format format;
    atlas::Buffer buffer;
};

/**
 * Defines the attributes of a RenderTile, such as render-offset relative
 * to the render target position.
 *
 * For example the later M may be close to the origin (0,0) (bottom left)
 * and have the extent close to the top right of the grid cell size,
 * whereas the `-` symbol may be offset to the vertical middle and have a
 * vertical extent of just a few pixels.
 *
 * This information is usually font specific and produced by (for example)
 * the text shaping engine and/or the glyph rasterizer.
 *
 * For image fragments x/y will most likely be (0, 0) and
 * width/height span the full grid cell.
 */
struct RenderTileAttributes
{
    // clang-format off
    struct X { int value; };
    struct Y { int value; };
    // clang-format on

    X x {};
    Y y {};
    ImageSize bitmapSize {}; // bitmap size inside the tile (must not be larger than the atlas tile).
};

/**
 * Terminal render target interface, for example OpenGL, DirectX, or software-rasterization.
 *
 * @see OpenGLRenderer
 */
class RenderTarget
{
  public:
    using RGBAColor = terminal::RGBAColor;
    using Width = crispy::Width;
    using Height = crispy::Height;
    using TextureAtlas = terminal::renderer::atlas::TextureAtlas<RenderTileAttributes>;

    virtual ~RenderTarget() = default;

    virtual void setRenderSize(ImageSize _size) = 0;
    virtual void setMargin(PageMargin _margin) = 0;

    virtual TextureAtlas& textureAtlas() = 0;

    virtual atlas::AtlasBackend& textureScheduler() = 0;

    /// Fills a rectangular area with the given solid color.
    virtual void renderRectangle(int x, int y, Width, Height, RGBAColor color) = 0;

    using ScreenshotCallback =
        std::function<void(std::vector<uint8_t> const& /*_rgbaBuffer*/, ImageSize /*_pixelSize*/)>;

    /// Schedules taking a screenshot of the current scene and forwards it to the given callback.
    virtual void scheduleScreenshot(ScreenshotCallback _callback) = 0;

    /// Clears the target surface with the given fill color.
    virtual void clear(terminal::RGBAColor _fillColor) = 0;

    /// Executes all previously scheduled render commands.
    virtual void execute() = 0;

    /// Clears any existing caches.
    virtual void clearCache() = 0;

    /// Reads out the given texture atlas.
    virtual std::vector<terminal::renderer::AtlasTextureScreenshot> readAtlas() = 0;
};

/**
 * Helper-base class for render subsystems, such as
 * text renderer, decoration renderer, image fragment renderer, etc.
 */
class Renderable
{
  public:
    using TextureAtlas = RenderTarget::TextureAtlas;

    virtual ~Renderable() = default;

    virtual void clearCache() {}

    virtual void setRenderTarget(RenderTarget& _renderTarget)
    {
        renderTarget_ = &_renderTarget;
        textureAtlas_ = &_renderTarget.textureAtlas();
    }

    constexpr bool renderTargetAvailable() const noexcept { return renderTarget_; }
    RenderTarget& renderTarget() { return *renderTarget_; }
    TextureAtlas& textureAtlas() noexcept { return *textureAtlas_; }

    atlas::AtlasBackend& textureScheduler() { return renderTarget_->textureScheduler(); }

  protected:
    RenderTarget* renderTarget_ = nullptr;
    TextureAtlas* textureAtlas_ = nullptr;
};

} // namespace terminal::renderer
