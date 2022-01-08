#pragma once

#include <array>
#include <optional>
#include <string>

namespace terminal::renderer
{

/// Dectorator, to decorate a grid cell, eventually containing a character
///
/// It should be possible to render multiple decoration onto the same coordinates.
enum class Decorator
{
    /// Draws an underline
    Underline,
    /// Draws a doubly underline
    DoubleUnderline,
    /// Draws a curly underline
    CurlyUnderline,
    /// Draws a dotted underline
    DottedUnderline,
    /// Draws a dashed underline
    DashedUnderline,
    /// Draws an overline
    Overline,
    /// Draws a strike-through line
    CrossedOut,
    /// Draws a box around the glyph, this is literally the bounding box of a grid cell.
    /// This could be used for debugging.
    /// TODO: That should span the box around the whole (potentially wide) character
    Framed,
    /// Puts a circle-shape around into the cell (and ideally around the glyph)
    /// TODO: How'd that look like with double-width characters?
    Encircle,
};

std::optional<Decorator> to_decorator(std::string const& _value) noexcept;

// {{{ impl
inline std::optional<Decorator> to_decorator(std::string const& _value) noexcept
{
    using std::pair;
    auto constexpr mappings = std::array {
        pair { "underline", Decorator::Underline },
        pair { "dotted-underline", Decorator::DottedUnderline },
        pair { "double-underline", Decorator::DoubleUnderline },
        pair { "curly-underline", Decorator::CurlyUnderline },
        pair { "dashed-underline", Decorator::DashedUnderline },
        pair { "overline", Decorator::Overline },
        pair { "crossed-out", Decorator::CrossedOut },
        pair { "framed", Decorator::Framed },
        pair { "encircle", Decorator::Encircle },
    };

    for (auto const& mapping: mappings)
        if (mapping.first == _value)
            return { mapping.second };

    return std::nullopt;
}
// }}}

} // namespace terminal::renderer
