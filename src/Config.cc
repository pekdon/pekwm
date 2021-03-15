//
// Config.cc for pekwm
// Copyright (C) 2002-2021 the pekwm development team
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "config.h"

#include "Charset.hh"
#include "Config.hh"
#include "Compat.hh"
#include "Debug.hh"
#include "PFont.hh"
#include "Util.hh"
#include "Workspaces.hh"
#include "X11.hh" // for DPY in keyconfig code

#include <fstream>

#include <cstdlib>

extern "C" {
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
}

static Util::StringMap<ActionAccessMask> action_access_mask_map =
    {{"", ACTION_ACCESS_NO},
     {"MOVE", ACTION_ACCESS_MOVE},
     {"RESIZE", ACTION_ACCESS_RESIZE},
     {"ICONIFY", ACTION_ACCESS_ICONIFY},
     {"SHADE", ACTION_ACCESS_SHADE},
     {"STICK", ACTION_ACCESS_STICK},
     {"MAXIMIZEHORIZONTAL", ACTION_ACCESS_MAXIMIZE_HORZ},
     {"MAXIMIZEVERTICAL", ACTION_ACCESS_MAXIMIZE_VERT},
     {"FULLSCREEN", ACTION_ACCESS_FULLSCREEN},
     {"SETWORKSPACE", ACTION_ACCESS_CHANGE_DESKTOP},
     {"CLOSE", ACTION_ACCESS_CLOSE}};

static Util::StringMap<MoveResizeActionType> moveresize_map =
    {{"", NO_MOVERESIZE_ACTION},
     {"MOVEHORIZONTAL", MOVE_HORIZONTAL},
     {"MOVEVERTICAL", MOVE_VERTICAL},
     {"RESIZEHORIZONTAL", RESIZE_HORIZONTAL},
     {"RESIZEVERTICAL", RESIZE_VERTICAL},
     {"MOVESNAP", MOVE_SNAP},
     {"CANCEL", MOVE_CANCEL},
     {"END", MOVE_END}};

static Util::StringMap<InputDialogAction> inputdialog_map =
    {{"", INPUT_NO_ACTION},
     {"INSERT", INPUT_INSERT},
     {"ERASE", INPUT_REMOVE},
     {"CLEAR", INPUT_CLEAR},
     {"CLEARFROMCURSOR", INPUT_CLEARFROMCURSOR},
     {"EXEC", INPUT_EXEC},
     {"CLOSE", INPUT_CLOSE},
     {"COMPLETE", INPUT_COMPLETE},
     {"COMPLETEABORT", INPUT_COMPLETE_ABORT},
     {"CURSNEXT", INPUT_CURS_NEXT},
     {"CURSPREV", INPUT_CURS_PREV},
     {"CURSEND", INPUT_CURS_END},
     {"CURSBEGIN", INPUT_CURS_BEGIN},
     {"HISTNEXT", INPUT_HIST_NEXT},
     {"HISTPREV", INPUT_HIST_PREV}};

static Util::StringMap<MouseEventType> mouse_event_map =
    {{"", MOUSE_EVENT_NO},
     {"BUTTONPRESS", MOUSE_EVENT_PRESS},
     {"BUTTONRELEASE", MOUSE_EVENT_RELEASE},
     {"DOUBLECLICK", MOUSE_EVENT_DOUBLE},
     {"MOTION", MOUSE_EVENT_MOTION},
     {"ENTER", MOUSE_EVENT_ENTER},
     {"LEAVE", MOUSE_EVENT_LEAVE},
     {"ENTERMOVING", MOUSE_EVENT_ENTER_MOVING},
     {"MOTIONPRESSED", MOUSE_EVENT_MOTION_PRESSED}};

static Util::StringMap<ActionType> menu_action_map =
    {{"", ACTION_MENU_NEXT},
     {"NEXTITEM", ACTION_MENU_NEXT},
     {"PREVITEM", ACTION_MENU_PREV},
     {"SELECT", ACTION_MENU_SELECT},
     {"ENTERSUBMENU", ACTION_MENU_ENTER_SUBMENU},
     {"LEAVESUBMENU", ACTION_MENU_LEAVE_SUBMENU},
     {"CLOSE", ACTION_CLOSE}};

static Util::StringMap<HarbourPlacement> harbour_placement_map =
    {{"", NO_HARBOUR_PLACEMENT},
     {"TOP", TOP},
     {"LEFT", LEFT},
     {"RIGHT", RIGHT},
     {"BOTTOM", BOTTOM}};

static Util::StringMap<Orientation> harbour_orientation_map =
    {{"", NO_ORIENTATION},
     {"TOPTOBOTTOM", TOP_TO_BOTTOM},
     {"LEFTTORIGHT", TOP_TO_BOTTOM},
     {"BOTTOMTOTOP", BOTTOM_TO_TOP},
     {"RIGHTTOLEFT", BOTTOM_TO_TOP}};

static Util::StringMap<CurrHeadSelector> curr_head_selector_map =
    {{"", CURR_HEAD_SELECTOR_NO},
     {"CURSOR", CURR_HEAD_SELECTOR_CURSOR},
     {"FOCUSEDWINDOW", CURR_HEAD_SELECTOR_FOCUSED_WINDOW}};

/**
 * Parse width and height limits.
 */
bool
SizeLimits::parse(const std::string &minimum, const std::string &maximum)
{
    return (parseLimit(minimum, _limits[WIDTH_MIN], _limits[HEIGHT_MIN])
            && parseLimit(maximum, _limits[WIDTH_MAX], _limits[HEIGHT_MAX]));
}

/**
 * Parse single limit.
 */
bool
SizeLimits::parseLimit(const std::string &limit, unsigned int &min, unsigned int &max)
{
    bool status = false;
    std::vector<std::string> tokens;
    if ((Util::splitString(limit, tokens, "x", 2, true)) == 2) {
        min = strtol(tokens[0].c_str(), 0, 10);
        max = strtol(tokens[1].c_str(), 0, 10);
        status = true;
    } else {
        min = 0;
        max = 0;
    }

    return status;
}

//! @brief Constructor for Config class
Config::Config(void) :
        _moveresize_edgeattract(0), _moveresize_edgeresist(0),
        _moveresize_woattract(0), _moveresize_woresist(0),
        _moveresize_opaquemove(0), _moveresize_opaqueresize(0),
        _screen_workspaces(4),
        _screen_workspaces_per_row(0), _screen_workspace_name_default(L"Workspace"),
        _screen_edge_indent(false),
        _screen_doubleclicktime(250), _screen_fullscreen_above(true),
        _screen_fullscreen_detect(true),
        _screen_showframelist(true),
        _screen_show_status_window(true), _screen_show_status_window_on_root(false),
        _screen_show_client_id(false),
        _screen_show_workspace_indicator(500), _screen_workspace_indicator_scale(16),
        _screen_workspace_indicator_opacity(EWMH_OPAQUE_WINDOW),
        _screen_place_new(true), _screen_focus_new(false),
        _screen_focus_new_child(true), _screen_focus_steal_protect(0), _screen_honour_randr(true),
        _screen_honour_aspectratio(true),
        _screen_placement_row(false),
        _screen_placement_ltr(true), _screen_placement_ttb(true),
        _screen_placement_offset_x(0), _screen_placement_offset_y(0),
        _place_trans_parent(true),
        _screen_client_unique_name(true),
        _screen_client_unique_name_pre(" #"), _screen_client_unique_name_post(""),
        _screen_report_all_clients(false),
        _menu_select_mask(0), _menu_enter_mask(0), _menu_exec_mask(0),
        _menu_display_icons(true),
        _menu_focus_opacity(EWMH_OPAQUE_WINDOW),
        _menu_unfocus_opacity(EWMH_OPAQUE_WINDOW),
        _cmd_dialog_history_unique(true), _cmd_dialog_history_size(1024),
        _cmd_dialog_history_file("~/.pekwm/history"), _cmd_dialog_history_save_interval(16),
        _harbour_da_min_s(0), _harbour_da_max_s(0),
        _harbour_ontop(true), _harbour_maximize_over(false),
        _harbour_placement(TOP), _harbour_orientation(TOP_TO_BOTTOM), _harbour_head_nr(0),
        _harbour_opacity(EWMH_OPAQUE_WINDOW)
{
    for (uint i = 0; i <= SCREEN_EDGE_NO; ++i) {
        _screen_edge_sizes.push_back(0);
    }

    // fill the mouse action map
    for (uint i = 0; i <= MOUSE_ACTION_LIST_NO; i++) {
        auto maln = static_cast<MouseActionListName>(i);
        _mouse_action_map[maln] = new std::vector<ActionEvent>();
    }
}

//! @brief Destructor for Config class
Config::~Config(void)
{
    for (auto it : _mouse_action_map) {
        delete it.second;
    }
}

/**
 * Returns an array of NULL-terminated desktop names in UTF-8.
 *
 * @param names *names will be set to an array of desktop names or 0. The caller has
 *        to delete [] *names
 * @param length *length will be set to the complete length of array *names points
 *        to or 0.
 */
void
Config::getDesktopNamesUTF8(uchar **names, uint *length) const
{
    if (! _screen_workspace_names.size()) {
        *names = 0;
        *length = 0;
        return;
    }

    // Convert strings to UTF-8 and calculate total length
    std::string utf8_names;
    for (auto it : _screen_workspace_names) {
        std::string utf8_name(Charset::to_utf8_str(it));
        utf8_names.append(utf8_name.c_str(), utf8_name.size() + 1);
    }

    *names = new uchar[utf8_names.size()];
    ::memcpy(*names, utf8_names.c_str(), utf8_names.size());
    *length = utf8_names.size();
}

/**
 * Sets the desktop names.
 *
 * @param names names is expected to point to an array of NULL-terminated
 *        utf8-strings.
 * @param length The length of the array "names".
 */
void
Config::setDesktopNamesUTF8(char *names, ulong length)
{
    _screen_workspace_names.clear();

    if (! names || ! length) {
        return;
    }

    for (ulong i = 0; i < length;) {
        _screen_workspace_names.push_back(Charset::from_utf8_str(names));
        i += strlen(names) + 1;
        names += strlen(names) + 1;
    }
}

//! @brief Tries to load config_file, ~/.pekwm/config, SYSCONFDIR/config
bool
Config::load(const std::string &config_file)
{
    if (! _cfg_files.requireReload(config_file)) {
        return false;
    }

    CfgParser cfg;

    _config_file = config_file;
    bool success = tryHardLoadConfig(cfg, _config_file);

    // Make sure config is reloaded next time as content is dynamically
    // generated from the configuration file.
    if (! success || cfg.isDynamicContent()) {
        _cfg_files.clear();
    } else {
        _cfg_files = cfg.getCfgFiles();
    }

    if (! success) {
        USER_WARN("unable to load configuration files");
        return false;
    }

    // Update PEKWM_CONFIG_FILE environment if needed (to reflect active file)
    auto cfg_env = Util::getEnv("PEKWM_CONFIG_FILE");
    if (cfg_env.size() == 0 || _config_file.compare(cfg_env) != 0) {
        setenv("PEKWM_CONFIG_FILE", _config_file.c_str(), 1);
    }

    std::string o_file_mouse; // temporary filepath for mouseconfig

    CfgParser::Entry *section;

    // Get other config files dests.
    section = cfg.getEntryRoot()->findSection("FILES");
    if (section) {
        loadFiles(section);
    }

    // Parse moving / resizing options.
    section = cfg.getEntryRoot()->findSection("MOVERESIZE");
    if (section) {
        loadMoveResize(section);
    }

    // Screen, important stuff such as number of workspaces
    section = cfg.getEntryRoot()->findSection("SCREEN");
    if (section) {
        loadScreen(section);
    }

    section = cfg.getEntryRoot()->findSection("MENU");
    if (section) {
        loadMenu(section);
    }

    section = cfg.getEntryRoot()->findSection("CMDDIALOG");
    if (section) {
      loadCmdDialog(section);
    }

    section = cfg.getEntryRoot()->findSection("HARBOUR");
    if (section) {
        loadHarbour(section);
    }

    return true;
}

//! @brief Loads file section of configuration
//! @param section Pointer to FILES section.
void
Config::loadFiles(CfgParser::Entry *section)
{
    if (! section) {
        return;
    }

    std::vector<CfgParserKey*> keys;
    keys.push_back(new CfgParserKeyPath("KEYS", _files_keys,
                                        SYSCONFDIR "/keys"));
    keys.push_back(new CfgParserKeyPath("MOUSE", _files_mouse,
                                        SYSCONFDIR "/mouse"));
    keys.push_back(new CfgParserKeyPath("MENU", _files_menu,
                                        SYSCONFDIR "/menu"));
    keys.push_back(new CfgParserKeyPath("START", _files_start,
                                        SYSCONFDIR "/start"));
    keys.push_back(new CfgParserKeyPath("AUTOPROPS", _files_autoprops,
                                        SYSCONFDIR "/autoproperties"));
    keys.push_back(new CfgParserKeyPath("THEME", _files_theme,
                                        DATADIR "/pekwm/themes/default/theme"));
    keys.push_back(new CfgParserKeyString("THEMEVARIANT",
                                          _files_theme_variant));
    keys.push_back(new CfgParserKeyPath("ICONS", _files_icon_path,
                                        DATADIR "/pekwm/icons"));

    // Parse
    section->parseKeyValues(keys.begin(), keys.end());

    // Free up resources
    for_each(keys.begin(), keys.end(), Util::Free<CfgParserKey*>());
}

//! @brief Loads MOVERESIZE section of main configuration
//! @param section Pointer to MOVERESIZE section.
void
Config::loadMoveResize(CfgParser::Entry *section)
{
    if (! section) {
        return;
    }

    std::vector<CfgParserKey*> keys;
    keys.push_back(new CfgParserKeyNumeric<int>("EDGEATTRACT",
                                                _moveresize_edgeattract, 0, 0));
    keys.push_back(new CfgParserKeyNumeric<int>("EDGERESIST",
                                                _moveresize_edgeresist, 0, 0));
    keys.push_back(new CfgParserKeyNumeric<int>("WINDOWATTRACT",
                                                _moveresize_woattract, 0, 0));
    keys.push_back(new CfgParserKeyNumeric<int>("WINDOWRESIST",
                                                _moveresize_woresist, 0, 0));
    keys.push_back(new CfgParserKeyBool("OPAQUEMOVE",
                                        _moveresize_opaquemove));
    keys.push_back(new CfgParserKeyBool("OPAQUERESIZE",
                                        _moveresize_opaqueresize));

    // Parse data
    section->parseKeyValues(keys.begin(), keys.end());

    // Free up resources
    for_each(keys.begin(), keys.end(), Util::Free<CfgParserKey*>());
}

//! @brief Loads SCREEN section of main configuration
//! @param section Pointer to SCREEN section.
void
Config::loadScreen(CfgParser::Entry *section)
{
    if (! section) {
        return;
    }

    // Parse data
    std::string edge_size, workspace_names, trim_title, curr_head_selector;
    CfgParser::Entry *value;

    std::vector<CfgParserKey*> keys;
    keys.push_back(new CfgParserKeyBool("THEMEBACKGROUND",
                                        _screen_theme_background, true));
    keys.push_back(new CfgParserKeyNumeric<int>("WORKSPACES",
                                                _screen_workspaces, 4, 1));
    keys.push_back(new CfgParserKeyNumeric<int>("WORKSPACESPERROW",
                                                _screen_workspaces_per_row,
                                                0, 0));
    keys.push_back(new CfgParserKeyString("WORKSPACENAMES", workspace_names));
    keys.push_back(new CfgParserKeyString("EDGESIZE", edge_size));
    keys.push_back(new CfgParserKeyBool("EDGEINDENT", _screen_edge_indent));
    keys.push_back(new CfgParserKeyNumeric<int>("DOUBLECLICKTIME",
                                                _screen_doubleclicktime,
                                                250, 0));
    keys.push_back(new CfgParserKeyString("TRIMTITLE", trim_title));
    keys.push_back(new CfgParserKeyBool("FULLSCREENABOVE",
                                        _screen_fullscreen_above, true));
    keys.push_back(new CfgParserKeyBool("FULLSCREENDETECT",
                                        _screen_fullscreen_detect, true));
    keys.push_back(new CfgParserKeyBool("SHOWFRAMELIST",
                                        _screen_showframelist));
    keys.push_back(new CfgParserKeyBool("SHOWSTATUSWINDOW",
                                        _screen_show_status_window));
    keys.push_back(new CfgParserKeyBool("SHOWSTATUSWINDOWCENTEREDONROOT",
                                        _screen_show_status_window_on_root,
                                        false));
    keys.push_back(new CfgParserKeyBool("SHOWCLIENTID",
                                        _screen_show_client_id));
    keys.push_back(new CfgParserKeyNumeric<int>("SHOWWORKSPACEINDICATOR",
                                              _screen_show_workspace_indicator,
                                                500, 0));
    keys.push_back(new CfgParserKeyNumeric<int>("WORKSPACEINDICATORSCALE",
                                             _screen_workspace_indicator_scale,
                                                16, 2));
    keys.push_back(new CfgParserKeyNumeric<uint>("WORKSPACEINDICATOROPACITY",
                                          _screen_workspace_indicator_opacity,
                                                 100, 0, 100));
    keys.push_back(new CfgParserKeyBool("PLACENEW", _screen_place_new));
    keys.push_back(new CfgParserKeyBool("FOCUSNEW", _screen_focus_new));
    keys.push_back(new CfgParserKeyBool("FOCUSNEWCHILD",
                                        _screen_focus_new_child, true));
    keys.push_back(new CfgParserKeyNumeric<uint>("FOCUSSTEALPROTECT",
                                                 _screen_focus_steal_protect,
                                                 0));
    keys.push_back(new CfgParserKeyBool("HONOURRANDR",
                                        _screen_honour_randr, true));
    keys.push_back(new CfgParserKeyBool("HONOURASPECTRATIO",
                                        _screen_honour_aspectratio, true));
    keys.push_back(new CfgParserKeyString("CURRHEADSELECTOR",
                                          curr_head_selector, "CURSOR"));
    keys.push_back(new CfgParserKeyBool("REPORTALLCLIENTS",
                                        _screen_report_all_clients, false));

    // Parse data
    section->parseKeyValues(keys.begin(), keys.end());

    // Free up resources
    for_each(keys.begin(), keys.end(), Util::Free<CfgParserKey*>());
    keys.clear();

    PFont::setTrimString(trim_title);

    // Convert opacity from percent to absolute value
    CONV_OPACITY(_screen_workspace_indicator_opacity);

    int edge_size_all = 0;
    _screen_edge_sizes.clear();
    if (edge_size.size()) {
        std::vector<std::string> sizes;
        if (Util::splitString(edge_size, sizes, " \t", 4) == 4) {
            for (auto it : sizes) {
                _screen_edge_sizes.push_back(strtol(it.c_str(), 0, 10));
            }
        } else {
            edge_size_all = strtol(edge_size.c_str(), 0, 10);
        }
    }

    for (uint i = 0; i < SCREEN_EDGE_NO; ++i) {
        _screen_edge_sizes.push_back(edge_size_all);
    }
    // Add SCREEN_EDGE_NO to the list for safety
    _screen_edge_sizes.push_back(0);

    // Workspace names
    _screen_workspace_names.clear();

    std::vector<std::string> vs;
    if (Util::splitString(workspace_names, vs, ";", 0, true)) {
        for (auto vs_it : vs) {
            _screen_workspace_names.push_back(Charset::to_wide_str(vs_it));
        }
    }

    _screen_curr_head_selector = curr_head_selector_map.get(curr_head_selector);

    auto sub = section->findSection("PLACEMENT");
    if (sub) {
        value = sub->findEntry("MODEL");
        if (value) {
            Workspace::setDefaultLayouter(value->getValue());
        }

        CfgParser::Entry *sub_2 = sub->findSection("SMART");
        if (sub_2) {
            keys.push_back(new CfgParserKeyBool("ROW", _screen_placement_row));
            keys.push_back(new CfgParserKeyBool("LEFTTORIGHT", _screen_placement_ltr));
            keys.push_back(new CfgParserKeyBool("TOPTOBOTTOM", _screen_placement_ttb));
            keys.push_back(new CfgParserKeyNumeric<int>("OFFSETX", _screen_placement_offset_x, 0, 0));
            keys.push_back(new CfgParserKeyNumeric<int>("OFFSETY", _screen_placement_offset_y, 0, 0));

            // Do the parsing
            sub_2->parseKeyValues(keys.begin(), keys.end());

            // Freeup resources
            for_each(keys.begin(), keys.end(), Util::Free<CfgParserKey*>());
            keys.clear();
        }

        keys.push_back(new CfgParserKeyBool("TRANSIENTONPARENT", _place_trans_parent, true));
        sub->parseKeyValues(keys.begin(), keys.end());
        for_each(keys.begin(), keys.end(), Util::Free<CfgParserKey*>());
        keys.clear();
    }

    sub = section->findSection("UNIQUENAMES");
    if (sub) {
        keys.push_back(new CfgParserKeyBool("SETUNIQUE", _screen_client_unique_name));
        keys.push_back(new CfgParserKeyString("PRE", _screen_client_unique_name_pre));
        keys.push_back(new CfgParserKeyString("POST", _screen_client_unique_name_post));

        // Parse data
        sub->parseKeyValues(keys.begin(), keys.end());

        // Free up resources
        for_each(keys.begin(), keys.end(), Util::Free<CfgParserKey*>());
        keys.clear();
    }
}

//! @brief Loads the MENU section of the main configuration
//! @param section Pointer to MENU section
void
Config::loadMenu(CfgParser::Entry *section)
{
    if (! section) {
        return;
    }

    std::vector<CfgParserKey*> keys;
    std::string value_select, value_enter, value_exec;

    keys.push_back(new CfgParserKeyString("SELECT", value_select, "MOTION", 0));
    keys.push_back(new CfgParserKeyString("ENTER", value_enter,
                                          "BUTTONPRESS", 0));
    keys.push_back(new CfgParserKeyString("EXEC", value_exec,
                                          "BUTTONRELEASE", 0));
    keys.push_back(new CfgParserKeyBool("DISPLAYICONS", _menu_display_icons,
                                        true));
    keys.push_back(new CfgParserKeyNumeric<uint>("FOCUSOPACITY",
                                                 _menu_focus_opacity,
                                                 100, 0, 100));
    keys.push_back(new CfgParserKeyNumeric<uint>("UNFOCUSOPACITY",
                                                 _menu_unfocus_opacity,
                                                 100, 0, 100));

    // Parse data
    section->parseKeyValues(keys.begin(), keys.end());

    _menu_select_mask = getMenuMask(value_select);
    _menu_enter_mask = getMenuMask(value_enter);
    _menu_exec_mask = getMenuMask(value_exec);

    // Free up resources
    for_each(keys.begin(), keys.end(), Util::Free<CfgParserKey*>());
    keys.clear();

    // Parse icon size limits
    CfgParser::iterator it(section->begin());
    for (; it != section->end(); ++it) {
        if (*(*it) == "ICONS") {
            loadMenuIcons((*it)->getSection());
        }
    }

    // Convert opacity from percent to absolute value
    CONV_OPACITY(_menu_focus_opacity);
    CONV_OPACITY(_menu_unfocus_opacity);
}

/**
 * Load Icon size limits for menu.
 */
void
Config::loadMenuIcons(CfgParser::Entry *section)
{
    if (! section || ! section->getValue().size()) {
        return;
    }

    std::vector<CfgParserKey*> keys;
    std::string minimum, maximum;

    keys.push_back(new CfgParserKeyString("MINIMUM", minimum, "16x16", 3));
    keys.push_back(new CfgParserKeyString("MAXIMUM", maximum, "16x16", 3));

    // Parse data
    section->parseKeyValues(keys.begin(), keys.end());

    for_each(keys.begin(), keys.end(), Util::Free<CfgParserKey*>());

    SizeLimits limits;
    if (limits.parse(minimum, maximum)) {
        _menu_icon_limits[section->getValue()] = limits;
    }
}

/**
 * Load configuration from CmdDialog section.
 */
void
Config::loadCmdDialog(CfgParser::Entry *section)
{
    if (! section) {
        return;
    }

    std::vector<CfgParserKey*> keys;

    keys.push_back(new CfgParserKeyBool("HISTORYUNIQUE",
                                        _cmd_dialog_history_unique));
    keys.push_back(new CfgParserKeyNumeric<int>("HISTORYSIZE",
                                                _cmd_dialog_history_size,
                                                1024, 1));
    keys.push_back(new CfgParserKeyPath("HISTORYFILE",
                                        _cmd_dialog_history_file,
                                        "~/.pekwm/history"));
    keys.push_back(new CfgParserKeyNumeric<int>("HISTORYSAVEINTERVAL",
                                              _cmd_dialog_history_save_interval,
                                                16, 0));

    section->parseKeyValues(keys.begin(), keys.end());

    for_each(keys.begin(), keys.end(), Util::Free<CfgParserKey*>());
}

//! @brief Loads the HARBOUR section of the main configuration
void
Config::loadHarbour(CfgParser::Entry *section)
{
    if (! section) {
        return;
    }

    std::vector<CfgParserKey*> keys;
    std::string value_placement, value_orientation;

    keys.push_back(new CfgParserKeyBool("ONTOP", _harbour_ontop, true));
    keys.push_back(new CfgParserKeyBool("MAXIMIZEOVER",
                                        _harbour_maximize_over, false));
    keys.push_back(new CfgParserKeyNumeric<int>("HEAD",
                                                _harbour_head_nr, 0, 0));
    keys.push_back(new CfgParserKeyString("PLACEMENT",
                                          value_placement, "RIGHT", 0));
    keys.push_back(new CfgParserKeyString("ORIENTATION",
                                          value_orientation, "TOPTOBOTTOM", 0));
    keys.push_back(new CfgParserKeyNumeric<uint>("OPACITY",
                                                 _harbour_opacity,
                                                 100, 0, 100));

    // Parse data
    section->parseKeyValues(keys.begin(), keys.end());

    // Free up resources
    for_each(keys.begin(), keys.end(), Util::Free<CfgParserKey*>());
    keys.clear();

    // Convert opacity from percent to absolute value
    CONV_OPACITY(_harbour_opacity);

    _harbour_placement = harbour_placement_map.get(value_placement);
    _harbour_orientation = harbour_orientation_map.get(value_orientation);
    if (_harbour_placement == NO_HARBOUR_PLACEMENT) {
        _harbour_placement = RIGHT;
    }
    if (_harbour_orientation == NO_ORIENTATION) {
        _harbour_orientation = TOP_TO_BOTTOM;
    }

    CfgParser::Entry *sub = section->findSection("DOCKAPP");
    if (sub) {
        keys.push_back(new CfgParserKeyNumeric<int>("SIDEMIN",
                                                    _harbour_da_min_s, 64, 0));
        keys.push_back(new CfgParserKeyNumeric<int>("SIDEMAX",
                                                    _harbour_da_max_s, 64, 0));

        // Parse data
        sub->parseKeyValues(keys.begin(), keys.end());

        // Free up resources
        for_each(keys.begin(), keys.end(), Util::Free<CfgParserKey*>());
        keys.clear();
    }
}

ActionAccessMask
Config::getActionAccessMask(const std::string &name)
{
    return action_access_mask_map.get(name);
}

bool
Config::parseActionAccessMask(const std::string &action_mask, uint &mask)
{
    mask = ACTION_ACCESS_NO;

    std::vector<std::string> tok;
    if (Util::splitString(action_mask, tok, " \t")) {
        for (auto it : tok) {
            mask |= getActionAccessMask(it);
        }
    }

    return true;
}

bool
Config::parseMoveResizeAction(const std::string &action_string, Action &action)
{
    std::vector<std::string> tok;

    // Chop the string up separating the actions.
    if (Util::splitString(action_string, tok, " \t", 2)) {
        action.setAction(moveresize_map.get(tok[0]));
        if (action.getAction() != NO_MOVERESIZE_ACTION) {
            if (tok.size() == 2) { // we got enough tok for a paremeter
                switch (action.getAction()) {
                case MOVE_HORIZONTAL:
                case MOVE_VERTICAL:
                case RESIZE_HORIZONTAL:
                case RESIZE_VERTICAL: {
                    char *endptr = nullptr;
                    action.setParamI(0, strtol(tok[1].c_str(), &endptr, 10));
                    if (endptr && *endptr == '%') {
                        action.setParamI(1, UNIT_PERCENT);
                    } else {
                        action.setParamI(1, UNIT_PIXEL);
                    }
                    break;
                }
                case MOVE_SNAP:
                default:
                    // Do nothing.
                    break;
                }
            }

            return true;
        } else {
            USER_WARN("Unknown move/resize action: " << tok[0]);
        }
    }

    return false;
}

bool
Config::parseMoveResizeActions(const std::string &action_string,
                               ActionEvent& ae)
{
    std::vector<std::string> tok;
    Action action;

    // reset the action event
    ae.action_list.clear();

    // chop the string up separating the actions
    if (Util::splitString(action_string, tok, ";")) {
        for (auto it : tok) {
            if (parseMoveResizeAction(it, action)) {
                ae.action_list.push_back(action);
                action.clear();
            }
        }

        return true;
    }

    return false;
}

//! @brief Parses MoveResize Event.
bool
Config::parseMoveResizeEvent(CfgParser::Entry *section, ActionEvent& ae)
{
    CfgParser::Entry *value;

    if (! section->getValue().size ()) {
        return false;
    }

    if (ActionConfig::parseKey(section->getValue(), ae.mod, ae.sym)) {
        value = section->getSection()->findEntry("ACTIONS");
        if (value) {
            return parseMoveResizeActions(value->getValue(), ae);
        }
    }

    return false;
}

bool
Config::parseInputDialogAction(const std::string &val, Action &action)
{
    action.setAction(inputdialog_map.get(val));
    return (action.getAction() != INPUT_NO_ACTION);
}

bool
Config::parseInputDialogActions(const std::string &actions, ActionEvent &ae)
{
    std::vector<std::string> tok;
    Action action;
    std::string::size_type first, last;

    // reset the action event
    ae.action_list.clear();

    // chop the string up separating the actions
    if (Util::splitString(actions, tok, ";")) {
        for (auto it : tok) {
            first = it.find_first_not_of(" \t\n");
            if (first == std::string::npos) {
                continue;
            }
            last = it.find_last_not_of(" \t\n");
            auto name = it.substr(first, last-first+1);
            if (parseInputDialogAction(name, action)) {
                ae.action_list.push_back(action);
                action.clear();
            }
        }

        return true;
    }

    return false;

}

//! @brief Parses InputDialog Event.
bool
Config::parseInputDialogEvent(CfgParser::Entry *section, ActionEvent &ae)
{
    CfgParser::Entry *value;

    if (! section->getValue().size()) {
        return false;
    }

    if (ActionConfig::parseKey(section->getValue(), ae.mod, ae.sym)) {
        value = section->getSection()->findEntry("ACTIONS");
        if (value) {
            return parseInputDialogActions(value->getValue(), ae);
        }
    }

    return false;
}

/**
 * Get mask for handling menu events.
 */
uint
Config::getMenuMask(const std::string &mask)
{
    uint mask_return = 0, val;

    std::vector<std::string> tok;
    Util::splitString(mask, tok, " \t");

    for (auto it : tok) {
        val = mouse_event_map.get(it);
        if (val != MOUSE_EVENT_NO) {
            mask_return |= val;
        }
    }
    return mask_return;
}

unsigned int
Config::getMenuIconLimit(unsigned int value, SizeLimitType limit,
                         const std::string &name) const
{
    unsigned int limit_val = 0;
    auto it(_menu_icon_limits.find(name));
    if (it == _menu_icon_limits.end()) {
        if (name == "DEFAULT") {
            limit_val = 16;
        } else {
            limit_val = getMenuIconLimit(value, limit, "DEFAULT");
        }
    } else {
        limit_val = it->second.get(limit);
    }

    return limit_val ? limit_val : value;
}

bool
Config::parseMenuAction(const std::string &action_string, Action &action)
{
    std::vector<std::string> tok;

    // chop the string up separating the actions
    if (Util::splitString(action_string, tok, " \t", 2)) {
        action.setAction(menu_action_map.get(tok[0]));
        if (action.getAction() != ACTION_NO) {
            return true;
        }
    }

    return false;
}

bool
Config::parseMenuActions(const std::string &actions, ActionEvent &ae)
{
    std::vector<std::string> tok;
    Action action;

    // reset the action event
    ae.action_list.clear();

    // chop the string up separating the actions
    if (Util::splitString(actions, tok, ";", 0, false, '\\')) {
        for (auto it : tok) {
            if (parseMenuAction(it, action)) {
                ae.action_list.push_back(action);
                action.clear();
            }
        }

        return true;
    }

    return false;
}

//! @brief Parses MenuEvent.
bool
Config::parseMenuEvent(CfgParser::Entry *section, ActionEvent& ae)
{
    CfgParser::Entry *value;

    if (! section->getValue().size()) {
        return false;
    }

    if (ActionConfig::parseKey(section->getValue(), ae.mod, ae.sym)) {
        value = section->getSection()->findEntry("ACTIONS");
        if (value) {
            return parseMenuActions(value->getValue(), ae);
        }
    }

    return false;
}

/**
 * Load main configuration file, priority as follows:
 *
 *   1. Load command line specified file.
 *   2. Load ~/.pekwm/config
 *   3. Copy configuration and load ~/.pekwm/config
 *   4. Load system configuration
 */
bool
Config::tryHardLoadConfig(CfgParser &cfg, std::string &file)
{
    bool success = false;

    // Try loading command line specified file.
    if (file.size()) {
        success = cfg.parse(file, CfgParserSource::SOURCE_FILE, true);
    }

    // Try loading ~/.pekwm/config
    if (! success) {
        file = Util::getEnv("HOME") + "/.pekwm/config";
        success = cfg.parse(file, CfgParserSource::SOURCE_FILE, true);

        // Copy cfg files to ~/.pekwm and try loading ~/.pekwm/config again.
        if (! success) {
            copyConfigFiles();
            success = cfg.parse(file, CfgParserSource::SOURCE_FILE, true);
        }
    }

    // Try loading system configuration files.
    if (! success) {
        file = std::string(SYSCONFDIR "/config");
        success = cfg.parse(file, CfgParserSource::SOURCE_FILE, true);
    }

    return success;
}

//! @brief Populates the ~/.pekwm/ dir with config files
void
Config::copyConfigFiles(void)
{
    std::string cfg_dir = Util::getEnv("HOME") + "/.pekwm";

    std::string cfg_file = cfg_dir + std::string("/config");
    std::string keys_file = cfg_dir + std::string("/keys");
    std::string mouse_file = cfg_dir + std::string("/mouse");
    std::string menu_file = cfg_dir + std::string("/menu");
    std::string autoprops_file = cfg_dir + std::string("/autoproperties");
    std::string start_file = cfg_dir + std::string("/start");
    std::string vars_file = cfg_dir + std::string("/vars");
    std::string themes_dir = cfg_dir + std::string("/themes");

    bool cp_config, cp_keys, cp_mouse, cp_menu;
    bool cp_autoprops, cp_start, cp_vars;
    bool make_themes = false;
    cp_config = cp_keys = cp_mouse = cp_menu = false;
    cp_autoprops = cp_start = cp_vars = false;

    struct stat stat_buf;
    // check and see if we already have a ~/.pekwm/ directory
    if (stat(cfg_dir.c_str(), &stat_buf) == 0) {
        // is it a dir or file?
        if (! S_ISDIR(stat_buf.st_mode)) {
            USER_WARN(cfg_dir << " already exists and is not a directory."
                      << " can not copy config files");
            return;
        }

        // we already have a directory, see if it's writeable and executable
        bool cfg_dir_ok = false;

        if (getuid() == stat_buf.st_uid) {
            if ((stat_buf.st_mode&S_IWUSR) && (stat_buf.st_mode&(S_IXUSR))) {
                cfg_dir_ok = true;
            }
        }
        if (! cfg_dir_ok) {
            if (getgid() == stat_buf.st_gid) {
                if ((stat_buf.st_mode&S_IWGRP)
                    && (stat_buf.st_mode&(S_IXGRP))) {
                    cfg_dir_ok = true;
                }
            }
        }

        if (! cfg_dir_ok) {
            if (! (stat_buf.st_mode&S_IWOTH)
                || ! (stat_buf.st_mode&(S_IXOTH))) {
                USER_WARN("write access missing to " << cfg_dir << " directory."
                          << " unable to copy the config files");
                return;
            }
        }

        // we apparently could write and exec that dir, now see if we have any
        // files in it
        if (stat(cfg_file.c_str(), &stat_buf))
            cp_config = true;
        if (stat(keys_file.c_str(), &stat_buf))
            cp_keys = true;
        if (stat(mouse_file.c_str(), &stat_buf))
            cp_mouse = true;
        if (stat(menu_file.c_str(), &stat_buf))
            cp_menu = true;
        if (stat(autoprops_file.c_str(), &stat_buf))
            cp_autoprops = true;
        if (stat(start_file.c_str(), &stat_buf))
            cp_start = true;
        if (stat(vars_file.c_str(), &stat_buf))
            cp_vars = true;
        if (stat(themes_dir.c_str(), &stat_buf)) {
            make_themes = true;
        }
    } else { // we didn't have a ~/.pekwm directory already, lets create one
        if (mkdir(cfg_dir.c_str(), 0700)) {
            USER_WARN("can not create the directory " << cfg_dir
                      << ". can not copy config files");
            return;
        }

        cp_config = cp_keys = cp_mouse = cp_menu = true;
        cp_autoprops = cp_start = cp_vars = true;
        make_themes = true;
    }

    if (cp_config) {
        Util::copyTextFile(SYSCONFDIR "/config", cfg_file);
    }
    if (cp_keys) {
        Util::copyTextFile(SYSCONFDIR "/keys", keys_file);
    }
    if (cp_mouse) {
        Util::copyTextFile(SYSCONFDIR "/mouse", mouse_file);
    }
    if (cp_menu) {
        Util::copyTextFile(SYSCONFDIR "/menu", menu_file);
    }
    if (cp_autoprops) {
        Util::copyTextFile(SYSCONFDIR "/autoproperties", autoprops_file);
    }
    if (cp_start) {
        Util::copyTextFile(SYSCONFDIR "/start", start_file);
    }
    if (cp_vars) {
        Util::copyTextFile(SYSCONFDIR "/vars", vars_file);
    }
    if (make_themes) {
        mkdir(themes_dir.c_str(), 0700);
    }
}

/**
 * Parses mouse configuration file.
 */
bool
Config::loadMouseConfig(const std::string &mouse_file)
{
    if (! _cfg_files_mouse.requireReload(mouse_file)) {
        return false;
    }

    CfgParser mouse_cfg;
    if (! mouse_cfg.parse(mouse_file, CfgParserSource::SOURCE_FILE, true)
        && ! mouse_cfg.parse(SYSCONFDIR "/mouse",
                             CfgParserSource::SOURCE_FILE, true)) {
        _cfg_files_mouse.clear();
        return false;
    }

    if (mouse_cfg.isDynamicContent()) {
        _cfg_files_mouse.clear();
    } else {
        _cfg_files_mouse = mouse_cfg.getCfgFiles();
    }

    // Make sure old actions get unloaded.
    for (auto it : _mouse_action_map) {
        it.second->clear();
    }

    CfgParser::Entry *section;

    section = mouse_cfg.getEntryRoot()->findSection("FRAMETITLE");
    if (section) {
        parseButtons(section, _mouse_action_map[MOUSE_ACTION_LIST_TITLE_FRAME],
                     nullptr, FRAME_OK);
    }

    section = mouse_cfg.getEntryRoot()->findSection("OTHERTITLE");
    if (section) {
        parseButtons(section, _mouse_action_map[MOUSE_ACTION_LIST_TITLE_OTHER],
                     nullptr, FRAME_OK);
    }

    section = mouse_cfg.getEntryRoot()->findSection("CLIENT");
    if (section) {
        parseButtons(section, _mouse_action_map[MOUSE_ACTION_LIST_CHILD_FRAME],
                     &_client_mouse_action_buttons, CLIENT_OK);
    }

    section = mouse_cfg.getEntryRoot()->findSection("ROOT");
    if (section) {
        parseButtons(section, _mouse_action_map[MOUSE_ACTION_LIST_ROOT],
                     nullptr, ROOTCLICK_OK);
    }

    section = mouse_cfg.getEntryRoot()->findSection("MENU");
    if (section) {
        parseButtons(section, _mouse_action_map[MOUSE_ACTION_LIST_MENU],
                     nullptr, FRAME_OK);
    }

    section = mouse_cfg.getEntryRoot()->findSection("OTHER");
    if (section) {
        parseButtons(section, _mouse_action_map[MOUSE_ACTION_LIST_OTHER],
                     nullptr, FRAME_OK);
    }

    section = mouse_cfg.getEntryRoot()->findSection("SCREENEDGE");
    if (section) {
        CfgParser::iterator edge_it(section->begin());
        for (; edge_it != section->end(); ++edge_it)  {
            uint pos = ActionConfig::getDirection((*edge_it)->getName());

            if (pos != SCREEN_EDGE_NO) {
                parseButtons((*edge_it)->getSection(),
                             getEdgeListFromPosition(pos), nullptr,
                             SCREEN_EDGE_OK);
            }
        }
    }

    section = mouse_cfg.getEntryRoot()->findSection("BORDER");
    if (section) {
        CfgParser::iterator border_it(section->begin());
        for (; border_it != section->end(); ++border_it) {
            uint pos = ActionConfig::getBorderPos((*border_it)->getName());
            if (pos != BORDER_NO_POS) {
                parseButtons((*border_it)->getSection(),
                             getBorderListFromPosition(pos), nullptr,
                             FRAME_BORDER_OK);
            }
        }
    }

    return true;
}

static void
mouseButtonsAdd(std::vector<BoundButton>* buttons, uint button, uint mod)
{
    if (buttons == nullptr) {
        return;
    }

    auto bit = buttons->begin();
    for (; bit != buttons->end(); ++bit) {
        if (bit->button == button) {
            for (auto mit : bit->mods) {
                if (mit == mod) {
                    return;
                }
            }
            bit->mods.push_back(mod);
            return;
        }
    }
    buttons->push_back({button, {mod}});
}

/**
 * Parses mouse config section, like FRAME
 */
void
Config::parseButtons(CfgParser::Entry *section,
                     std::vector<ActionEvent>* mouse_list,
                     std::vector<BoundButton>* mouse_buttons,
                     ActionOk action_ok)
{
    if (! section || ! mouse_list) {
        return;
    }

    ActionEvent ae;
    CfgParser::Entry *value;

    CfgParser::iterator it(section->begin());
    for (; it != section->end(); ++it) {
        if (! (*it)->getSection()) {
            continue;
        }

        ae.type = mouse_event_map.get((*it)->getName());
        if (ae.type == MOUSE_EVENT_NO) {
            continue;
        }

        if (ae.type == MOUSE_EVENT_MOTION) {
            value = (*it)->getSection()->findEntry("THRESHOLD");
            if (value) {
                ae.threshold = strtol(value->getValue().c_str(), 0, 10);
            } else {
                ae.threshold = 0;
            }
        }

        if (ActionConfig::parseActionEvent((*it), ae, action_ok, true)) {
            mouse_list->push_back(ae);
            mouseButtonsAdd(mouse_buttons, ae.sym, ae.mod);
        }
    }
}

// frame border configuration

std::vector<ActionEvent>*
Config::getBorderListFromPosition(uint pos)
{
    std::vector<ActionEvent> *ret = 0;

    switch (pos) {
    case BORDER_TOP_LEFT:
        ret = _mouse_action_map[MOUSE_ACTION_LIST_BORDER_TL];
        break;
    case BORDER_TOP:
        ret = _mouse_action_map[MOUSE_ACTION_LIST_BORDER_T];
        break;
    case BORDER_TOP_RIGHT:
        ret = _mouse_action_map[MOUSE_ACTION_LIST_BORDER_TR];
        break;
    case BORDER_LEFT:
        ret = _mouse_action_map[MOUSE_ACTION_LIST_BORDER_L];
        break;
    case BORDER_RIGHT:
        ret = _mouse_action_map[MOUSE_ACTION_LIST_BORDER_R];
        break;
    case BORDER_BOTTOM_LEFT:
        ret = _mouse_action_map[MOUSE_ACTION_LIST_BORDER_BL];
        break;
    case BORDER_BOTTOM:
        ret = _mouse_action_map[MOUSE_ACTION_LIST_BORDER_B];
        break;
    case BORDER_BOTTOM_RIGHT:
        ret = _mouse_action_map[MOUSE_ACTION_LIST_BORDER_BR];
        break;
    }

    return ret;
}

std::vector<ActionEvent>*
Config::getEdgeListFromPosition(uint pos)
{
    std::vector<ActionEvent> *ret = 0;

    switch (pos) {
    case SCREEN_EDGE_TOP:
        ret = _mouse_action_map[MOUSE_ACTION_LIST_EDGE_T];
        break;
    case SCREEN_EDGE_BOTTOM:
        ret = _mouse_action_map[MOUSE_ACTION_LIST_EDGE_B];
        break;
    case SCREEN_EDGE_LEFT:
        ret = _mouse_action_map[MOUSE_ACTION_LIST_EDGE_L];
        break;
    case SCREEN_EDGE_RIGHT:
        ret = _mouse_action_map[MOUSE_ACTION_LIST_EDGE_R];
        break;
    };

    return ret;
}

//! @brief Parses a string which contains two opacity values
bool
Config::parseOpacity(const std::string value, uint &focused, uint &unfocused)
{
    std::vector<std::string> tokens;
    switch ((Util::splitString(value, tokens, " ,", 2))) {
    case 2:
        focused = std::atoi(tokens.at(0).c_str());
        unfocused = std::atoi(tokens.at(1).c_str());
        break;
    case 1:
        focused = unfocused = std::atoi(tokens.at(0).c_str());
        break;
    default:
        return false;
    }
    CONV_OPACITY(focused);
    CONV_OPACITY(unfocused);
    return true;
}
