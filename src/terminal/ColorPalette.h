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
#include <terminal/Image.h>
#include <terminal/defines.h>

#include <crispy/StrongHash.h>
#include <crispy/stdfs.h>

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <variant>

namespace terminal
{

struct ImageData
{
    terminal::ImageFormat format;
    int rowAlignment = 1;
    ImageSize size;
    std::vector<uint8_t> pixels;

    crispy::StrongHash hash;

    void updateHash() noexcept;
};

using ImageDataPtr = std::shared_ptr<ImageData const>;

struct BackgroundImage
{
    using Location = std::variant<FileSystem::path, ImageDataPtr>;

    Location location;
    crispy::StrongHash hash;

    // image configuration
    float opacity = 1.0; // normalized value
    bool blur = false;
};

struct ColorPalette
{
    using Palette = std::array<RGBColor, 256>;

    Palette palette = []() constexpr
    {
        Palette colors;

        // normal colors
        colors[0] = 0x000000_rgb; // black
        colors[1] = 0x800000_rgb; // red
        colors[2] = 0x008000_rgb; // green
        colors[3] = 0x808000_rgb; // yellow
        colors[4] = 0x000080_rgb; // blue
        colors[5] = 0x800080_rgb; // magenta
        colors[6] = 0x008080_rgb; // cyan
        colors[7] = 0xc0c0c0_rgb; // white

        // bright colors
        colors[8] = 0x707070_rgb;  // bright black (dark gray)
        colors[9] = 0xff0000_rgb;  // bright red
        colors[10] = 0x00ff00_rgb; // bright green
        colors[11] = 0xffff00_rgb; // bright yellow
        colors[12] = 0x0000ff_rgb; // bright blue
        colors[13] = 0xff00ff_rgb; // bright magenta
        colors[14] = 0x00ffff_rgb; // bright blue
        colors[15] = 0xffffff_rgb; // bright white

        // colors 16-231 are a 6x6x6 color cube
        for (unsigned red = 0; red < 6; ++red)
            for (unsigned green = 0; green < 6; ++green)
                for (unsigned blue = 0; blue < 6; ++blue)
                    colors[16 + (red * 36) + (green * 6) + blue] =
                        RGBColor { static_cast<uint8_t>(red ? (red * 40 + 55) : 0),
                                   static_cast<uint8_t>(green ? (green * 40 + 55) : 0),
                                   static_cast<uint8_t>(blue ? (blue * 40 + 55) : 0) };

        // colors 232-255 are a grayscale ramp, intentionally leaving out black and white
        for (uint8_t gray = 0, level = uint8_t(gray * 10 + 8); gray < 24;
             ++gray, level = uint8_t(gray * 10 + 8))
            colors[232 + gray] = RGBColor { level, level, level };

        return colors;
    }
    ();

    RGBColor normalColor(size_t _index) const noexcept
    {
        assert(_index < 8);
        return palette.at(_index);
    }

    RGBColor brightColor(size_t _index) const noexcept
    {
        assert(_index < 8);
        return palette.at(_index + 8);
    }

    RGBColor dimColor(size_t _index) const
    {
        assert(_index < 8);
        return palette.at(_index); // TODO
    }

    RGBColor indexedColor(size_t _index) const noexcept
    {
        assert(_index < 256);
        return palette.at(_index);
    }

    RGBColor defaultForeground = 0xD0D0D0;
    RGBColor defaultBackground = 0x000000;
    std::optional<RGBColor> selectionForeground = std::nullopt;
    std::optional<RGBColor> selectionBackground = std::nullopt;

    CursorColor cursor;

    RGBColor mouseForeground = 0x800000;
    RGBColor mouseBackground = 0x808000;

    struct
    {
        RGBColor normal = 0x0070F0;
        RGBColor hover = 0xFF0000;
    } hyperlinkDecoration;

    std::shared_ptr<BackgroundImage const> backgroundImage;
};

enum class ColorTarget
{
    Foreground,
    Background,
};

RGBColor apply(ColorPalette const& _profile, Color _color, ColorTarget _target, bool _bright) noexcept;

} // namespace terminal
