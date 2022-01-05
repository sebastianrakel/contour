#pragma once

#include <text_shaper/font.h>

#include <crispy/point.h>

namespace terminal::renderer
{

enum class TextShapingEngine
{
    OpenShaper, //!< Uses open-source implementation: harfbuzz/freetype/fontconfig
    DWrite,     //!< native platform support: Windows
    CoreText,   //!< native platform support: OS/X
};

enum class FontLocatorEngine
{
    Mock,       //!< mock font locator API
    FontConfig, //!< platform independant font locator API
    DWrite,     //!< native platform support: Windows
    CoreText,   //!< native font locator on OS/X
};

struct FontDescriptions
{
    double dpiScale = 1.0;
    crispy::Point dpi = { 0, 0 }; // 0 => auto-fill with defaults
    text::font_size size;
    text::font_description regular;
    text::font_description bold;
    text::font_description italic;
    text::font_description boldItalic;
    text::font_description emoji;
    text::render_mode renderMode;
    TextShapingEngine textShapingEngine = TextShapingEngine::OpenShaper;
    FontLocatorEngine fontLocator = FontLocatorEngine::FontConfig;
    bool builtinBoxDrawing = true;
};

inline bool operator==(FontDescriptions const& a, FontDescriptions const& b) noexcept
{
    return a.size.pt == b.size.pt && a.regular == b.regular && a.bold == b.bold && a.italic == b.italic
           && a.boldItalic == b.boldItalic && a.emoji == b.emoji && a.renderMode == b.renderMode;
}

inline bool operator!=(FontDescriptions const& a, FontDescriptions const& b) noexcept
{
    return !(a == b);
}

enum class TextStyle
{
    Invalid = 0x00,
    Regular = 0x10,
    Bold = 0x11,
    Italic = 0x12,
    BoldItalic = 0x13,
};

constexpr TextStyle operator|(TextStyle a, TextStyle b) noexcept
{
    return static_cast<TextStyle>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}

constexpr bool operator<(TextStyle a, TextStyle b) noexcept
{
    return static_cast<unsigned>(a) < static_cast<unsigned>(b);
}

} // namespace terminal::renderer
