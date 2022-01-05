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

/*
    ### abstract control flow of a single frame

    beginFrame
        renderCell...
            appendCellTextToCluster
            flushTextCluster?
                getOrCreateCachedGlyphPositions
                renderRun
                    getOrCreateRasterizedMetadata
                        rasterizeGlyph
                    renderRasterizedGlyph
    endFrame
        &flushTextCluster...
*/

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

TextRenderer::TextRenderer(TextureAtlas& _textureAtlas,
                           GridMetrics const& _gridMetrics,
                           text::shaper& _textShaper,
                           FontDescriptions& _fontDescriptions,
                           FontKeys const& _fonts):
    gridMetrics_ { _gridMetrics },
    fontDescriptions_ { _fontDescriptions },
    fonts_ { _fonts },
    shapingResultCache_ { ShapingResultCache::create(crispy::StrongHashtableSize { 4096 },
                                                     crispy::LRUCapacity { 4000 }) },
    textShaper_ { _textShaper },
    textureAtlas_ { _textureAtlas }
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
    Require(codepoints_.empty());
    Require(clusters_.empty());

    auto constexpr DefaultColor = RGBColor {};
    style_ = TextStyle::Invalid;
    color_ = DefaultColor;
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

    auto const codepoints = gsl::span(_cell.codepoints.data(), _cell.codepoints.size());

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
                flushTextCluster();
            forceCellGroupSplit_ = true;
            return;
        }
    }
#endif

    if (forceCellGroupSplit_ || _cell.groupStart)
    {
        // fmt::print("TextRenderer.sequenceStart: {}\n", textPosition_);
        forceCellGroupSplit_ = false;
        textPosition_ = gridMetrics_.map(_cell.position);
    }

    appendCellTextToCluster(codepoints, style, _cell.foregroundColor);

    if (_cell.groupEnd)
        flushTextCluster();
}

void TextRenderer::endFrame()
{
    flushTextCluster();
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
                                         RasterizedGlyphMetrics const& _glyphMetrics,
                                         text::glyph_position const& _glyphPos)
{
    auto const x = _pos.x + _glyphMetrics.bearing.x + _glyphPos.offset.x;

    // Emoji are simple square bitmap fonts that do not need special positioning.
    auto const y = _glyphPos.presentation == unicode::PresentationStyle::Emoji
                       ? _pos.y
                       : _pos.y                                          // bottom left
                             + _glyphPos.offset.y                        // -> harfbuzz adjustment
                             + gridMetrics_.baseline                     // -> baseline
                             + _glyphMetrics.bearing.y                   // -> bitmap top
                             - _glyphMetrics.bitmapSize.height.as<int>() // -> bitmap height
        ;

    auto tile = atlas::RenderTile {};
    tile.x = atlas::RenderTile::X { x };
    tile.y = atlas::RenderTile::Y { y };
    tile.tileLocation = _rasterizedGlyph;
    tile.color = toNormalized4Color(_color);

    textureAtlas_.backend().renderTile(tile);

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

void TextRenderer::appendCellTextToCluster(gsl::span<char32_t const> _codepoints,
                                           TextStyle _style,
                                           RGBColor _color)
{
    bool const attribsChanged = _color != color_ || _style != style_;
    bool const hasText = !_codepoints.empty() && _codepoints[0] != 0x20;
    bool const noText = !hasText;
    bool const textStartFound = !textStartFound_ && hasText;
    if (noText)
        textStartFound_ = false;
    if (attribsChanged || textStartFound || noText)
    {
        if (cellCount_)
            flushTextCluster(); // also increments text start position
        color_ = _color;
        style_ = _style;
        textStartFound_ = textStartFound;
    }

    for (char32_t const codepoint: _codepoints)
    {
        codepoints_.emplace_back(codepoint);
        clusters_.emplace_back(cellCount_);
    }
    cellCount_++;
}

void TextRenderer::flushTextCluster()
{
    // fmt::print("TextRenderer.sequenceEnd: textPos={}, cellCount={}, width={}, count={}\n",
    //            textPosition_.x, cellCount_,
    //            gridMetrics_.cellSize.width,
    //            codepoints_.size());

    if (!codepoints_.empty())
    {
        text::shape_result const& glyphPositions = getOrCreateCachedGlyphPositions();
        renderRun(textPosition_, glyphPositions, color_);
    }

    codepoints_.clear();
    clusters_.clear();
    textPosition_.x += static_cast<int>(*gridMetrics_.cellSize.width * cellCount_);
    cellCount_ = 0;
    textStartFound_ = false;
}

void TextRenderer::renderRun(crispy::Point _pos, text::shape_result const& _glyphPositions, RGBColor _color)
{
    crispy::Point pen = _pos;
    auto const advanceX = *gridMetrics_.cellSize.width;

    for (text::glyph_position const& gpos: _glyphPositions)
    {
        if (RenderTileAttributes const* ti = getOrCreateRasterizedMetadata(gpos.glyph, gpos.presentation))
        {
            auto tileLocation = atlas::TileLocation {};    // was: TextureInfo
            auto glyphMetrics = RasterizedGlyphMetrics {}; // was: Metadata a.k.a. rasterizer result,
                                                           // GlyphMetrics (bitmap size + glyph bearing)
            renderRasterizedGlyph(pen, _color, tileLocation, glyphMetrics, gpos);
        }

        if (gpos.advance.x)
        {
            // Only advance horizontally, as we're (guess what) a terminal. :-)
            // Only advance in fixed-width steps.
            // Only advance iff there harfbuzz told us to.
            pen.x += static_cast<decltype(pen.x)>(advanceX);
        }
    }
}

RenderTileAttributes const* TextRenderer::getOrCreateRasterizedMetadata(
    text::glyph_key const& glyphKey, unicode::PresentationStyle presentationStyle)
{
    auto const hash = hashGlyphKeyAndPresentation(glyphKey, presentationStyle);

    RenderTileAttributes* result =
        textureAtlas_.get_or_try_emplace(hash, [&]() -> optional<RenderTileAttributes> {
            return rasterizeGlyph(hash, glyphKey, presentationStyle);
        });

    if (result)
        return nullopt;

    return { *result };
}

optional<RenderTileAttributes> TextRenderer::rasterizeGlyph(StrongHash const& hash,
                                                            text::glyph_key const& glyphKey,
                                                            unicode::PresentationStyle _presentation)
{
    auto theGlyphOpt = textShaper_.rasterize(glyphKey, fontDescriptions_.renderMode);
    if (!theGlyphOpt.has_value())
        return nullopt;

    text::rasterized_glyph& glyph = theGlyphOpt.value();
    Require(glyph.bitmap.size()
            == text::pixel_size(glyph.format) * unbox<size_t>(glyph.size.width)
                   * unbox<size_t>(glyph.size.height));
    auto const numCells = _presentation == unicode::PresentationStyle::Emoji
                              ? 2u
                              : 1u; // is this the only case - with colored := Emoji presentation?
    // FIXME: this `2` is a hack of my bad knowledge. FIXME.
    // As I only know of emojis being colored fonts, and those take up 2 cell with units.

    // {{{ scale bitmap down iff bitmap is emoji and overflowing in diemensions
    if (glyph.format == text::bitmap_format::rgba)
    {
        // FIXME !
        // We currently assume that only Emoji can be RGBA, but there are also colored glyphs!

        auto const cellSize = gridMetrics_.cellSize;
        if (numCells > 1 && // XXX for now, only if emoji glyph
            (glyph.size.width > (cellSize.width * numCells) || glyph.size.height > cellSize.height))
        {
            auto const newSize = ImageSize { Width(*cellSize.width * numCells), cellSize.height };
            auto [scaled, factor] = text::scale(glyph, newSize);

            glyph.size = scaled.size; // TODO: there shall be only one with'x'height.

            // center the image in the middle of the cell
            glyph.position.y = gridMetrics_.cellSize.height.as<int>() - gridMetrics_.baseline;
            glyph.position.x =
                (gridMetrics_.cellSize.width.as<int>() * numCells - glyph.size.width.as<int>()) / 2;

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
    auto const yMin = yMax - glyph.size.height.as<int>();

    // Number of pixel lines this rasterized glyph is overflowing above cell-top,
    // or 0 if not overflowing.
    auto const yOverflow = max(0, yMax - gridMetrics_.cellSize.height.as<int>());

    // Rasterized glyph's aspect ratio. This value
    // is needed for proper down-scaling of a pixmap (used for emoji specifically).
    auto const ratio =
        _presentation != unicode::PresentationStyle::Emoji
            ? 1.0f
            : max(float(gridMetrics_.cellSize.width.as<int>() * numCells) / float(glyph.size.width.as<int>()),
                  float(gridMetrics_.cellSize.height.as<int>()) / float(glyph.size.height.as<int>()));

    // userFormat is the identifier that can be used inside the shaders
    // to distinguish between the various supported formats and chose
    // the right texture atlas.
    // targetAtlas the the atlas this texture will be uploaded to.
    auto&& [userFormat, targetAtlas] = [this, glyphFormat = glyph.format]() -> pair<int, TextureAtlas&> {
        switch (glyphFormat)
        {
        case text::bitmap_format::rgba: return { 1, *colorAtlas_ };
        case text::bitmap_format::rgb: return { 2, *lcdAtlas_ };
        case text::bitmap_format::alpha_mask: return { 0, *monochromeAtlas_ };
        }
        return { 0, *monochromeAtlas_ };
    }();

    // Mapping from glyph ID to it's texture format.
    glyphToTextureMapping_[_id] = glyph.format;

    // If the rasterized glyph is overflowing above the grid cell metrics,
    // then cut off at the top.
    if (yOverflow)
    {
        LOGSTORE(RasterizerLog)("Cropping {} overflowing bitmap rows.", yOverflow);
        glyph.size.height -= Height(yOverflow);
        // Might have it done also, but better be save: glyph.position.y -= yOverflow;
        glyph.bitmap.resize(text::pixel_size(glyph.format) * unbox<size_t>(glyph.size.width)
                            * unbox<size_t>(glyph.size.height));
        Guarantee(glyph.valid());
    }

    // If the rasterized glyph is underflowing below the grid cell's minimum (0),
    // then cut off at grid cell's bottom.
    if (yMin < 0)
    {
        auto const rowCount = -yMin;
        Require(rowCount <= *glyph.size.height);
        auto const pixelCount = rowCount * unbox<int>(glyph.size.width) * text::pixel_size(glyph.format);
        Require(0 < pixelCount && pixelCount <= glyph.bitmap.size());
        LOGSTORE(RasterizerLog)("Cropping {} underflowing bitmap rows.", rowCount);
        glyph.size.height += Height(yMin);
        auto& data = glyph.bitmap;
        data.erase(begin(data), next(begin(data), pixelCount)); // XXX asan hit (size = -2)
        Guarantee(glyph.valid());
    }

    GlyphMetrics metrics {};
    metrics.bitmapSize = glyph.size;
    metrics.bearing = glyph.position;

    // clang-format off
    if (RasterizerLog)
        LOGSTORE(RasterizerLog)("Inserting {} id {} render mode {} {} ratio {} yOverflow {} yMin {}.",
                                glyph,
                                _id.index,
                                fontDescriptions_.renderMode,
                                _presentation,
                                ratio,
                                yOverflow,
                                yMin);
    // clang-format on

    return targetAtlas.insert(_id, glyph.size, glyph.size * ratio, move(glyph.bitmap), userFormat, metrics);
}

text::shape_result const& TextRenderer::getOrCreateCachedGlyphPositions()
{
    auto const codepoints = u32string_view(codepoints_.data(), codepoints_.size());
    if (auto p = shapingResultCache_->try_get(TextCacheKey { codepoints, style_ }))
        return *p;

    auto hash = StrongHasher<u32string_view> {}(codepoints);
    auto const cacheKeyFromStorage = TextCacheKey { codepoints, style_ };
    return shapingResultCache_->emplace(cacheKeyFromStorage, requestGlyphPositions());
    return shapingResultCache_->get_or_emplace(hash, [this]() {});
}

text::shape_result TextRenderer::requestGlyphPositions()
{
    text::shape_result glyphPositions;
    unicode::run_segmenter::range run;
    auto rs = unicode::run_segmenter(codepoints_.data(), codepoints_.size());
    while (rs.consume(out(run)))
        for (text::glyph_position gpos: shapeRun(run))
            glyphPositions.emplace_back(gpos);

    return glyphPositions;
}

text::shape_result TextRenderer::shapeRun(unicode::run_segmenter::range const& _run)
{
    bool const isEmojiPresentation =
        std::get<unicode::PresentationStyle>(_run.properties) == unicode::PresentationStyle::Emoji;

    auto const font = isEmojiPresentation ? fonts_.emoji : getFontForStyle(fonts_, style_);

    // TODO(where to apply cell-advances) auto const advanceX = gridMetrics_.cellSize.width;
    auto const count = static_cast<int>(_run.end - _run.start);
    auto const codepoints = u32string_view(codepoints_.data() + _run.start, count);
    auto const clusters = gsl::span(clusters_.data() + _run.start, count);

    text::shape_result gpos;
    gpos.reserve(clusters.size());
    textShaper_.shape(font,
                      codepoints,
                      clusters,
                      std::get<unicode::Script>(_run.properties),
                      std::get<unicode::PresentationStyle>(_run.properties),
                      gpos);

    if (RasterizerLog && !gpos.empty())
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
        // auto const& font = gpos.front().glyph.font;
        // msg.write("using font: \"{}\" \"{}\" \"{}\"\n", font.familyName(), font.styleName(),
        // font.filePath());

        msg.append("with metrics:");
        for (text::glyph_position const& gp: gpos)
            msg.append(" {}", gp);
    }

    return gpos;
}
// }}}

} // namespace terminal::renderer
