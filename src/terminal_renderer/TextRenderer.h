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

/// Text Rendering Pipeline
class TextRenderer: public Renderable
{
  public:
    TextRenderer(GridMetrics const& _gridMetrics,
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
    void appendCellTextToClusterGroup(std::u32string const& _codepoints, TextStyle _style, RGBColor _color);
    text::shape_result const& getOrCreateCachedGlyphPositions();
    text::shape_result createTextShapedGlyphPositions();
    text::shape_result shapeTextRun(unicode::run_segmenter::range const& _run);
    void flushTextClusterGroup();

    void renderRun(crispy::Point initialPenPosition,
                   text::shape_result const& _glyphPositions,
                   RGBColor _color);

    atlas::TileAttributes<RenderTileAttributes> const* getOrCreateRasterizedMetadata(
        text::glyph_key _id, unicode::PresentationStyle _presentation);

    /**
     * Creates (and rasterizes) a single glyph and returns its
     * render tile attributes required for the render step.
     */
    std::optional<TextureAtlas::TileCreateData> rasterizeGlyph(atlas::TileLocation targetLocation,
                                                               crispy::StrongHash const& hash,
                                                               text::glyph_key const& id,
                                                               unicode::PresentationStyle presentation);

    void renderRasterizedGlyph(crispy::Point targetSurfacePosition,
                               RGBAColor glyphColor,
                               atlas::TileLocation rasterizedGlyph,
                               RenderTileAttributes const& glyphMetrics,
                               text::glyph_position const& glyphShapingPosition);

    // general properties
    //
    GridMetrics const& gridMetrics_;
    FontDescriptions& fontDescriptions_;
    FontKeys const& fonts_;

    // performance optimizations
    //
    bool pressure_ = false;

    using ShapingResultCache = crispy::StrongLRUHashtable<text::shape_result>;
    using ShapingResultCachePtr = ShapingResultCache::Ptr;

    ShapingResultCachePtr shapingResultCache_;
    text::shaper&
        textShaper_; // TODO: make unique_ptr, get owned, export cref for other users in Renderer impl.

    // sub-renderer
    //
    // TODO(pr) BoxDrawingRenderer boxDrawingRenderer_;

    // work-data for the current text cluster group
    struct TextClusterGroup
    {
        // pen-start position of this text group
        crispy::Point textPosition {};

        // uniform text style for this text group
        TextStyle style = TextStyle::Invalid;

        // uniform text color for this text group
        RGBColor color {};

        // codepoints within this text group with
        // uniform unicode properties (script, language, direction).
        std::vector<char32_t> codepoints;

        // cluster indices for each codepoint
        std::vector<unsigned> clusters;

        // number of grid cells processed
        unsigned cellCount = 0;
    };
    TextClusterGroup textClusterGroup_ {};

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
