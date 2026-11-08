#pragma once

#include "Strings.h"
#include "Win32.h"

#include <string>

// The choices and the game in progress, kept between sessions in %APPDATA%\Chess3D\Chess3D.ini.
struct Settings {
    int theme = 0;
    int level = 3;
    int mode = 0;
    bool sound = true;
    bool panel = true;
    Language language = Language::English;
    bool hasWindow = false;
    RECT window{};
    bool maximized = false;
    std::string game; // see chess::Game::Serialize

    static Settings Load();
    void Save() const;
};
