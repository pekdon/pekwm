//
// Config.hh for pekwm
// Copyright (C) 2002-2021 the pekwm development team
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#pragma once

#include "config.h"

#include "pekwm.hh"
#include "Action.hh"
#include "CfgParser.hh"

#include <string>
#include <map>
#include <utility>

/**
 * Enum describing the different limits of a size limit.
 */
enum SizeLimitType {
    WIDTH_MIN = 0,
    WIDTH_MAX,
    HEIGHT_MIN,
    HEIGHT_MAX
};

/**
 * Simple class describing size limitations, width and height min and
 * max sizes.
 */
class SizeLimits
{
public:
    /** SizeLimits constructor setting limits to 0. */
    SizeLimits(void) {
        for (unsigned int i = 0; i < HEIGHT_MAX; ++i) {
            _limits[i] = 0;
        }
    }

    /** Get limit for limit type. */
    unsigned int get(SizeLimitType limit) const { return _limits[limit]; }
    bool parse(const std::string &minimum, const std::string &maximum);

private:
    bool parseLimit(const std::string &limit,
                    unsigned int &min, unsigned int &max);

private:
    unsigned int _limits[HEIGHT_MAX + 1]; /**< Limits. */
};

// CONV_OPACITY converts percentage to absolute opacity values.
// The variable X containing the percent value is changed directly.
#define CONV_OPACITY(X)\
    X = (X == 100)?EWMH_OPAQUE_WINDOW:X*(EWMH_OPAQUE_WINDOW/100)

struct BoundButton {
    uint button;
    std::vector<uint> mods;
};

/**
 * Large set of configuration options stored and parsed by the
 * singleton Config class.
 */
class Config
{
public:
    Config(void);
    ~Config(void);

    bool load(const std::string &config_file);
    bool loadMouseConfig(const std::string &mouse_file);

    inline const std::string &getConfigFile(void) const { return _config_file; }

    // Files
    const std::string &getKeyFile(void) const { return _files_keys; }
    const std::string &getMenuFile(void) const { return _files_menu; }
    const std::string &getStartFile(void) const { return _files_start; }
    const std::string &getAutoPropsFile(void) const { return _files_autoprops; }
    const std::string &getThemeFile(void) const { return _files_theme; }
    const std::string &getThemeVariant(void) const {
        return _files_theme_variant;
    }
    const std::string &getMouseConfigFile(void) const { return _files_mouse; }
    const std::string &getIconPath(void) const { return _files_icon_path; }
    const char *getSystemIconPath(void) const {
        return DATADIR "/pekwm/icons/";
    }

    // Moveresize
    inline int getEdgeAttract(void) const { return _moveresize_edgeattract; }
    inline int getEdgeResist(void) const { return _moveresize_edgeresist; }
    inline int getWOAttract(void) const { return _moveresize_woattract; }
    inline int getWOResist(void) const { return _moveresize_woresist; }
    inline bool getOpaqueMove(void) const { return _moveresize_opaquemove; }
    inline bool getOpaqueResize(void) const { return _moveresize_opaqueresize; }

    // Screen
    bool getThemeBackground(void) const { return _screen_theme_background; }
    int getWorkspaces(void) const { return _screen_workspaces; }
    int getWorkspacesPerRow(void) const { return _screen_workspaces_per_row; }
    void getDesktopNamesUTF8(uchar **names, uint *length) const;
    const std::wstring &getWorkspaceName(uint num) const {
        return (num >= _screen_workspace_names.size())
            ? _screen_workspace_name_default
            : _screen_workspace_names[num];
    }
    void setDesktopNamesUTF8(char *names, ulong length);

    int getScreenEdgeSize(EdgeType edge) const {
        return _screen_edge_sizes[edge];
    }
    bool getScreenEdgeIndent(void) const { return _screen_edge_indent; }
    int getDoubleClickTime(void) const { return _screen_doubleclicktime; }

    bool isFullscreenAbove(void) const { return _screen_fullscreen_above; }
    bool isFullscreenDetect(void) const { return _screen_fullscreen_detect; }

    bool getShowFrameList(void) const { return _screen_showframelist; }
    bool isShowStatusWindow(void) const { return _screen_show_status_window; }
    bool isShowStatusWindowOnRoot(void) const {
        return _screen_show_status_window_on_root;
    }
    bool isShowClientID(void) const { return _screen_show_client_id; }
    int getShowWorkspaceIndicator(void) const {
        return _screen_show_workspace_indicator;
    }
    int getWorkspaceIndicatorScale(void) const {
        return _screen_workspace_indicator_scale;
    }
    uint getWorkspaceIndicatorOpacity(void) const {
        return _screen_workspace_indicator_opacity;
    }
    bool isPlaceNew(void) const { return _screen_place_new; }
    bool isFocusNew(void) const { return _screen_focus_new; }
    bool isFocusNewChild(void) const { return _screen_focus_new_child; }
    uint getFocusStealProtect(void) const {
        return _screen_focus_steal_protect;
    }
    bool isHonourRandr(void) const { return _screen_honour_randr; }
    bool isHonourAspectRatio(void) const { return _screen_honour_aspectratio; }
    CurrHeadSelector getCurrHeadSelector(void) const {
        return _screen_curr_head_selector;
    }

    bool placeTransOnParent(void) const { return _place_trans_parent; }

    bool getPlacementRow(void) const { return _screen_placement_row; }
    bool getPlacementLtR(void) const { return _screen_placement_ltr; }
    bool getPlacementTtB(void) const { return _screen_placement_ttb; }
    int getPlacementOffsetX(void) const { return _screen_placement_offset_x; }
    int getPlacementOffsetY(void) const { return _screen_placement_offset_y; }

    bool getClientUniqueName(void) const { return _screen_client_unique_name; }
    const std::string &getClientUniqueNamePre(void) const {
        return _screen_client_unique_name_pre;
    }
    inline const std::string &getClientUniqueNamePost(void) const {
        return _screen_client_unique_name_post;
    }
    bool isReportAllClients(void) const { return _screen_report_all_clients; }

    bool isMenuSelectOn(uint val) const { return (_menu_select_mask&val); }
    bool isMenuEnterOn(uint val) const { return (_menu_enter_mask&val); }
    bool isMenuExecOn(uint val) const { return (_menu_exec_mask&val); }
    bool isDisplayMenuIcons(void) const { return _menu_display_icons; }
    uint getMenuFocusOpacity(void) const { return _menu_focus_opacity; }
    uint getMenuUnfocusOpacity(void) const { return _menu_unfocus_opacity; }

    bool isCmdDialogHistoryUnique(void) const {
        return _cmd_dialog_history_unique;
    }
    int getCmdDialogHistorySize(void) const { return _cmd_dialog_history_size; }
    const std::string &getCmdDialogHistoryFile(void) const {
        return _cmd_dialog_history_file;
    }
    int getCmdDialogHistorySaveInterval(void) const {
        return _cmd_dialog_history_save_interval;
    }

    int getHarbourDAMinSide(void) const { return _harbour_da_min_s; }
    int getHarbourDAMaxSide(void) const { return _harbour_da_max_s; }
    int getHarbourHead(void) const { return _harbour_head_nr; }
    bool isHarbourOntop(void) const { return _harbour_ontop; }
    bool isHarbourMaximizeOver(void) const { return _harbour_maximize_over; }
    uint getHarbourPlacement(void) const { return _harbour_placement; }
    uint getHarbourOrientation(void) const { return _harbour_orientation; }
    uint getHarbourOpacity(void) const { return _harbour_opacity; }

    std::vector<ActionEvent> *getMouseActionList(MouseActionListName name) {
        return _mouse_action_map[name];
    }
    const std::vector<BoundButton>& getClientMouseActionButtons(void) {
        return _client_mouse_action_buttons;
    }

    std::vector<ActionEvent> *getBorderListFromPosition(uint pos);
    std::vector<ActionEvent> *getEdgeListFromPosition(uint pos);

    // map parsing
    ActionAccessMask getActionAccessMask(const std::string &name);

    bool parseActionAccessMask(const std::string &action_mask_string,
                               uint &mask);

    bool parseMoveResizeAction(const std::string &action_string,
                               Action &action);
    bool parseMoveResizeActions(const std::string &actions, ActionEvent &ae);
    bool parseMoveResizeEvent(CfgParser::Entry *section, ActionEvent &ae);

    bool parseInputDialogAction(const std::string &val, Action &action);
    bool parseInputDialogActions(const std::string &actions, ActionEvent &ae);
    bool parseInputDialogEvent(CfgParser::Entry *section, ActionEvent &ae);

    uint getMenuMask(const std::string &mask);
    /** Return maximum allowed icon width. */
    unsigned int getMenuIconLimit(unsigned int value, SizeLimitType limit,
                                  const std::string &name) const;

    bool parseMenuAction(const std::string& action_string, Action& action);
    bool parseMenuActions(const std::string& actions, ActionEvent& ae);
    bool parseMenuEvent(CfgParser::Entry *section, ActionEvent& ae);

    static bool parseOpacity(const std::string value, uint &focused,
                             uint &unfocused);

private:
    bool tryHardLoadConfig(CfgParser &cfg, std::string &file);
    void copyConfigFiles(void);

    void loadFiles(CfgParser::Entry *section);
    void loadMoveResize(CfgParser::Entry *section);
    void loadScreen(CfgParser::Entry *section);
    void loadMenu(CfgParser::Entry *section);
    void loadMenuIcons(CfgParser::Entry *section);
    void loadCmdDialog(CfgParser::Entry *section);
    void loadHarbour(CfgParser::Entry *section);

    void parseButtons(CfgParser::Entry *section,
                      std::vector<ActionEvent>* mouse_list,
                      std::vector<BoundButton>* mouse_buttons, ActionOk action_ok);

    std::string _config_file; /**< Path to config file last loaded. */
    TimeFiles _cfg_files;
    TimeFiles _cfg_files_mouse;

    // files
    std::string _files_keys;
    std::string _files_menu;
    std::string _files_start;
    std::string _files_autoprops;
    std::string _files_theme;
    std::string _files_theme_variant;
    std::string _files_mouse;
    std::string _files_icon_path; /**< Path to user icon directory. */

    // moveresize
    int _moveresize_edgeattract, _moveresize_edgeresist;
    int _moveresize_woattract, _moveresize_woresist;
    bool _moveresize_opaquemove, _moveresize_opaqueresize;

    // screen
    bool _screen_theme_background;
    int _screen_workspaces;
    int _screen_workspaces_per_row;
    std::vector<std::wstring> _screen_workspace_names;
    std::wstring _screen_workspace_name_default;
    std::vector<int> _screen_edge_sizes;
    bool _screen_edge_indent;
    int _screen_doubleclicktime;
    /** Flag to make fullscreen go above all windows. */
    bool _screen_fullscreen_above;
    /** Flag to make configure request fullscreen detection. */
    bool _screen_fullscreen_detect;
    bool _screen_showframelist;
    bool _screen_show_status_window;
    /** If true, center status window relative to current head. */
    bool _screen_show_status_window_on_root;
    bool _screen_show_client_id; //!< Flag to display client ID in title.
    /** Display workspace indicator for N seconds. */
    int _screen_show_workspace_indicator;
    /** Scale of the workspace indicator head */
    int _screen_workspace_indicator_scale;
    uint _screen_workspace_indicator_opacity;
    bool _screen_place_new, _screen_focus_new, _screen_focus_new_child;
    /** Number of seconds to protect against focus stealing. */
    uint _screen_focus_steal_protect;
    /** Boolean flag if randr information should be honoured. */
    bool _screen_honour_randr;
    /**< if true, pekwm keeps aspect ratio (XSizeHint) */
    bool _screen_honour_aspectratio;
    /** Setting for how current head is determined. */
    CurrHeadSelector _screen_curr_head_selector;
    bool _screen_placement_row, _screen_placement_ltr, _screen_placement_ttb;
    int _screen_placement_offset_x, _screen_placement_offset_y;
    bool _place_trans_parent;
    bool _screen_client_unique_name;
    std::string _screen_client_unique_name_pre, _screen_client_unique_name_post;
    bool _screen_report_all_clients;

    uint _menu_select_mask, _menu_enter_mask, _menu_exec_mask;
    /** Boolean flag, when true display icons in menus. */
    bool _menu_display_icons;
    uint _menu_focus_opacity, _menu_unfocus_opacity;

    /** Map of name -> limit for icons in menus */
    std::map<std::string, SizeLimits> _menu_icon_limits;

    /** Boolean flag, when true entries in the CmdDialog history are unique. */
    bool _cmd_dialog_history_unique;
    /** Number of entries in the history before the last entries are dropped. */
    int _cmd_dialog_history_size;
    /** Path to cmd dialog history file. */
    std::string _cmd_dialog_history_file;
    /** Save history file each Nth CmdDialog exec. */
    int _cmd_dialog_history_save_interval;

    int _harbour_da_min_s, _harbour_da_max_s;
    bool _harbour_ontop;
    bool _harbour_maximize_over;
    uint _harbour_placement;
    uint _harbour_orientation;
    int _harbour_head_nr;
    uint _harbour_opacity;

    std::map<MouseActionListName, std::vector<ActionEvent>* > _mouse_action_map;
    std::vector<BoundButton> _client_mouse_action_buttons;
};

namespace pekwm
{
    Config* config();
}
