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
#include <terminal/RenderBuffer.h>
#include <terminal/Screen.h>

// TODO(pr) #include <terminal_renderer/BoxDrawingRenderer.h>
#include <terminal_renderer/FontDescriptions.h>
#include <terminal_renderer/RenderTarget.h>
#include <terminal_renderer/TextureAtlas.h>

#include <text_shaper/font.h>
#include <text_shaper/shaper.h>

#include <crispy/FNV.h>
#include <crispy/LRUCache.h>
#include <crispy/point.h>
#include <crispy/size.h>

#include <unicode/convert.h>
#include <unicode/run_segmenter.h>

#include <gsl/span>
#include <gsl/span_ext>

#include <functional>
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

namespace terminal::renderer
{

struct GridMetrics;

std::unique_ptr<text::font_locator> createFontLocator(FontLocatorEngine _engine);

struct FontKeys
{
    text::font_key regular;
    text::font_key bold;
    text::font_key italic;
    text::font_key boldItalic;
    text::font_key emoji;
};

// XXX CAN this is replaced by generic RenderTileAttributes
struct RasterizedGlyphMetrics
{
    ImageSize bitmapSize;  // glyph size in pixels
    crispy::Point bearing; // offset baseline and left to top and left of the glyph's bitmap
};

/// Text Rendering Pipeline
class TextRenderer: public Renderable
{
  public:
    TextRenderer(TextureAtlas& _atlasManager,
                 GridMetrics const& _gridMetrics,
                 text::shaper& _textShaper,
                 FontDescriptions& _fontDescriptions,
                 FontKeys const& _fontKeys);

    void setRenderTarget(RenderTarget& _renderTarget) override;

    void inspect(std::ostream& _textOutput) const;

    void clearCache() override;

    void updateFontMetrics();

    void setPressure(bool _pressure) noexcept { pressure_ = _pressure; }

    /// Must be invoked before a new terminal frame is rendered.
    void beginFrame();

    /// Renders a given terminal's grid cell that has been
    /// transformed into a RenderCell.
    void renderCell(RenderCell const& _cell);

    /// Must be invoked when rendering the terminal's text has finished for this frame.
    void endFrame();

  private:
    /// Puts a sequence of codepoints that belong to the same grid cell at @p _pos
    /// at the end of the currently filled line.
    void appendCellTextToCluster(gsl::span<char32_t const> _codepoints, TextStyle _style, RGBColor _color);
    text::shape_result const& getOrCreateCachedGlyphPositions();
    text::shape_result requestGlyphPositions();
    text::shape_result shapeRun(unicode::run_segmenter::range const& _run);
    void flushTextCluster();

    void renderRun(crispy::Point _startPos, text::shape_result const& _glyphPositions, RGBColor _color);

    // rendering
    //
    RenderTileAttributes const* getOrCreateRasterizedMetadata(text::glyph_key const& _id,
                                                              unicode::PresentationStyle _presentation);

    /**
     * Creates (and rasterizes) a single glyph and returns its
     * render tile attributes required for the render step.
     */
    std::optional<RenderTileAttributes> rasterizeGlyph(crispy::StrongHash const& hash,
                                                       text::glyph_key const& id,
                                                       unicode::PresentationStyle presentation);

    // TODO(pr) gpos should be applied into RenderTileAttributes already.
    void renderRasterizedGlyph(crispy::Point _pos,
                               RGBAColor _color,
                               atlas::TileLocation _rasterizedGlyph,
                               RasterizedGlyphMetrics const& _glyphMetrics,
                               text::glyph_position const& _glyphPos);

    // general properties
    //
    GridMetrics const& gridMetrics_;
    FontDescriptions& fontDescriptions_;
    FontKeys const& fonts_;

    // performance optimizations
    //
    bool pressure_ = false;

    using ShapingResultCache = crispy::StrongLRUHashtable<text::shape_result>;
    using ShapingResultCachePtr = ShapingResultCache::CachePtr;

    ShapingResultCachePtr shapingResultCache_;
    text::shaper& textShaper_; // TODO: make unique_ptr, get owned, export cref for other users in Renderer impl.
    TextureAtlas& textureAtlas_; // owned by Renderer

    // sub-renderer
    //
    // TODO(pr) BoxDrawingRenderer boxDrawingRenderer_;

    // work-data for the current text cluster
    TextStyle style_ = TextStyle::Invalid;
    RGBColor color_ {};
    crispy::Point textPosition_;
    std::vector<char32_t> codepoints_;
    std::vector<unsigned> clusters_;
    unsigned cellCount_ = 0;
    bool textStartFound_ = false;
    bool forceCellGroupSplit_ = false;
};

} // namespace terminal::renderer

namespace fmt
{ // {{{
template <>
struct formatter<terminal::renderer::FontDescriptions>
{
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx)
    {
        return ctx.begin();
    }
    template <typename FormatContext>
    auto format(terminal::renderer::FontDescriptions const& fd, FormatContext& ctx)
    {
        return format_to(ctx.out(),
                         "({}, {}, {}, {}, {}, {})",
                         fd.size,
                         fd.regular,
                         fd.bold,
                         fd.italic,
                         fd.boldItalic,
                         fd.emoji,
                         fd.renderMode);
    }
};

template <>
struct formatter<terminal::renderer::FontLocatorEngine>
{
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx)
    {
        return ctx.begin();
    }
    template <typename FormatContext>
    auto format(terminal::renderer::FontLocatorEngine value, FormatContext& ctx)
    {
        using terminal::renderer::FontLocatorEngine;
        switch (value)
        {
        case FontLocatorEngine::Mock: return format_to(ctx.out(), "Mock");
        case FontLocatorEngine::FontConfig: return format_to(ctx.out(), "FontConfig");
        case FontLocatorEngine::DWrite: return format_to(ctx.out(), "DirectWrite");
        case FontLocatorEngine::CoreText: return format_to(ctx.out(), "CoreText");
        }
        return format_to(ctx.out(), "UNKNOWN");
    }
};

template <>
struct formatter<terminal::renderer::TextShapingEngine>
{
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx)
    {
        return ctx.begin();
    }
    template <typename FormatContext>
    auto format(terminal::renderer::TextShapingEngine value, FormatContext& ctx)
    {
        using terminal::renderer::TextShapingEngine;
        switch (value)
        {
        case TextShapingEngine::OpenShaper: return format_to(ctx.out(), "OpenShaper");
        case TextShapingEngine::DWrite: return format_to(ctx.out(), "DirectWrite");
        case TextShapingEngine::CoreText: return format_to(ctx.out(), "CoreText");
        }
        return format_to(ctx.out(), "UNKNOWN");
    }
};

} // namespace fmt
