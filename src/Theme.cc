//
// Theme.cc for pekwm
// Copyright (C) 2003-2021 the pekwm development team
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "config.h"

#include "Theme.hh"

#include "Debug.hh"
#include "X11.hh"
#include "PFont.hh"
#include "PTexture.hh"
#include "FontHandler.hh"
#include "ImageHandler.hh"
#include "TextureHandler.hh"
#include "Util.hh"

#include <cstdlib>
#include <iostream>
#include <string>

#define DEFAULT_FONT "Sans:size=12#XFT"
#define DEFAULT_LARGE_FONT "Sans:size=14:weight=bold#XFT"
#define DEFAULT_HEIGHT 17
#define DEFAULT_HEIGHT_STR "17"

static void parse_pad(const std::string& str, uint *pad)
{
    std::vector<std::string> tok;
    if (Util::splitString(str, tok, " \t", 4) == 4) {
        for (uint i = 0; i < PAD_NO; ++i) {
            try {
                pad[i] = std::stoi(tok[i]);
            } catch (const std::invalid_argument&) {
                pad[i] = 0;
            }
        }
    }
}

// Theme::ColorMap
bool
Theme::ColorMap::load(CfgParser::Entry *section)
{
    auto it = section->begin();
    for (; it != section->end(); ++it) {
        if (strcasecmp((*it)->getName().c_str(), "MAP")) {
            USER_WARN("unexpected entry " << (*it)->getName()
                      << " in ColorMap");
            continue;
        }

        auto to = (*it)->getSection()
            ? (*it)->getSection()->findEntry("TO") : nullptr;
        if (to == nullptr) {
            USER_WARN("missing To entry in ColorMap entry");
        } else {
            try {
                int from_c = parseColor((*it)->getValue());
                int to_c = parseColor(to->getValue());
                insert({from_c, to_c});
            } catch (std::invalid_argument&) {
                USER_WARN("invalid colors from " << (*it)->getValue()
                          << " to " << to->getValue() << " in ColorMap entry");
            }
        }
    }
    return true;
}

int
Theme::ColorMap::parseColor(const std::string& str)
{
    if (str.size() != 7 || str[0] != '#') {
        throw std::invalid_argument("invalid color: " + str);
    }

    // ARGB format, always set full alpha
    const char *p = str.c_str() + 1;
    uchar cbuf[4] = {255, parseHex(p), parseHex(p + 2), parseHex(p + 4)};
    int c;
    memcpy(&c, cbuf, sizeof(cbuf));
    return c;
}

uchar
Theme::ColorMap::parseHex(const char *p)
{
    char val[3] = {p[0], p[1], 0};
    return static_cast<uchar>(strtol(val, NULL, 16));
}

void
Theme::ColorMap::unload(void)
{
    clear();
}

// Theme::PDecorButtonData

//! @brief Theme::PDecorButtonData constructor.
Theme::PDecorButtonData::PDecorButtonData(TextureHandler *th)
    : _th(th),
      _loaded(false),
      _shape(true),
      _left(false),
      _width(1),
      _height(1)
{
    for (uint i = 0; i < BUTTON_STATE_NO; ++i) {
        _texture[i] = 0;
    }
}

//! @brief Theme::PDecorButtonData destructor.
Theme::PDecorButtonData::~PDecorButtonData(void)
{
    unload();
}

//! @brief Parses CfgParser::Entry section, loads and verifies data.
//! @param section CfgParser::Entry with button configuration.
//! @return True if a valid button was parsed.
bool
Theme::PDecorButtonData::load(CfgParser::Entry *section)
{
    if (*section == "LEFT") {
        _left = true;
    } else if (*section == "RIGHT") {
        _left = false;
    } else {
        check();
        return false;
    }
    _loaded = true;

    // Get actions.
    ActionEvent ae;
    auto it(section->begin());
    for (; it != section->end(); ++it) {
        if (ActionConfig::parseActionEvent(*it, ae, BUTTONCLICK_OK, true)) {
            _aes.push_back (ae);
        }
    }

    // Got some actions, consider it to be a valid button.
    if (_aes.size() > 0) {
        CfgParser::Entry *value;

        value = section->findEntry("SETSHAPE");
        if (value) {
            _shape = Util::isTrue(value->getValue());
        }

        value = section->findEntry("FOCUSED");
        if (value) {
            _texture[BUTTON_STATE_FOCUSED] = _th->getTexture(value->getValue());
        }
        value = section->findEntry("UNFOCUSED");
        if (value) {
            _texture[BUTTON_STATE_UNFOCUSED] =
                _th->getTexture(value->getValue());
        }
        value = section->findEntry("PRESSED");
        if (value) {
            _texture[BUTTON_STATE_PRESSED] = _th->getTexture(value->getValue());
        }

        // HOOVER has been kept around due to backwards compatibility.
        value = section->findEntry("HOVER");
        if (! value) {
            value = section->findEntry("HOOVER");
        }
        if (value) {
            _texture[BUTTON_STATE_HOVER] = _th->getTexture(value->getValue());
        }

        check();

        return true;
    }

    return false;
}

//! @brief Unloads data.
void
Theme::PDecorButtonData::unload(void)
{
    if (! _loaded) {
        return;
    }
    _loaded = false;

    for (uint i = 0; i < BUTTON_STATE_NO; ++i) {
        _th->returnTexture(_texture[i]);
        _texture[i] = 0;
    }
}

//! @brief Verifies and makes sure no 0 textures exists.
void
Theme::PDecorButtonData::check(void)
{
    for (uint i = 0; i < (BUTTON_STATE_NO - 1); ++i) {
        if (! _texture[i]) {
            _texture[i] = _th->getTexture("Solid #999999");
        }
    }

    _width = _texture[BUTTON_STATE_FOCUSED]->getWidth();
    _height = _texture[BUTTON_STATE_FOCUSED]->getHeight();

    _loaded = true;
}

// Theme::PDecorData

std::map<FocusedState, std::string> Theme::PDecorData::_fs_map =
    {{FOCUSED_STATE_FOCUSED, "FOCUSED"},
     {FOCUSED_STATE_UNFOCUSED, "UNFOCUSED"},
     {FOCUSED_STATE_FOCUSED_SELECTED, "FOCUSEDSELECTED"},
     {FOCUSED_STATE_UNFOCUSED_SELECTED, "UNFOCUSEDSELECTED"}};

std::map<BorderPosition, std::string> Theme::PDecorData::_border_map =
    {{BORDER_TOP_LEFT, "TOPLEFT"},
     {BORDER_TOP, "TOP"},
     {BORDER_TOP_RIGHT, "TOPRIGHT"},
     {BORDER_LEFT, "LEFT"},
     {BORDER_RIGHT, "RIGHT"},
     {BORDER_BOTTOM_LEFT, "BOTTOMLEFT"},
     {BORDER_BOTTOM, "BOTTOM"},
     {BORDER_BOTTOM_RIGHT, "BOTTOMRIGHT"}};

//! @brief Theme::PDecorData constructor.
Theme::PDecorData::PDecorData(FontHandler* fh, TextureHandler* th,
                              const char *name)
    : _fh(fh),
      _th(th),
      _loaded(false),
      _title_height(DEFAULT_HEIGHT),
      _title_width_min(0),
      _title_width_max(100),
      _title_width_symetric(true),
      _title_height_adapt(false)
{
    if (name) {
        _name = name;
    }

    // init arrays
    memset(_pad, 0, sizeof(_pad));
    for (uint i = 0; i < FOCUSED_STATE_NO; ++i) {
        _texture_tab[i] = 0;
        _font[i] = 0;
        _font_color[i] = 0;
    }
    for (uint i = 0; i < FOCUSED_STATE_FOCUSED_SELECTED; ++i) {
        _texture_main[i] = 0;
        _texture_separator[i] = 0;
    }

    for (uint i = 0; i < FOCUSED_STATE_FOCUSED_SELECTED; ++i) {
        for (uint j = 0; j < BORDER_NO_POS; ++j) {
            _texture_border[i][j] = 0;
        }
    }
}

//! @brief Theme::PDecorData destructor.
Theme::PDecorData::~PDecorData(void)
{
    unload();
}

uint
Theme::PDecorData::getPad(PadType pad) const
{
    return _pad[(pad != PAD_NO) ? pad : 0];
}

/**
 * Parses CfgParser::Entry section, loads and verifies data.
 *
 * @param section CfgParser::Entry with pdecor configuration.
 * @return true if a valid decor was constructed, else false.
 */
bool
Theme::PDecorData::load(CfgParser::Entry *section)
{
    if (! section) {
        check();
        return false;
    }

    _name = section->getValue();
    if (! _name.size()) {
        USER_WARN("no name identifying decor");
        return false;
    }

    auto title_section = section->findSection("TITLE");
    if (! title_section) {
        // no longer require a Title section, everything in the decor
        // is inside of it anyway.
        title_section = section;
    }
    _loaded = true;

    std::vector<std::string> tok;
    std::vector<CfgParserKey*> keys;
    std::string value_pad, value_focused, value_unfocused;

    keys.push_back(new CfgParserKeyNumeric<int>("HEIGHT",
                                                _title_height, 10, 0));
    keys.push_back(new CfgParserKeyNumeric<int>("WIDTHMIN",
                                                _title_width_min, 0));
    keys.push_back(new CfgParserKeyNumeric<int>("WIDTHMAX",
                                                _title_width_max, 100, 0, 100));
    keys.push_back(new CfgParserKeyBool("WIDTHSYMETRIC",
                                        _title_width_symetric));
    keys.push_back(new CfgParserKeyBool("HEIGHTADAPT", _title_height_adapt));
    keys.push_back(new CfgParserKeyString("PAD", value_pad, "0 0 0 0", 7));
    keys.push_back(new CfgParserKeyString("FOCUSED", value_focused,
                                          "Empty", _th->getLengthMin()));
    keys.push_back(new CfgParserKeyString("UNFOCUSED", value_unfocused,
                                          "Empty", _th->getLengthMin()));

    // Free up resources
    title_section->parseKeyValues(keys.begin(), keys.end());

    for_each (keys.begin(), keys.end(), Util::Free<CfgParserKey*>());
    keys.clear();

    // Handle parsed data.
    _texture_main[FOCUSED_STATE_FOCUSED] = _th->getTexture(value_focused);
    _texture_main[FOCUSED_STATE_UNFOCUSED] = _th->getTexture(value_unfocused);
    parse_pad(value_pad, _pad);

    auto tab_section = title_section->findSection("TAB");
    if (tab_section) {
        for (uint i = 0; i < FOCUSED_STATE_NO; ++i) {
            auto value = tab_section->findEntry(_fs_map[FocusedState(i)]);
            if (value) {
                _texture_tab[i] = _th->getTexture(value->getValue());
            }
        }
    }

    auto separator_section = title_section->findSection("SEPARATOR");
    if (separator_section) {
        keys.push_back(new CfgParserKeyString("FOCUSED", value_focused,
                                              "Empty", _th->getLengthMin()));
        keys.push_back(new CfgParserKeyString("UNFOCUSED", value_unfocused,
                                              "Empty", _th->getLengthMin()));

        // Parse data
        separator_section->parseKeyValues(keys.begin(), keys.end());

        // Free up resources
        for_each(keys.begin(), keys.end(), Util::Free<CfgParserKey*>());
        keys.clear();

        // Handle parsed data.
        _texture_separator[FOCUSED_STATE_FOCUSED] =
            _th->getTexture(value_focused);
        _texture_separator[FOCUSED_STATE_UNFOCUSED] =
            _th->getTexture(value_unfocused);
    }


    auto font_section = title_section->findSection("FONT");
    if (font_section) {
        for (uint i = 0; i < FOCUSED_STATE_NO; ++i) {
            auto value = font_section->findEntry(_fs_map[FocusedState(i)]);
            if (value) {
                _font[i] = _fh->getFont(value->getValue());
            }
        }
    } else {
        WARN("no font section in decor: " << _name);
    }

    auto fontcolor_section = title_section->findSection("FONTCOLOR");
    if (fontcolor_section) {
        for (uint i = 0; i < FOCUSED_STATE_NO; ++i) {
            auto value = fontcolor_section->findEntry(_fs_map[FocusedState(i)]);
            if (value) {
                _font_color[i] = _fh->getColor(value->getValue());
            }
        }
    }

    loadButtons(title_section->findSection("BUTTONS"));
    loadBorder(title_section->findSection("BORDER"));

    check();

    return true;
}

//! @brief Unloads data.
void
Theme::PDecorData::unload(void)
{
    if (! _loaded) {
        return;
    }
    _loaded = false;

    for (uint i = 0; i < FOCUSED_STATE_NO; ++i) {
        _th->returnTexture(_texture_tab[i]);
        _fh->returnFont(_font[i]);
        _fh->returnColor(_font_color[i]);

        _texture_tab[i] = 0;
        _font[i] = 0;
        _font_color[i] = 0;
    }

    for (uint i = 0; i < FOCUSED_STATE_FOCUSED_SELECTED; ++i) {
        _th->returnTexture(_texture_main[i]);
        _th->returnTexture(_texture_separator[i]);
        _texture_main[i] = 0;
        _texture_separator[i] = 0;

        for (uint j = 0; j < BORDER_NO_POS; ++j) {
            _th->returnTexture(_texture_border[i][j]);
            _texture_border[i][j] = 0;
        }
    }

    for (auto it : _buttons) {
        delete it;
    }
    _buttons.clear();
}

//! @brief Checks data properties, prints warning and tries to fix.
void
Theme::PDecorData::check(void)
{
    // check values
    if (_title_width_max > 100) {
        WARN(_name << " WIDTHMAX > 100");
        _title_width_max = 100;
    }

    checkTextures();
    checkFonts();
    checkBorder();
    checkColors();

    _loaded = true;
}

//! @brief Loads border data.
void
Theme::PDecorData::loadBorder(CfgParser::Entry *section)
{
    if (! section) {
        return;
    }

    CfgParser::Entry *sub, *value;

    sub = section->findSection("FOCUSED");
    if (sub) {
        for (uint i = 0; i < BORDER_NO_POS; ++i) {
            value = sub->findEntry(_border_map[BorderPosition (i)]);
            if (value) {
                _texture_border[FOCUSED_STATE_FOCUSED][i] =
                    _th->getTexture(value->getValue());
            }
        }
    }

    sub = section->findSection("UNFOCUSED");
    if (sub) {
        for (uint i = 0; i < BORDER_NO_POS; ++i) {
            value = sub->findEntry(_border_map[BorderPosition (i)]);
            if (value) {
                _texture_border[FOCUSED_STATE_UNFOCUSED][i] =
                    _th->getTexture(value->getValue());
            }
        }
    }
}

//! @brief Loads button data.
void
Theme::PDecorData::loadButtons(CfgParser::Entry *section)
{
    if (! section) {
        return;
    }

    auto it(section->begin());
    for (; it != section->end(); ++it) {
        if (! (*it)->getSection()) {
            continue;
        }

        auto btn = new Theme::PDecorButtonData(_th);
        if (btn->load((*it)->getSection())) {
            _buttons.push_back(btn);
        } else {
            delete btn;
        }
    }
}

//! @brief Checks for 0 textures, prints warning and sets empty texture
void
Theme::PDecorData::checkTextures(void)
{
    const char *textures[] = {
        "Solid #eeeeee",
        "Solid #cccccc",
        "Solid #ffffff",
        "Solid #dddddd"
    };

    for (uint i = 0; i < FOCUSED_STATE_NO; ++i) {
        if (! _texture_tab[i]) {
            WARN(_name << " missing tab texture state "
                 << _fs_map[FocusedState(i)]);
            _texture_tab[i] = _th->getTexture(textures[i]);
        }
    }
    for (uint i = 0; i < FOCUSED_STATE_FOCUSED_SELECTED; ++i) {
        if (! _texture_main[i]) {
            WARN(_name << " missing main texture state "
                 << _fs_map[FocusedState(i)]);
            _texture_main[i] = _th->getTexture(textures[i]);
        }
        if (! _texture_separator[i]) {
            WARN(_name << " missing tab texture state "
                 << _fs_map[FocusedState(i)]);
            _texture_separator[i] =
                _th->getTexture("Solid #999999 2x" DEFAULT_HEIGHT_STR);
        }
    }
}

//! @brief Checks for 0 fonts, prints warning and sets empty font
void
Theme::PDecorData::checkFonts(void)
{
    // the only font that's "obligatory" is the standard focused font,
    // others are only used if availible so we only check the focused font.
    if (! _font[FOCUSED_STATE_FOCUSED]) {
        WARN(_name << " missing font state " << _fs_map[FOCUSED_STATE_FOCUSED]);
        _font[FOCUSED_STATE_FOCUSED] = _fh->getFont(DEFAULT_FONT);
    }
}

//! @brief Checks for 0 border PTextures.
void
Theme::PDecorData::checkBorder(void)
{
    for (uint state = FOCUSED_STATE_FOCUSED;
         state < FOCUSED_STATE_FOCUSED_SELECTED; ++state) {
        for (uint i = 0; i < BORDER_NO_POS; ++i) {
            if (! _texture_border[state][i]) {
                WARN(_name << " missing border texture "
                     << _border_map[BorderPosition(i)] << " "
                     << _fs_map[FocusedState(state)]);
                _texture_border[state][i] =
                    _th->getTexture("Solid #999999 2x2");
            }
        }
    }
}

//! @brief Checks for 0 colors, prints warning and sets empty color
void
Theme::PDecorData::checkColors(void)
{
    for (uint i = 0; i < FOCUSED_STATE_NO; ++i) {
        if (! _font_color[i]) {
            WARN(_name << " missing font color state "
                 << _fs_map[FocusedState(i)]);
            _font_color[i] = _fh->getColor("#000000");
        }
    }
}

// Theme::PMenuData

//! @brief PMenuData constructor
Theme::PMenuData::PMenuData(FontHandler* fh, TextureHandler* th)
    : _fh(fh),
      _th(th),
      _loaded(false)
{
    for (uint i = 0; i <= OBJECT_STATE_NO; ++i) {
        _font[i] = 0;
        _color[i] = 0;
        _tex_menu[i] = 0;
        _tex_item[i] = 0;
        _tex_arrow[i] = 0;
    }
    for (uint i = 0; i < OBJECT_STATE_NO; ++i) {
        _tex_sep[i] = 0;
    }
    memset(_pad, 0, sizeof(_pad));
}

//! @brief PMenuData destructor
Theme::PMenuData::~PMenuData(void)
{
    unload();
}

//! @brief Parses CfgParser::Entry section, loads and verifies data.
//! @param section CfgParser::Entry with pmenu configuration.
bool
Theme::PMenuData::load(CfgParser::Entry *section)
{
    if (! section) {
        check();
        return false;
    }
    _loaded = true;

    CfgParser::Entry *value;
    value = section->findEntry("PAD");
    if (value) {
        parse_pad(value->getValue(), _pad);
    }

    value = section->findSection("FOCUSED");
    if (value) {
        loadState(value, OBJECT_STATE_FOCUSED);
    }

    value = section->findSection("UNFOCUSED");
    if (value) {
        loadState(value, OBJECT_STATE_UNFOCUSED);
    }

    value = section->findSection("SELECTED");
    if (value) {
        loadState(value, OBJECT_STATE_SELECTED);
    }

    check();

    return true;
}

//! @brief Unloads data.
void
Theme::PMenuData::unload(void)
{
    if (! _loaded) {
        return;
    }
    _loaded = false;

    for (uint i = 0; i <= OBJECT_STATE_NO; ++i) {
        _fh->returnFont(_font[i]);
        _font[i] = 0;

        _fh->returnColor(_color[i]);
        _color[i] = 0;

        _th->returnTexture(_tex_menu[i]);
        _tex_menu[i] = 0;

        _th->returnTexture(_tex_item[i]);
        _tex_item[i] = 0;

        _th->returnTexture(_tex_arrow[i]);
        _tex_arrow[i] = 0;
    }

    for (uint i = 0; i < OBJECT_STATE_NO; ++i) {
        _th->returnTexture(_tex_sep[i]);
        _tex_sep[i] = 0;
    }
}

//! @brief Check data properties, prints warning and tries to fix.
void
Theme::PMenuData::check(void)
{
    for (uint i = 0; i <= OBJECT_STATE_NO; ++i) {
        if (! _font[i]) {
            _font[i] = _fh->getFont(DEFAULT_FONT);
        }
        if (! _color[i]) {
            _color[i] = _fh->getColor("#000000");
        }
        if (! _tex_menu[i]) {
            _tex_menu[i] = _th->getTexture("Solid #ffffff");
        }
        if (! _tex_item[i]) {
            _tex_item[i] = _th->getTexture("Solid #ffffff");
        }
        if (! _tex_arrow[i]) {
            _tex_arrow[i] = _th->getTexture("Solid #000000 2x2");
        }
    }

    for (uint i = 0; i < OBJECT_STATE_NO; ++i) {
        if (! _tex_sep[i]) {
            _tex_sep[i] = _th->getTexture("Solid #000000 1x1");
        }
    }

    _loaded = true;
}

void
Theme::PMenuData::loadState(CfgParser::Entry *section, ObjectState state)
{
    std::vector<CfgParserKey*> keys;
    std::string value_font, value_background, value_item;
    std::string value_text, value_arrow, value_separator;

    keys.push_back(new CfgParserKeyString("FONT", value_font));
    keys.push_back(new CfgParserKeyString("BACKGROUND", value_background,
                                          "Solid #ffffff"));
    keys.push_back(new CfgParserKeyString("ITEM", value_item, "Solid #ffffff"));
    keys.push_back(new CfgParserKeyString("TEXT", value_text, "#000000"));
    keys.push_back(new CfgParserKeyString("ARROW", value_arrow,
                                          "Solid #000000 2x2"));
    if (state < OBJECT_STATE_SELECTED) {
        keys.push_back(new CfgParserKeyString("SEPARATOR", value_separator,
                                              "Solid #000000 1x1"));
    }

    section->parseKeyValues(keys.begin(), keys.end());

    for_each(keys.begin(), keys.end(), Util::Free<CfgParserKey*>());

    // Handle parsed data.
    _font[state] = _fh->getFont(value_font);
    _tex_menu[state] = _th->getTexture(value_background);
    _tex_item[state] = _th->getTexture(value_item);
    _color[state] = _fh->getColor(value_text);
    _tex_arrow[state] = _th->getTexture(value_arrow);
    if (state < OBJECT_STATE_SELECTED) {
        _tex_sep[state] = _th->getTexture(value_separator);
    }
}

// Theme::TextDialogData

//! @brief TextDialogData constructor.
Theme::TextDialogData::TextDialogData(FontHandler* fh, TextureHandler* th)
    : _fh(fh),
      _th(th),
      _loaded(false),
      _font(0),
      _color(0),
      _tex(0)
{
    memset(_pad, 0, sizeof(_pad));
}

//! @brief TextDialogData destructor.
Theme::TextDialogData::~TextDialogData(void)
{
    unload();
}

//! @brief Parses CfgParser::Entry section, loads and verifies data.
//! @param section CfgParser::Entry with textdialog configuration.
bool
Theme::TextDialogData::load(CfgParser::Entry *section)
{
    if (! section) {
        check();
        return false;
    }
    _loaded = true;

    std::vector<CfgParserKey*> keys;
    std::string value_font, value_text, value_texture, value_pad;

    keys.push_back(new CfgParserKeyString("FONT", value_font));
    keys.push_back(new CfgParserKeyString("TEXT", value_text, "#000000"));
    keys.push_back(new CfgParserKeyString("TEXTURE", value_texture,
                                          "Solid #ffffff"));
    keys.push_back(new CfgParserKeyString("PAD", value_pad, "0 0 0 0", 7));

    section->parseKeyValues(keys.begin(), keys.end());

    for_each(keys.begin(), keys.end(), Util::Free<CfgParserKey*>());

    // Handle parsed data.
    _font = _fh->getFont(value_font);
    _color = _fh->getColor(value_text);
    _tex = _th->getTexture(value_texture);

    parse_pad(value_pad, _pad);

    check();

    return true;
}

//! @brief Unloads data.
void
Theme::TextDialogData::unload(void)
{
    if (! _loaded) {
        return;
    }
    _loaded = false;

    _fh->returnFont(_font);
    _fh->returnColor(_color);
    _th->returnTexture(_tex);

    _font = 0;
    _tex = 0;
    _color = 0;
}

//! @brief Check data properties, prints warning and tries to fix.
//! @todo print warnings
void
Theme::TextDialogData::check(void)
{
    if (! _font) {
        _font = _fh->getFont(DEFAULT_FONT);
    }
    if (! _color) {
        _color = _fh->getColor("#000000");
    }
    if (! _tex) {
        _tex = _th->getTexture("Solid #ffffff");
    }

    _loaded = true;
}

// WorkspaceIndicatorData

/**
 * WorkspaceIndicatorData constructor
 */
Theme::WorkspaceIndicatorData::WorkspaceIndicatorData(FontHandler* fh,
                                                      TextureHandler *th)
    : _fh(fh),
      _th(th),
      _loaded(false),
      font(0),
      font_color(0),
      texture_background(0),
      texture_workspace(0),
      texture_workspace_act(0),
      edge_padding(0),
      workspace_padding(0)
{
}

/**
 * WorkspaceIndicatorData destructor
 */
Theme::WorkspaceIndicatorData::~WorkspaceIndicatorData(void)
{
    unload();
}

/**
 * Load theme data and check.
 */
bool
Theme::WorkspaceIndicatorData::load(CfgParser::Entry *section)
{
    if (! section) {
        check();
        return false;
    }
    _loaded = true;

    std::vector<CfgParserKey*> keys;

    std::string value_font, value_color, value_tex_bg;
    std::string value_tex_ws, value_tex_ws_act;

    keys.push_back(new CfgParserKeyString("FONT", value_font));
    keys.push_back(new CfgParserKeyString("TEXT", value_color));
    keys.push_back(new CfgParserKeyString("BACKGROUND", value_tex_bg));
    keys.push_back(new CfgParserKeyString("WORKSPACE", value_tex_ws));
    keys.push_back(new CfgParserKeyString("WORKSPACEACTIVE", value_tex_ws_act));
    keys.push_back(new CfgParserKeyNumeric<int>("EDGEPADDING", edge_padding,
                                                5, 0));
    keys.push_back(new CfgParserKeyNumeric<int>("WORKSPACEPADDING",
                                                workspace_padding,
                                                2, 0));

    section->parseKeyValues(keys.begin(), keys.end());
    for_each(keys.begin(), keys.end(), Util::Free<CfgParserKey*>());

    font = _fh->getFont(value_font);
    font_color = _fh->getColor(value_color);
    texture_background = _th->getTexture(value_tex_bg);
    texture_workspace = _th->getTexture(value_tex_ws);
    texture_workspace_act = _th->getTexture(value_tex_ws_act);

    check();

    return true;
}

/**
 * Unload loaded theme data.
 */
void
Theme::WorkspaceIndicatorData::unload(void)
{
    if (! _loaded) {
        return;
    }
    _loaded = false;

    _fh->returnFont(font);
    _fh->returnColor(font_color);
    _th->returnTexture(texture_background);
    _th->returnTexture(texture_workspace);
    _th->returnTexture(texture_workspace_act);

    font = 0;
    font_color = 0;
    texture_background = 0;
    texture_workspace = 0;
    texture_workspace_act = 0;
    edge_padding = 0;
    workspace_padding = 0;
}

/**
 * Validate theme data loading
 */
void
Theme::WorkspaceIndicatorData::check(void)
{
    if (! font) {
        font = _fh->getFont(DEFAULT_FONT);
    }
    if (! font_color) {
        font_color = _fh->getColor("#000000");
    }
    if (! texture_background) {
        texture_background = _th->getTexture("Solid #ffffff");
    }
    if (! texture_workspace) {
        texture_workspace = _th->getTexture("Solid #cccccc");
    }
    if (! texture_workspace_act) {
        texture_workspace_act = _th->getTexture("Solid #aaaaaa");
    }

    _loaded = true;
}

Theme::DialogData::DialogData(FontHandler* fh, TextureHandler* th)
    : _fh(fh),
      _th(th),
      _loaded(false),
      _background(nullptr),
      _button_font(nullptr),
      _button_color(nullptr),
      _title_font(nullptr),
      _title_color(nullptr),
      _text_font(nullptr),
      _text_color(nullptr)
{
    for (int i = 0; i < PAD_NO; i++) {
        _pad[i] = 2;
    }
    memset(_button, 0, sizeof(_button));
}

Theme::DialogData::~DialogData(void)
{
    unload();
}

bool
Theme::DialogData::load(CfgParser::Entry *section)
{
    if (section == nullptr) {
        check();
        return false;
    }
    _loaded = true;

    std::string val_bg, val_bfont, val_btext, val_font, val_text,
        val_tfont, val_ttext, val_pad;
    std::vector<CfgParserKey*> keys =
        {new CfgParserKeyString("BACKGROUND", val_bg, "Solid #ffffff"),
         new CfgParserKeyString("FONT", val_font, DEFAULT_FONT),
         new CfgParserKeyString("TEXT", val_text, "#000000"),
         new CfgParserKeyString("TITLEFONT", val_tfont, DEFAULT_LARGE_FONT),
         new CfgParserKeyString("TITLECOLOR", val_ttext, "#000000"),
         new CfgParserKeyString("PAD", val_pad, "0 0 0 0", 7)};
    section->parseKeyValues(keys.begin(), keys.end());
    for_each(keys.begin(), keys.end(), Util::Free<CfgParserKey*>());

    // Handle parsed data.
    _background = _th->getTexture(val_bg);
    _text_font = _fh->getFont(val_font);
    _text_color = _fh->getColor(val_text);
    _title_font = _fh->getFont(val_tfont);
    _title_color = _fh->getColor(val_ttext);
    parse_pad(val_pad, _pad);

    auto button = section->findSection("BUTTON");
    if (button != nullptr) {
        std::string val_fo, val_un, val_pr, val_ho;
        std::vector<CfgParserKey*> bkeys =
            {new CfgParserKeyString("FONT", val_font, DEFAULT_FONT),
             new CfgParserKeyString("TEXT", val_text, "#000000"),
             new CfgParserKeyString("FOCUSED", val_fo),
             new CfgParserKeyString("UNFOCUSED", val_un),
             new CfgParserKeyString("PRESSED", val_pr),
             new CfgParserKeyString("HOOVER", val_ho)};
        button->parseKeyValues(bkeys.begin(), bkeys.end());
        for_each(bkeys.begin(), bkeys.end(), Util::Free<CfgParserKey*>());

        _button_font = _fh->getFont(val_font);
        _button_color = _fh->getColor(val_text);
        _button[BUTTON_STATE_FOCUSED] = _th->getTexture(val_fo);
        _button[BUTTON_STATE_UNFOCUSED] = _th->getTexture(val_un);
        _button[BUTTON_STATE_PRESSED] = _th->getTexture(val_pr);
        _button[BUTTON_STATE_HOVER] = _th->getTexture(val_ho);
    }

    check();

    return true;
}

void
Theme::DialogData::unload(void)
{
    if (! _loaded) {
        return;
    }
    _loaded = false;

    _fh->returnColor(_text_color);
    _fh->returnFont(_text_font);
    _fh->returnColor(_button_color);
    _fh->returnFont(_title_font);
    for (int i = 0; i < BUTTON_STATE_NO; i++) {
        _th->returnTexture(_button[i]);
    }
    _fh->returnColor(_button_color);
    _fh->returnFont(_button_font);
    _th->returnTexture(_background);
}

void
Theme::DialogData::check(void)
{
    if (_background == nullptr) {
        _background = _th->getTexture("Solid #ffffff");
    }
    if (_button_font == nullptr) {
        _button_font = _fh->getFont(DEFAULT_FONT);
    }
    if (_button_color == nullptr) {
        _button_color = _fh->getColor("#000000");
    }
    const char* button_textures[] = {
        "SolidRaised #dddddd #aaaaaa #333333", // focused
        "SolidRaised #cccccc #aaaaaa #333333", // unfocused
        "SolidRaised #bbbbbb #aaaaaa #333333", // pressed
        "SolidRaised #eeeeee #aaaaaa #333333" // hover
    };
    for (int i = 0; i < BUTTON_STATE_NO; i++) {
        if (_button[i] == nullptr) {
            _button[i] = _th->getTexture(button_textures[i]);
        }
    }
    if (_title_font == nullptr) {
        _title_font = _fh->getFont(DEFAULT_LARGE_FONT);
    }
    if (_title_color == nullptr) {
        _title_color = _fh->getColor("#000000");
    }
    if (_text_font == nullptr) {
        _text_font = _fh->getFont(DEFAULT_FONT);
    }
    if (_text_color == nullptr) {
        _text_color = _fh->getColor("#000000");
    }

    _loaded = true;
}

/**
 * HarbourData constructor.
 */
Theme::HarbourData::HarbourData(TextureHandler* th)
    : _th(th),
      _loaded(false),
      _texture(nullptr)
{
}

/**
 * HarbourData destructor, unload data.
 */
Theme::HarbourData::~HarbourData(void)
{
    unload();
}

/**
 * Load harbour data and validate state, unloading previously loaded
 * data if any.
 */
bool
Theme::HarbourData::load(CfgParser::Entry *section)
{
    if (! section) {
        check();
        return false;
    }
    _loaded = true;

    auto value = section->findEntry("TEXTURE");
    if (value) {
        _texture = _th->getTexture(value->getValue());
    }

    check();

    return true;
}


/**
 * Unload harbour data.
 */
void
Theme::HarbourData::unload(void)
{
    if (! _loaded) {
        return;
    }
    _loaded = false;

    _th->returnTexture(_texture);
    _texture = 0;
}


/**
 * Check state of harbour data.
 */
void
Theme::HarbourData::check(void)
{
    if (! _texture) {
        _texture = _th->getTexture("Solid #000000");
    }

    _loaded = true;
}

// Theme

//! @brief Theme constructor
Theme::Theme(FontHandler *fh, ImageHandler *ih, TextureHandler *th,
             const std::string& theme_file, const std::string &theme_variant)
    : _fh(fh),
      _ih(ih),
      _th(th),
      _version(0),
      _loaded(false),
      _invert_gc(None),
      _dialog_data(fh, th),
      _menu_data(fh, th),
      _harbour_data(th),
      _status_data(fh, th),
      _cmd_d_data(fh, th),
      _ws_indicator_data(fh, th)
{
    _color_maps[""] = ColorMap();
    _decors[""] = nullptr;

    // window gc's
    XGCValues gv;
    gv.function = GXinvert;
    gv.subwindow_mode = IncludeInferiors;
    gv.line_width = 1;
    _invert_gc = X11::createGC(X11::getRoot(),
                               GCFunction|GCSubwindowMode|GCLineWidth, &gv);

    load(theme_file, theme_variant);
}

//! @brief Theme destructor
Theme::~Theme(void)
{
    unload();
    X11::freeGC(_invert_gc);
}

/**
 * Re-loads theme if needed, clears up previously used resources.
 */
bool
Theme::load(const std::string &dir, const std::string &variant)
{
    std::string norm_dir(dir);
    if (dir.size() && dir.at(dir.size() - 1) == '/') {
        norm_dir.erase(norm_dir.end() - 1);
    }
    std::string theme_file(norm_dir + "/theme");
    if (! variant.empty()) {
        auto theme_file_variant = theme_file + "-" + variant;
        if (Util::isFile(theme_file_variant)) {
            theme_file = theme_file_variant;
        } else {
            DBG("theme variant " << variant << " does not exist");
        }
    }

    if (_theme_dir == norm_dir
        && _theme_file == theme_file
        && ! _cfg_files.requireReload(theme_file)) {
        return false;
    }

    unload();

    _theme_dir = norm_dir;
    _theme_file = theme_file;
    if (! _theme_dir.size()) {
        USER_WARN("empty theme directory name, using default");
        _theme_dir = DATADIR "/pekwm/themes/default";
        _theme_file = _theme_dir + "/theme";
    }

    bool theme_ok = true;
    CfgParser theme;
    theme.setVar("$THEME_DIR", _theme_dir);
    if (! theme.parse(theme_file)) {
        _theme_dir = DATADIR "/pekwm/themes/default";
        theme.setVar("$THEME_DIR", _theme_dir);
        theme_file = _theme_dir + "/theme";
        if (! theme.parse(theme_file)) {
            USER_WARN("unable to load " << _theme_dir << " or default theme");
            theme_ok = false;
        }
    }

    X11::setString(X11::getRoot(), PEKWM_THEME, theme_file);

    // Setup quirks and requirements before parsing.
    if (theme_ok) {
        if (theme.isDynamicContent()) {
            _cfg_files.clear();
        } else {
            _cfg_files = theme.getCfgFiles();
        }
        loadThemeRequire(theme, theme_file);
    }
    auto root = theme.getEntryRoot();

    // Set image basedir.
    _ih->path_clear();
    _ih->path_push_back(_theme_dir + "/");

    loadVersion(root);
    loadBackground(root->findSection("BACKGROUND"));
    loadColorMaps(root->findSection("COLORMAPS"));

    _dialog_data.load(root->findSection("DIALOG"));
    loadDecors(root);

    if (! _menu_data.load(root->findSection("MENU"))) {
        WARN("Missing or malformed \"MENU\" section!");
    }
    if (! _status_data.load(root->findSection("STATUS"))) {
        WARN("Missing \"STATUS\" section!");
    }
    if (! _cmd_d_data.load(root->findSection("CMDDIALOG"))) {
        WARN("Missing \"CMDDIALOG\" section!");
    }
    auto ws_section = root->findSection("WORKSPACEINDICATOR");
    if (! _ws_indicator_data.load(ws_section)) {
        WARN("Missing \"WORKSPACEINDICATOR\" section!");
    }
    if (! _harbour_data.load(root->findSection("HARBOUR"))) {
        WARN("Missing \"HARBOUR\" section!");
    }

    _loaded = true;

    return true;
}

/**
 * Load template quirks.
 */
void
Theme::loadThemeRequire(CfgParser &theme_cfg, std::string &file)
{
    CfgParser::Entry *section;

    // Look for requires section,
    section = theme_cfg.getEntryRoot()->findSection("REQUIRE");
    if (section) {
        std::vector<CfgParserKey*> keys;
        bool value_templates;

        keys.push_back(new CfgParserKeyBool("TEMPLATES", value_templates,
                                            false));
        section->parseKeyValues(keys.begin(), keys.end());
        for_each(keys.begin(), keys.end(), Util::Free<CfgParserKey*>());

        // Re-load configuration with templates enabled.
        if (value_templates) {
            theme_cfg.clear(true);
            theme_cfg.setVar("$THEME_DIR", _theme_dir);
            theme_cfg.parse(file, CfgParserSource::SOURCE_FILE, true);
        }
    }
}

void
Theme::loadVersion(CfgParser::Entry* root)
{
    std::vector<CfgParserKey*> keys;
    keys.push_back(new CfgParserKeyNumeric<int>("VERSION", _version));
    root->parseKeyValues(keys.begin(), keys.end());
    std::for_each(keys.begin(), keys.end(), Util::Free<CfgParserKey*>());
}

void
Theme::loadBackground(CfgParser::Entry* section)
{
    _background = "";
    if (section == nullptr) {
        return;
    }

    std::vector<CfgParserKey*> keys;
    keys.push_back(new CfgParserKeyString("TEXTURE", _background, ""));
    section->parseKeyValues(keys.begin(), keys.end());
    std::for_each(keys.begin(), keys.end(), Util::Free<CfgParserKey*>());
}

void
Theme::loadColorMaps(CfgParser::Entry* section)
{
    _ih->clearColorMaps();
    if (section == nullptr) {
        return;
    }

    auto it = section->begin();
    for (; it != section->end(); ++it) {
        if (strcasecmp((*it)->getName().c_str(), "COLORMAP")) {
            USER_WARN("unexpected entry " << (*it)->getName()
                      << " in ColorMaps");
            continue;
        }

        Theme::ColorMap color_map;
        color_map.load((*it)->getSection());
        _ih->addColorMap((*it)->getValue(), color_map);
    }
}

void
Theme::loadDecors(CfgParser::Entry *root)
{
    auto section = root->findSection("PDECOR");
    if (section == nullptr) {
        section = root;
    }

    auto it = section->begin();
    for (; it != section->end(); ++it) {
        if (strcasecmp((*it)->getName().c_str(), "DECOR") != 0) {
            continue;
        }

        auto data = new Theme::PDecorData(_fh, _th);
        if (data->load((*it)->getSection())) {
            _decors[data->getName()] = data;
        } else {
            delete data;
        }
    }

    if (! getPDecorData("DEFAULT")) {
        // Create DEFAULT decor, let check fill it up with empty but
        // non-null data.
        WARN("Theme doesn't contain any DEFAULT decor.");
        auto decor_data = new Theme::PDecorData(_fh, _th, "DEFAULT");
        decor_data->check();
        _decors["DEFAULT"] = decor_data;
    }
}

/**
 * Unload theme data.
 */
void
Theme::unload(void)
{
    if (! _loaded) {
        return;
    }
    _loaded = false;

    // Unload decors
    for (auto it : _decors) {
        delete it.second;
    }
    _decors.clear();
    _decors[""] = nullptr;

    // Unload theme data
    _menu_data.unload();
    _status_data.unload();
    _cmd_d_data.unload();
    _ws_indicator_data.unload();
    _harbour_data.unload();
}
