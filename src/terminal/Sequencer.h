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

#include <terminal/Functions.h>
#include <terminal/Image.h>
#include <terminal/ParserEvents.h>
#include <terminal/ParserExtension.h>
#include <terminal/Sequence.h>
#include <terminal/SixelParser.h>
#include <terminal/primitives.h>

#include <unicode/convert.h>

#include <cassert>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace terminal
{

template <typename EventListener>
class Screen;

// {{{ TODO: refactor me
// XTSMGRAPHICS (xterm extension)
// CSI ? Pi ; Pa ; Pv S
namespace XtSmGraphics
{
    enum class Item
    {
        NumberOfColorRegisters = 1,
        SixelGraphicsGeometry = 2,
        ReGISGraphicsGeometry = 3,
    };

    enum class Action
    {
        Read = 1,
        ResetToDefault = 2,
        SetToValue = 3,
        ReadLimit = 4
    };

    using Value = std::variant<std::monostate, unsigned, ImageSize>;
} // namespace XtSmGraphics

/// TBC - Tab Clear
///
/// This control function clears tab stops.
enum class HorizontalTabClear
{
    /// Ps = 0 (default)
    AllTabs,

    /// Ps = 3
    UnderCursor,
};

/// Input: CSI 16 t
///
///  Input: CSI 14 t (for text area size)
///  Input: CSI 14; 2 t (for full window size)
/// Output: CSI 14 ; width ; height ; t
enum class RequestPixelSize
{
    CellArea,
    TextArea,
    WindowArea,
};

/// DECRQSS - Request Status String
enum class RequestStatusString
{
    SGR,
    DECSCL,
    DECSCUSR,
    DECSCA,
    DECSTBM,
    DECSLRM,
    DECSLPP,
    DECSCPP,
    DECSNLS
};

/// DECSIXEL - Sixel Graphics Image.
struct SixelImage
{ // TODO: this struct is only used internally in Sequencer, make it private
    /// Size in pixels for this image
    ImageSize size;

    /// RGBA buffer of the image to be rendered
    Image::Data rgba;
};

inline std::string setDynamicColorValue(
    RGBColor const& color) // TODO: yet another helper. maybe SemanticsUtils static class?
{
    auto const r = static_cast<unsigned>(static_cast<float>(color.red) / 255.0f * 0xFFFF);
    auto const g = static_cast<unsigned>(static_cast<float>(color.green) / 255.0f * 0xFFFF);
    auto const b = static_cast<unsigned>(static_cast<float>(color.blue) / 255.0f * 0xFFFF);
    return fmt::format("rgb:{:04X}/{:04X}/{:04X}", r, g, b);
}

enum class ApplyResult
{
    Ok,
    Invalid,
    Unsupported,
};
// }}}

/// Sequencer - The semantic VT analyzer layer.
///
/// Sequencer implements the translation from VT parser events, forming a higher level Sequence,
/// that can be matched against actions to perform on the target Screen.
template <typename TheTerminal>
class Sequencer
{
    decltype(auto) state() noexcept { return terminal_.state(); }
    decltype(auto) state() const noexcept { return terminal_.state(); }

  public:
    /// Constructs the sequencer stage.
    Sequencer(TheTerminal& _terminal, std::shared_ptr<SixelColorPalette> _imageColorPalette);

    void setMaxImageSize(ImageSize value) { state().maxImageSize = value; }
    void setUsePrivateColorRegisters(bool _value) { state().usePrivateColorRegisters = _value; }

    uint64_t instructionCounter() const noexcept { return state().instructionCounter; }
    void resetInstructionCounter() noexcept { state().instructionCounter = 0; }
    char32_t precedingGraphicCharacter() const noexcept { return state().precedingGraphicCharacter; }

    // ParserEvents
    //
    void error(std::string_view _errorString);
    void print(char _text);
    void print(std::string_view _chars);
    void execute(char _controlCode);
    void clear();
    void collect(char _char);
    void collectLeader(char _leader);
    void param(char _char);
    void dispatchESC(char _function);
    void dispatchCSI(char _function);
    void startOSC();
    void putOSC(char _char);
    void dispatchOSC();
    void hook(char _function);
    void put(char _char);
    void unhook();
    void startAPC() {}
    void putAPC(char) {}
    void dispatchAPC() {}
    void startPM() {}
    void putPM(char) {}
    void dispatchPM() {}

  private:
    void executeControlFunction(char _c0);
    void handleSequence();

    [[nodiscard]] std::unique_ptr<ParserExtension> hookSTP(Sequence const& _ctx);
    [[nodiscard]] std::unique_ptr<ParserExtension> hookSixel(Sequence const& _ctx);
    [[nodiscard]] std::unique_ptr<ParserExtension> hookDECRQSS(Sequence const& _ctx);
    [[nodiscard]] std::unique_ptr<ParserExtension> hookXTGETTCAP(Sequence const& /*_seq*/);

    void applyAndLog(FunctionDefinition const& _function, Sequence const& _context);
    ApplyResult apply(FunctionDefinition const& _function, Sequence const& _context);

    decltype(auto) screen() noexcept { return terminal_.screen(); }
    decltype(auto) screen() const noexcept { return terminal_.screen(); }

    // private data
    //
    TheTerminal& terminal_;
    Sequence sequence_ {};

    std::unique_ptr<ParserExtension> hookedParser_;
    std::unique_ptr<SixelImageBuilder> sixelImageBuilder_;
    std::shared_ptr<SixelColorPalette> imageColorPalette_;
};

} // namespace terminal

// {{{ fmt formatter
namespace fmt
{

template <>
struct formatter<terminal::RequestStatusString>
{
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx)
    {
        return ctx.begin();
    }

    template <typename FormatContext>
    constexpr auto format(terminal::RequestStatusString value, FormatContext& ctx)
    {
        switch (value)
        {
        case terminal::RequestStatusString::SGR: return format_to(ctx.out(), "SGR");
        case terminal::RequestStatusString::DECSCL: return format_to(ctx.out(), "DECSCL");
        case terminal::RequestStatusString::DECSCUSR: return format_to(ctx.out(), "DECSCUSR");
        case terminal::RequestStatusString::DECSCA: return format_to(ctx.out(), "DECSCA");
        case terminal::RequestStatusString::DECSTBM: return format_to(ctx.out(), "DECSTBM");
        case terminal::RequestStatusString::DECSLRM: return format_to(ctx.out(), "DECSLRM");
        case terminal::RequestStatusString::DECSLPP: return format_to(ctx.out(), "DECSLPP");
        case terminal::RequestStatusString::DECSCPP: return format_to(ctx.out(), "DECSCPP");
        case terminal::RequestStatusString::DECSNLS: return format_to(ctx.out(), "DECSNLS");
        }
        return format_to(ctx.out(), "{}", unsigned(value));
    }
};

template <>
struct formatter<terminal::Sequence>
{
    template <typename ParseContext>
    constexpr auto parse(ParseContext& ctx)
    {
        return ctx.begin();
    }
    template <typename FormatContext>
    auto format(terminal::Sequence const& seq, FormatContext& ctx)
    {
        return format_to(ctx.out(), "{}", seq.text());
    }
};
} // namespace fmt
