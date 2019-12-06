/**
 * This file is part of the "libterminal" project
 *   Copyright (c) 2019 Christian Parpart <christian@parpart.family>
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
#include <terminal/Commands.h>          // CursorDisplay
#include <terminal/WindowSize.h>
#include <terminal_view/GLCursor.h>
#include <terminal_view/GLLogger.h>

#include <ground/stdfs.h>

#include <optional>
#include <string>
#include <variant>
#include <unordered_map>

namespace actions {
    struct ToggleFullScreen{};
    struct ScreenshotVT{};
    struct IncreaseFontSize{};
    struct DecreaseFontSize{};
    struct IncreaseOpacity{};
    struct DecreaseOpacity{};
    struct SendChars{ std::string chars; };
    struct WriteScreen{ std::string chars; }; // "\033[2J\033[3J"
    struct ScrollOneUp{};
    struct ScrollOneDown{};
    struct ScrollUp{};
    struct ScrollDown{};
    struct ScrollPageUp{};
    struct ScrollPageDown{};
    struct ScrollToTop{};
    struct ScrollToBottom{};
	struct PasteClipboard{};
	struct CopySelection{};
	struct PasteSelection{};
	struct NewTerminal{};
	struct OpenConfiguration{};
    struct Quit{};
	// CloseTab
	// OpenTab
	// FocusNextTab
	// FocusPreviousTab
}

using Action = std::variant<
    actions::ToggleFullScreen,
    actions::ScreenshotVT,
    actions::IncreaseFontSize,
    actions::DecreaseFontSize,
    actions::IncreaseOpacity,
    actions::DecreaseOpacity,
    actions::SendChars,
    actions::WriteScreen,
    actions::ScrollOneUp,
    actions::ScrollOneDown,
    actions::ScrollUp,
    actions::ScrollDown,
    actions::ScrollPageUp,
    actions::ScrollPageDown,
    actions::ScrollToTop,
    actions::ScrollToBottom,
    actions::CopySelection,
    actions::PasteSelection,
    actions::PasteClipboard,
	actions::NewTerminal,
	actions::OpenConfiguration,
    actions::Quit
>;

struct InputMapping {
    terminal::InputEvent input;
    Action action;
};

struct Config {
    FileSystem::path backingFilePath;
    std::optional<FileSystem::path> logFilePath;

    std::string shell;

    terminal::WindowSize terminalSize;
    std::optional<size_t> maxHistoryLineCount;
    size_t historyScrollMultiplier;
    bool autoScrollOnUpdate;
    bool fullscreen;
    unsigned short fontSize;
    std::string fontFamily;
    terminal::CursorDisplay cursorDisplay;
    terminal::CursorShape cursorShape;
    unsigned int tabWidth;
    terminal::Opacity backgroundOpacity; // value between 0 (fully transparent) and 0xFF (fully visible).
    bool backgroundBlur; // On Windows 10, this will enable Acrylic Backdrop.
    terminal::view::LogMask loggingMask;

    std::string wordDelimiters;

    terminal::ColorProfile colorProfile{};
    std::vector<InputMapping> inputMappings;
};

std::optional<int> loadConfigFromCLI(Config& _config, int argc, char const* argv[]);
void loadConfigFromFile(Config& _config, FileSystem::path const& _fileName);

std::string serializeYaml(Config const& _config);
void saveConfigToFile(Config const& _config, FileSystem::path const& _path);
