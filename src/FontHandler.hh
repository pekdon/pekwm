//
// FontHandler.hh for pekwm
// Copyright (C) 2004-2020 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#pragma once

#include "config.h"

#include "PFont.hh"
#include "Handler.hh"

#include <map>
#include <string>

//! @brief FontHandler, a caching and font type transparent font handler.
class FontHandler {
public:
    FontHandler(void);
    ~FontHandler(void);

    PFont *getFont(const std::string &font);
    void returnFont(PFont *font);

    PFont::Color *getColor(const std::string &color);
    void returnColor(PFont::Color *color);

private:
    void loadColor(const std::string &color, PFont::Color *font_color, bool fg);
    void freeColor(PFont::Color *font_color);

private:
    std::vector<HandlerEntry<PFont*> > _fonts;
    std::vector<HandlerEntry<PFont::Color*> > _colours;
};

namespace pekwm
{
    FontHandler* fontHandler();
}
