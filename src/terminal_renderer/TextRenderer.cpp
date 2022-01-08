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

/*
### abstract control flow of a single frame

    beginFrame
        renderCell...
            appendCellTextToClusterGroup
            flushTextClusterGroup?
                getOrCreateCachedGlyphPositions
                renderRun
                    getOrCreateRasterizedMetadata
                        rasterizeGlyph
                    renderRasterizedGlyph
    endFrame
        &flushTextClusterGroup...

### BETTER SOLUTION

    TODO: create struct TextClusterGroup { ... };

    beginFrame
        renderCell...
            appendCellTextToClusterGroup
            flushTextClusterGroup?
                getOrCreateRasterizedMetadataFromTextClusterGroup
                    getOrCreateCachedGlyphPositions
                    renderRun
                        getOrCreateRasterizedMetadata
                            rasterizeGlyph
                renderRasterizedGlyph
    endFrame
        &flushTextClusterGroup...

*/

#include <terminal/logging.h>

// TODO(pr) #include <terminal_renderer/BoxDrawingRenderer.h>
#include <terminal_renderer/GridMetrics.h>
#include <terminal_renderer/TextRenderer.h>
#include <terminal_renderer/utils.h>

#include <text_shaper/fontconfig_locator.h>
#include <text_shaper/mock_font_locator.h>

#if defined(_WIN32)
    #include <text_shaper/directwrite_locator.h>
#endif

#if defined(__APPLE__)
    #include <text_shaper/coretext_locator.h>
#endif

#include <crispy/algorithm.h>
#include <crispy/assert.h>
#include <crispy/indexed.h>
#include <crispy/range.h>

#include <unicode/convert.h>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include <range/v3/algorithm/copy.hpp>

using crispy::StrongHash;

using unicode::out;

using std::array;
using std::get;
using std::make_unique;
using std::max;
using std::move;
using std::nullopt;
using std::optional;
using std::pair;
using std::u32string;
using std::u32string_view;
using std::vector;

using namespace std::placeholders;

namespace terminal::renderer
{

namespace
{
    StrongHash hashGlyphKeyAndPresentation(text::glyph_key key,
                                           unicode::PresentationStyle presentation) noexcept
    {
        auto hash = StrongHash {}; // TODO
        return hash;
    }

    StrongHash hashTextAndStyle(u32string_view text, TextStyle style) noexcept
    {
        size_t const weakTextHash = std::hash<u32string_view> {}(text);
        return StrongHash(0,
                          0,
                          static_cast<uint32_t>(weakTextHash >> 32),
                          static_cast<uint32_t>(weakTextHash & 0xFFFF) + static_cast<uint32_t>(style));
    }

    constexpr auto toNormalized4Color(RGBAColor color) noexcept
    {
        return array {
            static_cast<float>(color.red()) / 255.0f,
            static_cast<float>(color.green()) / 255.0f,
            static_cast<float>(color.blue()) / 255.0f,
            static_cast<float>(color.alpha()) / 255.0f,
        };
    };

    text::font_key getFontForStyle(FontKeys const& _fonts, TextStyle _style)
    {
        switch (_style)
        {
        case TextStyle::Invalid: break;
        case TextStyle::Regular: return _fonts.regular;
        case TextStyle::Bold: return _fonts.bold;
        case TextStyle::Italic: return _fonts.italic;
        case TextStyle::BoldItalic: return _fonts.boldItalic;
        }
        return _fonts.regular;
    }
} // namespace

std::unique_ptr<text::font_locator> createFontLocator(FontLocatorEngine _engine)
{
    switch (_engine)
    {
    case FontLocatorEngine::Mock: return make_unique<text::mock_font_locator>();
    case FontLocatorEngine::DWrite:
#if defined(_WIN32)
        return make_unique<text::directwrite_locator>();
#else
        LOGSTORE(text::LocatorLog)("Font locator DirectWrite not supported on this platform.");
#endif
        break;
    case FontLocatorEngine::CoreText:
#if defined(__APPLE__)
        return make_unique<text::coretext_locator>();
#else
        LOGSTORE(text::LocatorLog)("Font locator CoreText not supported on this platform.");
#endif
        break;

    case FontLocatorEngine::FontConfig:
        // default case below
        break;
    }

    LOGSTORE(text::LocatorLog)("Using font locator: fontconfig.");
    return make_unique<text::fontconfig_locator>();
}

// TODO: What's a good value here? Or do we want to make that configurable,
// or even computed based on memory resources available?
constexpr size_t TextShapingCacheSize = 1000;

TextRenderer::TextRenderer(GridMetrics const& _gridMetrics,
                           text::shaper& _textShaper,
                           FontDescriptions& _fontDescriptions,
                           FontKeys const& _fonts):
    gridMetrics_ { _gridMetrics },
    fontDescriptions_ { _fontDescriptions },
    fonts_ { _fonts },
    shapingResultCache_ { ShapingResultCache::create(crispy::StrongHashtableSize { 4096 },
                                                     crispy::LRUCapacity { 4000 }) },
    textShaper_ { _textShaper }
// TODO(pr) , boxDrawingRenderer_ { _gridMetrics }
{
}

void TextRenderer::inspect(std::ostream& _textOutput) const
{
    // _textOutput << fmt::format("cache key storage items: {}\n", cacheKeyStorage_.size());
    // _textOutput << fmt::format("shaping cache items: {}\n", cache_.size());
    // _textOutput << fmt::format("glyph to texture mappings: {}\n", glyphToTextureMapping_.size());
}

void TextRenderer::setRenderTarget(RenderTarget& _renderTarget)
{
    Renderable::setRenderTarget(_renderTarget);
    // TODO(pr) boxDrawingRenderer_.setRenderTarget(_renderTarget);
    clearCache();
}

void TextRenderer::clearCache()
{
    shapingResultCache_->clear();

    // TODO(pr) boxDrawingRenderer_.clearCache();
}

void TextRenderer::updateFontMetrics()
{
    if (!renderTargetAvailable())
        return;

    clearCache();
}

void TextRenderer::beginFrame()
{
    // fmt::print("beginFrame: {} / {}\n", codepoints_.size(), clusters_.size());
    Require(textClusterGroup_.codepoints.empty());
    Require(textClusterGroup_.clusters.empty());

    auto constexpr DefaultColor = RGBColor {};
    textClusterGroup_.style = TextStyle::Invalid;
    textClusterGroup_.color = DefaultColor;
}

void TextRenderer::renderCell(RenderCell const& _cell)
{
    auto const style = [](auto mask) constexpr->TextStyle
    {
        if (contains_all(mask, CellFlags::Bold | CellFlags::Italic))
            return TextStyle::BoldItalic;
        if (mask & CellFlags::Bold)
            return TextStyle::Bold;
        if (mask & CellFlags::Italic)
            return TextStyle::Italic;
        return TextStyle::Regular;
    }
    (_cell.flags);

    auto const& codepoints = _cell.codepoints;

#if 0 // TODO(pr)
    bool const isBoxDrawingCharacter = fontDescriptions_.builtinBoxDrawing && _cell.codepoints.size() == 1
                                       && boxDrawingRenderer_.renderable(codepoints[0]);

    if (isBoxDrawingCharacter)
    {
        auto const success = boxDrawingRenderer_.render(
            _cell.position.line, _cell.position.column, codepoints[0], _cell.foregroundColor);
        if (success)
        {
            if (!forceCellGroupSplit_)
                flushTextClusterGroup();
            forceCellGroupSplit_ = true;
            return;
        }
    }
#endif

    if (forceCellGroupSplit_ || _cell.groupStart)
    {
        // fmt::print("TextRenderer.sequenceStart: {}\n", textPosition_);
        forceCellGroupSplit_ = false;
        textClusterGroup_.textPosition = gridMetrics_.map(_cell.position);
    }

    appendCellTextToClusterGroup(codepoints, style, _cell.foregroundColor);

    if (_cell.groupEnd)
        flushTextClusterGroup();
}

void TextRenderer::endFrame()
{
    flushTextClusterGroup();
}

/**
 * Renders a tile relative to the shape run's base position.
 *
 * @param _pos          offset relative to the glyph run's base position
 * @param _color        text color
 * @param _glyphMetrics bitmap size and glyph bearing (cachable)
 * @param _glyphPos     glyph positioning relative to the pen's baseline pos (cachable)
 *
 */
void TextRenderer::renderRasterizedGlyph(crispy::Point _pos,
                                         RGBAColor _color,
                                         atlas::TileLocation _rasterizedGlyph,
                                         RenderTileAttributes const& _glyphMetrics,
                                         text::glyph_position const& _glyphPos)
{
    auto const x = _pos.x + _glyphMetrics.x.value + _glyphPos.offset.x;

    // Emoji are simple square bitmap fonts that do not need special positioning.
    auto const y = _glyphPos.presentation == unicode::PresentationStyle::Emoji
                       ? _pos.y
                       : _pos.y                                          // bottom left
                             + _glyphPos.offset.y                        // -> harfbuzz adjustment
                             + gridMetrics_.baseline                     // -> baseline
                             + _glyphMetrics.y.value                     // -> bitmap top
                             - _glyphMetrics.bitmapSize.height.as<int>() // -> bitmap height
        ;

    auto tile = atlas::RenderTile {};
    tile.x = atlas::RenderTile::X { x };
    tile.y = atlas::RenderTile::Y { y };
    tile.tileLocation = _rasterizedGlyph;
    tile.color = toNormalized4Color(_color);

    textureScheduler().renderTile(tile);

#if 0
    if (RasterizerLog)
        LOGSTORE(RasterizerLog)(
                "xy={}:{} pos=({}:{}) tex={}, gpos=({}:{}), baseline={}",
                x, y,
                _pos,
                _textureInfo.bitmapSize,
                _glyphPos.offset.x, _glyphPos.offset.y,
                gridMetrics_.baseline);
#endif
}

void TextRenderer::appendCellTextToClusterGroup(std::u32string const& _codepoints,
                                                TextStyle _style,
                                                RGBColor _color)
{
    bool const attribsChanged = _color != textClusterGroup_.color || _style != textClusterGroup_.style;
    bool const hasText = !_codepoints.empty() && _codepoints[0] != 0x20;
    bool const noText = !hasText;
    bool const textStartFound = !textStartFound_ && hasText;
    if (noText)
        textStartFound_ = false;
    if (attribsChanged || textStartFound || noText)
    {
        if (textClusterGroup_.cellCount)
            flushTextClusterGroup(); // also increments text start position
        textClusterGroup_.color = _color;
        textClusterGroup_.style = _style;
        textStartFound_ = textStartFound;
    }

    for (char32_t const codepoint: _codepoints)
    {
        textClusterGroup_.codepoints.emplace_back(codepoint);
        textClusterGroup_.clusters.emplace_back(textClusterGroup_.cellCount);
    }
    textClusterGroup_.cellCount++;
}

void TextRenderer::flushTextClusterGroup()
{
    // fmt::print("TextRenderer.sequenceEnd: textPos={}, cellCount={}, width={}, count={}\n",
    //            textPosition_.x, cellCount_,
    //            gridMetrics_.cellSize.width,
    //            codepoints_.size());

    if (!textClusterGroup_.codepoints.empty())
    {
        text::shape_result const& glyphPositions = getOrCreateCachedGlyphPositions();
        renderRun(textClusterGroup_.textPosition, glyphPositions, textClusterGroup_.color);
    }

    textClusterGroup_.codepoints.clear();
    textClusterGroup_.clusters.clear();
    textClusterGroup_.textPosition.x +=
        static_cast<int>(*gridMetrics_.cellSize.width * textClusterGroup_.cellCount);
    textClusterGroup_.cellCount = 0;
    textStartFound_ = false;
}

void TextRenderer::renderRun(crispy::Point initialPenPosition,
                             text::shape_result const& _glyphPositions,
                             RGBColor _color)
{
    crispy::Point pen = initialPenPosition;
    auto const advanceX = *gridMetrics_.cellSize.width;

    for (text::glyph_position const& glyphPosition: _glyphPositions)
    {
        if (atlas::TileAttributes<RenderTileAttributes> const* ta =
                getOrCreateRasterizedMetadata(glyphPosition.glyph, glyphPosition.presentation))
        {
            auto const tileLocation = ta->location;
            RenderTileAttributes const& glyphMetrics = ta->metadata;
            renderRasterizedGlyph(pen, _color, tileLocation, glyphMetrics, glyphPosition);
        }

        if (glyphPosition.advance.x)
        {
            // Only advance horizontally, as we're (guess what) a terminal. :-)
            // Only advance in fixed-width steps.
            // Only advance iff there harfbuzz told us to.
            pen.x += static_cast<decltype(pen.x)>(advanceX);
        }
    }
}

atlas::TileAttributes<RenderTileAttributes> const* TextRenderer::getOrCreateRasterizedMetadata(
    text::glyph_key glyphKey, unicode::PresentationStyle presentationStyle)
{
    auto const hash = hashGlyphKeyAndPresentation(glyphKey, presentationStyle);

    // TextureAtlas<RenderTileAttributes> -> optional<RenderTileAttributes>
    return textureAtlas_->get_or_try_emplace(
        hash, [&](atlas::TileLocation targetLocation) -> optional<TextureAtlas::TileCreateData> {
            return rasterizeGlyph(targetLocation, hash, glyphKey, presentationStyle);
        });
}

auto TextRenderer::rasterizeGlyph(atlas::TileLocation targetLocation,
                                  StrongHash const& hash,
                                  text::glyph_key const& glyphKey,
                                  unicode::PresentationStyle _presentation)
    -> optional<TextureAtlas::TileCreateData>
{
    auto theGlyphOpt = textShaper_.rasterize(glyphKey, fontDescriptions_.renderMode);
    if (!theGlyphOpt.has_value())
        return nullopt;

    text::rasterized_glyph& glyph = theGlyphOpt.value();
    Require(glyph.bitmap.size()
            == text::pixel_size(glyph.format) * unbox<size_t>(glyph.bitmapSize.width)
                   * unbox<size_t>(glyph.bitmapSize.height));
    auto const numCells = _presentation == unicode::PresentationStyle::Emoji
                              ? 2
                              : 1; // is this the only case - with colored := Emoji presentation?
    // FIXME: this `2` is a hack of my bad knowledge. FIXME.
    // As I only know of emojis being colored fonts, and those take up 2 cell with units.

    // {{{ scale bitmap down iff bitmap is emoji and overflowing in diemensions
    if (glyph.format == text::bitmap_format::rgba)
    {
        // FIXME !
        // We currently assume that only Emoji can be RGBA, but there are also colored glyphs!

        auto const cellSize = gridMetrics_.cellSize;
        if (numCells > 1 && // XXX for now, only if emoji glyph
            (*glyph.bitmapSize.width > (*cellSize.width * numCells)
             || glyph.bitmapSize.height > cellSize.height))
        {
            auto const newSize = ImageSize { Width(*cellSize.width * numCells), cellSize.height };
            auto [scaled, factor] = text::scale(glyph, newSize);

            glyph.bitmapSize = scaled.bitmapSize; // TODO: there shall be only one with'x'height.

            // center the image in the middle of the cell
            glyph.position.y = gridMetrics_.cellSize.height.as<int>() - gridMetrics_.baseline;
            glyph.position.x =
                (gridMetrics_.cellSize.width.as<int>() * numCells - glyph.bitmapSize.width.as<int>()) / 2;

            // (old way)
            // glyph.metrics.bearing.x /= factor;
            // glyph.metrics.bearing.y /= factor;

            glyph.bitmap = move(scaled.bitmap);

            // XXX currently commented out because it's not used.
            // TODO: But it should be used for cutting the image off the right edge with unnecessary
            // transparent pixels.
            //
            // int const rightEdge = [&]() {
            //     auto rightEdge = std::numeric_limits<int>::max();
            //     for (int x = glyph.bitmap.width - 1; x >= 0; --x) {
            //         for (int y = 0; y < glyph.bitmap.height; ++y)
            //         {
            //             auto const& pixel = &glyph.bitmap.data.at(y * glyph.bitmap.width * 4 + x * 4);
            //             if (pixel[3] > 20)
            //                 rightEdge = x;
            //         }
            //         if (rightEdge != std::numeric_limits<int>::max())
            //             break;
            //     }
            //     return rightEdge;
            // }();
            // if (rightEdge != std::numeric_limits<int>::max())
            //     LOGSTORE(RasterizerLog)("right edge found. {} < {}.", rightEdge+1, glyph.bitmap.width);
        }
    }
    // }}}

    // y-position relative to cell-bottom of glyphs top.
    auto const yMax = gridMetrics_.baseline + glyph.position.y;

    // y-position relative to cell-bottom of the glyphs bottom.
    auto const yMin = yMax - glyph.bitmapSize.height.as<int>();

    // Number of pixel lines this rasterized glyph is overflowing above cell-top,
    // or 0 if not overflowing.
    auto const yOverflow = max(0, yMax - gridMetrics_.cellSize.height.as<int>());

    // Rasterized glyph's aspect ratio. This value
    // is needed for proper down-scaling of a pixmap (used for emoji specifically).
    auto const ratio =
        _presentation != unicode::PresentationStyle::Emoji
            ? 1.0f
            : max(float(gridMetrics_.cellSize.width.as<int>() * numCells)
                      / float(glyph.bitmapSize.width.as<int>()),
                  float(gridMetrics_.cellSize.height.as<int>()) / float(glyph.bitmapSize.height.as<int>()));

    // If the rasterized glyph is overflowing above the grid cell metrics,
    // then cut off at the top.
    if (yOverflow)
    {
        LOGSTORE(RasterizerLog)("Cropping {} overflowing bitmap rows.", yOverflow);
        glyph.bitmapSize.height -= Height(yOverflow);
        // Might have it done also, but better be save: glyph.position.y -= yOverflow;
        glyph.bitmap.resize(text::pixel_size(glyph.format) * unbox<size_t>(glyph.bitmapSize.width)
                            * unbox<size_t>(glyph.bitmapSize.height));
        Guarantee(glyph.valid());
    }

    // If the rasterized glyph is underflowing below the grid cell's minimum (0),
    // then cut off at grid cell's bottom.
    if (yMin < 0)
    {
        auto const rowCount = -yMin;
        Require(rowCount <= *glyph.bitmapSize.height);
        auto const pixelCount =
            rowCount * unbox<int>(glyph.bitmapSize.width) * text::pixel_size(glyph.format);
        Require(0 < pixelCount && pixelCount <= glyph.bitmap.size());
        LOGSTORE(RasterizerLog)("Cropping {} underflowing bitmap rows.", rowCount);
        glyph.bitmapSize.height += Height(yMin);
        auto& data = glyph.bitmap;
        data.erase(begin(data), next(begin(data), pixelCount)); // XXX asan hit (size = -2)
        Guarantee(glyph.valid());
    }

    // clang-format off
    if (RasterizerLog)
        LOGSTORE(RasterizerLog)("Inserting {} id {} render mode {} {} ratio {} yOverflow {} yMin {}.",
                                glyph,
                                glyphKey.index,
                                fontDescriptions_.renderMode,
                                _presentation,
                                ratio,
                                yOverflow,
                                yMin);
    // clang-format on

    auto rasterizedTile = atlas::UploadTile {};
    rasterizedTile.location = targetLocation;
    rasterizedTile.bitmap = move(glyph.bitmap);
    rasterizedTile.bitmapSize = glyph.bitmapSize;
    textureScheduler().uploadTile(rasterizedTile);

    // TODO(pr) Apply to bitmap or at render time?
    // To bitmap means software, to render time means GPU (preferred I think).
    auto const scaledBitmapSize = glyph.bitmapSize * ratio;

    auto renderTileAttributes = RenderTileAttributes {};
    renderTileAttributes.x = RenderTileAttributes::X { glyph.position.x };
    renderTileAttributes.y = RenderTileAttributes::Y { glyph.position.y };
    renderTileAttributes.bitmapSize = glyph.bitmapSize;

    return TextureAtlas::TileCreateData { move(glyph.bitmap), glyph.bitmapSize, renderTileAttributes };
}

text::shape_result const& TextRenderer::getOrCreateCachedGlyphPositions()
{
    return shapingResultCache_->get_or_emplace(
        hashTextAndStyle(
            u32string_view(textClusterGroup_.codepoints.data(), textClusterGroup_.codepoints.size()),
            textClusterGroup_.style),
        [this](auto) { return createTextShapedGlyphPositions(); });
}

text::shape_result TextRenderer::createTextShapedGlyphPositions()
{
    auto glyphPositions = text::shape_result {};

    auto run = unicode::run_segmenter::range {};
    auto rs = unicode::run_segmenter(
        u32string_view(textClusterGroup_.codepoints.data(), textClusterGroup_.codepoints.size()));
    while (rs.consume(out(run)))
        for (text::glyph_position const& glyphPosition: shapeTextRun(run))
            glyphPositions.emplace_back(glyphPosition);

    return glyphPositions;
}

/**
 * Performs text shaping on a text run, that is, a sequence of codepoints
 * with a uniform set of properties:
 *  - same direction
 *  - same script tag
 *  - same language tag
 *  - same SGR attributes (font style, color)
 */
text::shape_result TextRenderer::shapeTextRun(unicode::run_segmenter::range const& _run)
{
    bool const isEmojiPresentation =
        std::get<unicode::PresentationStyle>(_run.properties) == unicode::PresentationStyle::Emoji;

    auto const font = isEmojiPresentation ? fonts_.emoji : getFontForStyle(fonts_, textClusterGroup_.style);

    // TODO(where to apply cell-advances) auto const advanceX = gridMetrics_.cellSize.width;
    auto const count = static_cast<int>(_run.end - _run.start);
    auto const codepoints = u32string_view(textClusterGroup_.codepoints.data() + _run.start, count);
    auto const clusters = gsl::span(textClusterGroup_.clusters.data() + _run.start, count);

    text::shape_result glyphPosition;
    glyphPosition.reserve(clusters.size());
    textShaper_.shape(font,
                      codepoints,
                      clusters,
                      std::get<unicode::Script>(_run.properties),
                      std::get<unicode::PresentationStyle>(_run.properties),
                      glyphPosition);

    if (RasterizerLog && !glyphPosition.empty())
    {
        auto msg = LOGSTORE(RasterizerLog);
        msg.append("Shaped codepoints: {}", unicode::convert_to<char>(codepoints));
        msg.append("  (presentation: {}/{})",
                   isEmojiPresentation ? "emoji" : "text",
                   get<unicode::PresentationStyle>(_run.properties));

        msg.append(" (");
        for (auto const [i, codepoint]: crispy::indexed(codepoints))
        {
            if (i)
                msg.append(" ");
            msg.append("U+{:04X}", unsigned(codepoint));
        }
        msg.append(")\n");

        // A single shape run always uses the same font,
        // so it is sufficient to just print that.
        // auto const& font = glyphPosition.front().glyph.font;
        // msg.write("using font: \"{}\" \"{}\" \"{}\"\n", font.familyName(), font.styleName(),
        // font.filePath());

        msg.append("with metrics:");
        for (text::glyph_position const& gp: glyphPosition)
            msg.append(" {}", gp);
    }

    return glyphPosition;
}
// }}}

} // namespace terminal::renderer
