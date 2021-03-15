//
// Action.cc for pekwm
// Copyright (C) 2021 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "config.h"

#include "Action.hh"
#include "Debug.hh"
#include "Util.hh"

#include <map>

typedef std::pair<ActionType, uint> action_pair;

const int FRAME_MASK =
    FRAME_OK|FRAME_BORDER_OK|CLIENT_OK|WINDOWMENU_OK|
    KEYGRABBER_OK|BUTTONCLICK_OK;
const int ANY_MASK =
    KEYGRABBER_OK|FRAME_OK|FRAME_BORDER_OK|CLIENT_OK|ROOTCLICK_OK|
    BUTTONCLICK_OK|WINDOWMENU_OK|ROOTMENU_OK|SCREEN_EDGE_OK|
    CMD_OK;

static Util::StringMap<std::pair<ActionType, uint> > action_map =
    {{"", action_pair(ACTION_NO, 0)},
     {"Focus", action_pair(ACTION_FOCUS, ANY_MASK)},
     {"UnFocus", action_pair(ACTION_UNFOCUS, ANY_MASK)},
     {"Set", action_pair(ACTION_SET, ANY_MASK)},
     {"Unset", action_pair(ACTION_UNSET, ANY_MASK)},
     {"Toggle", action_pair(ACTION_TOGGLE, ANY_MASK)},
     {"MaxFill", action_pair(ACTION_MAXFILL, FRAME_MASK|CMD_OK)},
     {"GrowDirection", action_pair(ACTION_GROW_DIRECTION, FRAME_MASK|CMD_OK)},
     {"Close", action_pair(ACTION_CLOSE, FRAME_MASK)},
     {"CloseFrame", action_pair(ACTION_CLOSE_FRAME, FRAME_MASK)},
     {"Kill", action_pair(ACTION_KILL, FRAME_MASK)},
     {"SetGeometry", action_pair(ACTION_SET_GEOMETRY, FRAME_MASK|CMD_OK)},
     {"Raise", action_pair(ACTION_RAISE, FRAME_MASK|CMD_OK)},
     {"Lower", action_pair(ACTION_LOWER, FRAME_MASK|CMD_OK)},
     {"ActivateOrRaise", action_pair(ACTION_ACTIVATE_OR_RAISE,
                                     FRAME_MASK|CMD_OK)},
     {"ActivateClientRel", action_pair(ACTION_ACTIVATE_CLIENT_REL,
                                       FRAME_MASK|CMD_OK)},
     {"MoveClientRel", action_pair(ACTION_MOVE_CLIENT_REL, FRAME_MASK|CMD_OK)},
     {"ActivateClient", action_pair(ACTION_ACTIVATE_CLIENT, FRAME_MASK|CMD_OK)},
     {"ActivateClientNum",
      action_pair(ACTION_ACTIVATE_CLIENT_NUM, KEYGRABBER_OK|CMD_OK)},
     {"Resize",
      action_pair(ACTION_RESIZE,
                  BUTTONCLICK_OK|CLIENT_OK|FRAME_OK|FRAME_BORDER_OK)},
     {"Move", action_pair(ACTION_MOVE, FRAME_OK|FRAME_BORDER_OK|CLIENT_OK)},
     {"MoveResize", action_pair(ACTION_MOVE_RESIZE, KEYGRABBER_OK)},
     {"GroupingDrag", action_pair(ACTION_GROUPING_DRAG, FRAME_OK|CLIENT_OK)},
     {"WarpToWorkspace", action_pair(ACTION_WARP_TO_WORKSPACE, SCREEN_EDGE_OK)},
     {"MoveToHead", action_pair(ACTION_MOVE_TO_HEAD, FRAME_MASK|CMD_OK)},
     {"MoveToEdge", action_pair(ACTION_MOVE_TO_EDGE, KEYGRABBER_OK|CMD_OK)},
     {"NextFrame",
      action_pair(ACTION_NEXT_FRAME,
                  KEYGRABBER_OK|ROOTCLICK_OK|SCREEN_EDGE_OK)},
     {"PrevFrame",
      action_pair(ACTION_PREV_FRAME,
                  KEYGRABBER_OK|ROOTCLICK_OK|SCREEN_EDGE_OK)},
     {"NextFrameMRU",
      action_pair(ACTION_NEXT_FRAME_MRU,
                  KEYGRABBER_OK|ROOTCLICK_OK|SCREEN_EDGE_OK)},
     {"PrevFrameMRU",
      action_pair(ACTION_PREV_FRAME_MRU,
                  KEYGRABBER_OK|ROOTCLICK_OK|SCREEN_EDGE_OK)},
     {"FocusDirectional", action_pair(ACTION_FOCUS_DIRECTIONAL,
                                      FRAME_MASK|CMD_OK)},
     {"AttachMarked", action_pair(ACTION_ATTACH_MARKED, FRAME_MASK|CMD_OK)},
     {"AttachClientInNextFrame",
      action_pair(ACTION_ATTACH_CLIENT_IN_NEXT_FRAME, FRAME_MASK|CMD_OK)},
     {"AttachClientInPrevFrame",
      action_pair(ACTION_ATTACH_CLIENT_IN_PREV_FRAME, FRAME_MASK|CMD_OK)},
     {"FindClient", action_pair(ACTION_FIND_CLIENT, ANY_MASK)},
     {"GotoClientID", action_pair(ACTION_GOTO_CLIENT_ID, ANY_MASK|CMD_OK)},
     {"Detach", action_pair(ACTION_DETACH, FRAME_MASK|CMD_OK)},
     {"SendToWorkspace", action_pair(ACTION_SEND_TO_WORKSPACE, ANY_MASK)},
     {"GoToWorkspace", action_pair(ACTION_GOTO_WORKSPACE, ANY_MASK)},
     {"Exec",
      action_pair(ACTION_EXEC,
                  FRAME_MASK|ROOTMENU_OK|ROOTCLICK_OK|SCREEN_EDGE_OK)},
     {"ShellExec",
      action_pair(ACTION_SHELL_EXEC,
                  FRAME_MASK|ROOTMENU_OK|ROOTCLICK_OK|SCREEN_EDGE_OK)},
     {"Reload", action_pair(ACTION_RELOAD, KEYGRABBER_OK|ROOTMENU_OK)},
     {"Restart", action_pair(ACTION_RESTART, KEYGRABBER_OK|ROOTMENU_OK)},
     {"RestartOther",
      action_pair(ACTION_RESTART_OTHER, KEYGRABBER_OK|ROOTMENU_OK)},
     {"Exit", action_pair(ACTION_EXIT, KEYGRABBER_OK|ROOTMENU_OK)},
     {"ShowCmdDialog",
      action_pair(ACTION_SHOW_CMD_DIALOG,
                  KEYGRABBER_OK|ROOTCLICK_OK|SCREEN_EDGE_OK|ROOTMENU_OK|
                  WINDOWMENU_OK|CMD_OK)},
     {"ShowSearchDialog",
      action_pair(ACTION_SHOW_SEARCH_DIALOG, KEYGRABBER_OK|ROOTCLICK_OK|
                  SCREEN_EDGE_OK|ROOTMENU_OK|WINDOWMENU_OK|CMD_OK)},
     {"ShowMenu",
      action_pair(ACTION_SHOW_MENU, FRAME_MASK|ROOTCLICK_OK|SCREEN_EDGE_OK|
                  ROOTMENU_OK|WINDOWMENU_OK|CMD_OK)},
     {"HideAllMenus",
      action_pair(ACTION_HIDE_ALL_MENUS, FRAME_MASK|ROOTCLICK_OK|
                  SCREEN_EDGE_OK|CMD_OK)},
     {"SubMenu", action_pair(ACTION_MENU_SUB, ROOTMENU_OK|WINDOWMENU_OK)},
     {"Dynamic", action_pair(ACTION_MENU_DYN, ROOTMENU_OK|WINDOWMENU_OK)},
     {"SendKey", action_pair(ACTION_SEND_KEY, ANY_MASK)},
     {"WarpPointer", action_pair(ACTION_WARP_POINTER, ANY_MASK)},
     {"SetOpacity", action_pair(ACTION_SET_OPACITY, FRAME_MASK|CMD_OK)},
     {"Debug", action_pair(ACTION_DEBUG, ANY_MASK)}};

static Util::StringMap<ActionStateType> action_state_map =
    {{"", ACTION_STATE_NO},
     {"Maximized", ACTION_STATE_MAXIMIZED},
     {"Fullscreen", ACTION_STATE_FULLSCREEN},
     {"Shaded", ACTION_STATE_SHADED},
     {"Sticky", ACTION_STATE_STICKY},
     {"AlwaysOnTop", ACTION_STATE_ALWAYS_ONTOP},
     {"AlwaysBelow", ACTION_STATE_ALWAYS_BELOW},
     {"Decor", ACTION_STATE_DECOR},
     {"DecorBorder", ACTION_STATE_DECOR_BORDER},
     {"DecorTitlebar", ACTION_STATE_DECOR_TITLEBAR},
     {"Iconified", ACTION_STATE_ICONIFIED},
     {"Tagged", ACTION_STATE_TAGGED},
     {"Marked", ACTION_STATE_MARKED},
     {"Skip", ACTION_STATE_SKIP},
     {"CfgDeny", ACTION_STATE_CFG_DENY},
     {"Opaque", ACTION_STATE_OPAQUE},
     {"Title", ACTION_STATE_TITLE},
     {"HarbourHidden", ACTION_STATE_HARBOUR_HIDDEN},
     {"GlobalGrouping", ACTION_STATE_GLOBAL_GROUPING}};

static Util::StringMap<BorderPosition> borderpos_map =
    {{"", BORDER_NO_POS},
     {"TOPLEFT", BORDER_TOP_LEFT},
     {"TOP", BORDER_TOP},
     {"TOPRIGHT", BORDER_TOP_RIGHT},
     {"LEFT", BORDER_LEFT},
     {"RIGHT", BORDER_RIGHT},
     {"BOTTOMLEFT", BORDER_BOTTOM_LEFT},
     {"BOTTOM", BORDER_BOTTOM},
     {"BOTTOMRIGHT", BORDER_BOTTOM_RIGHT}};

static Util::StringMap<CfgDeny> cfg_deny_map =
    {{"", CFG_DENY_NO},
     {"POSITION", CFG_DENY_POSITION},
     {"SIZE", CFG_DENY_SIZE},
     {"STACKING", CFG_DENY_STACKING},
     {"ACTIVEWINDOW", CFG_DENY_ACTIVE_WINDOW},
     {"MAXIMIZEDVERT", CFG_DENY_STATE_MAXIMIZED_VERT},
     {"MAXIMIZEDHORZ", CFG_DENY_STATE_MAXIMIZED_HORZ},
     {"HIDDEN", CFG_DENY_STATE_HIDDEN},
     {"FULLSCREEN", CFG_DENY_STATE_FULLSCREEN},
     {"ABOVE", CFG_DENY_STATE_ABOVE},
     {"BELOW", CFG_DENY_STATE_BELOW},
     {"STRUT", CFG_DENY_STRUT},
     {"RESIZEINC", CFG_DENY_RESIZE_INC}};

static Util::StringMap<DirectionType> direction_map =
    {{"", DIRECTION_NO},
     {"UP", DIRECTION_UP},
     {"DOWN", DIRECTION_DOWN},
     {"LEFT", DIRECTION_LEFT},
     {"RIGHT", DIRECTION_RIGHT}};

static Util::StringMap<OrientationType> edge_map =
    {{"", NO_EDGE},
     {"TOPLEFT", TOP_LEFT},
     {"TOPEDGE", TOP_EDGE},
     {"TOPCENTEREDGE", TOP_CENTER_EDGE},
     {"TOPRIGHT", TOP_RIGHT},
     {"BOTTOMRIGHT", BOTTOM_RIGHT},
     {"BOTTOMEDGE", BOTTOM_EDGE},
     {"BOTTOMCENTEREDGE", BOTTOM_CENTER_EDGE},
     {"BOTTOMLEFT", BOTTOM_LEFT},
     {"LEFTEDGE", LEFT_EDGE},
     {"LEFTCENTEREDGE", LEFT_CENTER_EDGE},
     {"RIGHTEDGE", RIGHT_EDGE},
     {"RIGHTCENTEREDGE", RIGHT_CENTER_EDGE},
     {"CENTER", CENTER}};

static Util::StringMap<Layer> layer_map =
    {{"", LAYER_NONE},
     {"DESKTOP", LAYER_DESKTOP},
     {"BELOW", LAYER_BELOW},
     {"NORMAL", LAYER_NORMAL},
     {"ONTOP", LAYER_ONTOP},
     {"HARBOUR", LAYER_DOCK},
     {"ABOVEHARBOUR", LAYER_ABOVE_DOCK},
     {"MENU", LAYER_MENU}};

static Util::StringMap<uint> mod_map =
    {{"", 0},
     {"NONE", 0},
     {"SHIFT", ShiftMask},
     {"CTRL", ControlMask},
     {"MOD1", Mod1Mask},
     {"MOD2", Mod2Mask},
     {"MOD3", Mod3Mask},
     {"MOD4", Mod4Mask},
     {"MOD5", Mod5Mask},
     {"ANY", MOD_ANY}};

static Util::StringMap<Raise> raise_map =
    {{"", NO_RAISE},
     {"ALWAYSRAISE", ALWAYS_RAISE},
     {"ENDRAISE", END_RAISE},
     {"NEVERRAISE", NEVER_RAISE},
     {"TEMPRAISE", TEMP_RAISE}};

static Util::StringMap<Skip> skip_map =
    {{"", SKIP_NONE},
     {"MENUS", SKIP_MENUS},
     {"FOCUSTOGGLE", SKIP_FOCUS_TOGGLE},
     {"SNAP", SKIP_SNAP},
     {"PAGER", SKIP_PAGER},
     {"TASKBAR", SKIP_TASKBAR}};

static Util::StringMap<WorkspaceChangeType> workspace_change_map =
    {{"", WORKSPACE_NO},
     {"LEFT", WORKSPACE_LEFT},
     {"LEFTN", WORKSPACE_LEFT_N},
     {"PREV", WORKSPACE_PREV},
     {"PREVN", WORKSPACE_PREV_N},
     {"RIGHT", WORKSPACE_RIGHT},
     {"RIGHTN", WORKSPACE_RIGHT_N},
     {"NEXT", WORKSPACE_NEXT},
     {"NEXTN", WORKSPACE_NEXT_N},
     {"PREVV", WORKSPACE_PREV_V},
     {"UP", WORKSPACE_UP},
     {"NEXTV", WORKSPACE_NEXT_V},
     {"DOWN", WORKSPACE_DOWN},
     {"LAST", WORKSPACE_LAST}};

/**
 * Parse WarpToWorkspace, SendToWorkspace and GotoWorkspace argument.
 *
 * Either in form of absolute number or as ROWxCOL, the latter is
 * stored as 3 integers where the first is -1, then row and col.
 */
static void
parseActionChangeWorkspace(Action &action, const std::string &arg)
{
    // Get workspace looking for relative numbers
    uint num = workspace_change_map.get(arg);

    if (num == WORKSPACE_NO) {
        // Workspace isn't relative, check for 2x2 and ordinary specification
        std::vector<std::string> tok;
        if (Util::splitString(arg, tok, "x", 2, true) == 2) {
            action.setParamI(0, -1); // indicate ROWxCOL
            action.setParamI(1, strtol(tok[0].c_str(), 0, 10) - 1); // row
            action.setParamI(2, strtol(tok[1].c_str(), 0, 10) - 1); // col
        } else {
            action.setParamI(0, strtol(arg.c_str(), 0, 10) - 1);
        }
    } else {
        action.setParamI(0, num);
    }
}

static bool
parseActionState(Action &action, const std::string &as_action)
{
    std::vector<std::string> tok;

    // chop the string up separating the action and parameters
    if (Util::splitString(as_action, tok, " \t", 2)) {
        action.setParamI(0, action_state_map.get(tok[0]));
        if (action.getParamI(0) != ACTION_STATE_NO) {
            if (tok.size() == 2) { // we got enough tok for a parameter
                std::string directions;

                switch (action.getParamI(0)) {
                case ACTION_STATE_MAXIMIZED:
                    // Using copy of token here to silence valgrind checks.
                    directions = tok[1];

                    Util::splitString(directions, tok, " \t", 2);
                    if (tok.size() == 4) {
                        action.setParamI(1, Util::isTrue(tok[2]));
                        action.setParamI(2, Util::isTrue(tok[3]));
                    } else {
                        USER_WARN("missing argument to Maximized, 2 required");
                    }
                    break;
                case ACTION_STATE_TAGGED:
                    action.setParamI(1, Util::isTrue(tok[1]));
                    break;
                case ACTION_STATE_SKIP:
                    action.setParamI(1, skip_map.get(tok[1]));
                    break;
                case ACTION_STATE_CFG_DENY:
                    action.setParamI(1, cfg_deny_map.get(tok[1]));
                    break;
                case ACTION_STATE_DECOR:
                case ACTION_STATE_TITLE:
                    action.setParamS(tok[1]);
                    break;
                };
            } else {
                switch (action.getParamI(0)) {
                case ACTION_STATE_MAXIMIZED:
                    action.setParamI(1, 1);
                    action.setParamI(2, 1);
                    break;
                default:
                    // do nothing
                    break;
                }
            }

            return true;
        }
    }

    return false;
}

static void
parseActionArg(Action &action, const std::string& arg)
{
    std::vector<std::string> tok;
    switch (action.getAction()) {
    case ACTION_EXEC:
    case ACTION_SHELL_EXEC:
    case ACTION_RESTART_OTHER:
    case ACTION_FIND_CLIENT:
    case ACTION_SHOW_CMD_DIALOG:
    case ACTION_SHOW_SEARCH_DIALOG:
    case ACTION_SEND_KEY:
    case ACTION_MENU_DYN:
    case ACTION_DEBUG:
        action.setParamS(arg);
        break;
    case ACTION_SET_GEOMETRY:
        ActionConfig::parseActionSetGeometry(action, arg);
        break;
    case ACTION_ACTIVATE_CLIENT_REL:
    case ACTION_MOVE_CLIENT_REL:
    case ACTION_GOTO_CLIENT_ID:
    case ACTION_MOVE_TO_HEAD:
        action.setParamI(0, strtol(arg.c_str(), 0, 10));
        break;
    case ACTION_SET:
    case ACTION_UNSET:
    case ACTION_TOGGLE:
        parseActionState(action, arg);
        break;
    case ACTION_MAXFILL:
        if ((Util::splitString(arg, tok, " \t", 2)) == 2) {
            action.setParamI(0, Util::isTrue(tok[tok.size() - 2]));
            action.setParamI(1, Util::isTrue(tok[tok.size() - 1]));
        } else {
            USER_WARN("missing argument to MaxFill, 2 required");
        }
        break;
    case ACTION_GROW_DIRECTION:
        action.setParamI(0, direction_map.get(arg));
        break;
    case ACTION_ACTIVATE_CLIENT_NUM:
        action.setParamI(0, strtol(arg.c_str(), 0, 10) - 1);
        if (action.getParamI(0) < 0) {
            USER_WARN("negative number to ActivateClientNum");
            action.setParamI(0, 0);
        }
        break;
    case ACTION_WARP_TO_WORKSPACE:
    case ACTION_SEND_TO_WORKSPACE:
    case ACTION_GOTO_WORKSPACE:
        parseActionChangeWorkspace(action, arg);
        break;
    case ACTION_GROUPING_DRAG:
        action.setParamI(0, Util::isTrue(arg));
        break;
    case ACTION_MOVE_TO_EDGE:
        action.setParamI(0, edge_map.get(arg));
        break;
    case ACTION_NEXT_FRAME:
    case ACTION_NEXT_FRAME_MRU:
    case ACTION_PREV_FRAME:
    case ACTION_PREV_FRAME_MRU:
        if ((Util::splitString(arg, tok, " \t", 2)) == 2) {
            action.setParamI(0, raise_map.get(tok[0]));
            action.setParamI(1, Util::isTrue(tok[1]));
        } else {
            action.setParamI(0, raise_map.get(arg));
            action.setParamI(1, false);
        }
        break;
    case ACTION_FOCUS_DIRECTIONAL:
        if ((Util::splitString(arg, tok, " \t", 2)) == 2) {
            action.setParamI(0, direction_map.get(tok[0]));
            action.setParamI(1, Util::isTrue(tok[1])); // raise
        } else {
            action.setParamI(0, direction_map.get(arg));
            action.setParamI(1, true); // default to raise
        }
        break;
    case ACTION_RESIZE:
        action.setParamI(0, 1 + borderpos_map.get(arg));
        break;
    case ACTION_RAISE:
    case ACTION_LOWER:
        if ((Util::splitString(arg, tok, " \t", 1)) == 1) {
            action.setParamI(0, Util::isTrue(tok[tok.size() - 1]));
        } else {
            action.setParamI(0, false);
        }
        break;
    case ACTION_SHOW_MENU:
        Util::splitString(arg, tok, " \t", 2);
        Util::to_upper(tok[0]);
        action.setParamS(tok[0]);
        if (tok.size() == 2) {
            action.setParamI(0, Util::isTrue(tok[1]));
        } else {
            // Default to non-sticky
            action.setParamI(0, false);
        }
        break;
    case ACTION_WARP_POINTER:
        if (Util::splitString(arg, tok, " \t", 2) == 2) {
            action.setParamI(0, std::atoi(tok[0].c_str()));
            action.setParamI(1, std::atoi(tok[1].c_str()));
        }
        break;
    case ACTION_SET_OPACITY:
        if ((Util::splitString(arg, tok, " \t", 2)) == 2) {
            action.setParamI(0, std::atoi(tok[0].c_str()));
            action.setParamI(1, std::atoi(tok[1].c_str()));
        } else {
            action.setParamI(0, std::atoi(arg.c_str()));
            action.setParamI(1, std::atoi(arg.c_str()));
        }
        break;
    default:
        // do nothing
        break;
    }
}

static void
parseActionNoArg(Action &action)
{
    switch (action.getAction()) {
    case ACTION_MAXFILL:
        action.setParamI(0, 1);
        action.setParamI(1, 1);
        break;
    default:
        // do nothing
        break;
    }
}

static bool
parseButton(const std::string &button_string, uint &mod, uint &button)
{
    std::vector<std::string> tok;
    // chop the string up separating mods and the end key/button
    if (Util::splitString(button_string, tok, " \t")) {
        // if the last token isn't an key/button, the action isn't valid
        button = ActionConfig::getMouseButton(tok[tok.size() - 1]);
        if (button != BUTTON_NO) {
            tok.pop_back(); // remove the key/button

            // add the modifier
            mod = 0;
            uint tmp_mod;

            for (auto it : tok) {
                tmp_mod = ActionConfig::getMod(it);
                if (tmp_mod == MOD_ANY) {
                    mod = MOD_ANY;
                    break;
                } else {
                    mod |= tmp_mod;
                }
            }

            return true;
        }
    }

    return false;
}

Action::Action(uint action)
    : _action(action)
{
}

Action::~Action(void)
{
}

ActionEvent::ActionEvent(void)
    : mod(0),
      sym(0),
      type(0),
      threshold(0)
{
}

ActionEvent::ActionEvent(Action action)
    : mod(0),
      sym(0),
      type(0),
      threshold(0)
{
    action_list.push_back(action);
}

ActionEvent::~ActionEvent(void)
{
}

namespace ActionConfig {

    bool
    parseKey(const std::string &key_string, uint& mod, uint &key)
    {
        // chop the string up separating mods and the end key/button
        std::vector<std::string> tok;
        if (! Util::splitString(key_string, tok, " \t")) {
            return false;
        }

        uint num = tok.size() - 1;
        if ((tok[num].size() > 1) && (tok[num][0] == '#')) {
            key = strtol(tok[num].c_str() + 1, 0, 10);
        } else if (strcasecmp(tok[num].c_str(), "ANY") == 0) {
            // Do no matching, anything goes.
            key = 0;
        } else {
            auto keysym = XStringToKeysym(tok[num].c_str());

            // XStringToKeysym() may fail. Perhaps we have luck after some
            // simple transformations. First we convert the string to lowercase
            // and try again. Then we try with only the first character in
            // uppercase and at last we try a complete uppercase string. If all
            // fails, we print a warning and return false.
            if (keysym == NoSymbol) {
                std::string str = tok[num];
                Util::to_lower(str);
                keysym = XStringToKeysym(str.c_str());
                if (keysym == NoSymbol) {
                    str[0] = ::toupper(str[0]);
                    keysym = XStringToKeysym(str.c_str());
                    if (keysym == NoSymbol) {
                        Util::to_upper(str);
                        keysym = XStringToKeysym(str.c_str());
                        if (keysym == NoSymbol) {
                            USER_WARN("could not find keysym for "
                                      << tok[num]);
                            return false;
                        }
                    }
                }
            }
            key = XKeysymToKeycode(X11::getDpy(), keysym);
        }

        // if the last token isn't an key/button, the action isn't valid
        if ((key != 0) || (strcasecmp(tok[num].c_str(), "ANY") == 0)) {
            tok.pop_back(); // remove the key/button

            // add the modifier
            mod = 0;
            for (auto it : tok) {
                mod |= getMod(it);
            }

            return true;
        }

        return false;
    }


    /**
     * Parse a single action and fills action.
     * @param action_string String representation of action.
     * @param action Action structure to fill in.
     * @param mask Mask action is valid for.
     * @return true on success, else false
     */
    bool
    parseAction(const std::string &action_string, Action &action, uint mask)
    {
        std::vector<std::string> tok;

        // chop the string up separating the action and parameters
        if (Util::splitString(action_string, tok, " \t", 2)) {
            action.setAction(getAction(tok[0], mask));
            if (action.getAction() != ACTION_NO) {
                if (tok.size() == 2) {
                    parseActionArg(action, tok[1]);
                } else {
                    parseActionNoArg(action);
                }
                return true;
            }
        }

        return false;
    }

    bool
    parseActions(const std::string &action_string, ActionEvent &ae, uint mask)
    {
        // reset the action event
        ae.action_list.clear();

        std::vector<std::string> tok;
        if (! Util::splitString(action_string, tok, ";", 0, false, '\\')) {
            return false;
        }

        for (auto it : tok) {
            Action action;
            if (parseAction(it, action, mask)) {
                ae.action_list.push_back(action);
            }
        }

        return true;
    }

    bool
    parseActionEvent(CfgParser::Entry *section, ActionEvent &ae,
                     uint mask, bool is_button)
    {
        auto value = section->findEntry("ACTIONS");
        if (value == nullptr && section->getSection()) {
            value = section->getSection()->findEntry("ACTIONS");
        }
        if (value == nullptr) {
            return false;
        }

        auto str_button = section->getValue();
        if (str_button.empty()) {
            if (ae.type == MOUSE_EVENT_ENTER || ae.type == MOUSE_EVENT_LEAVE) {
                str_button = "1";
            } else {
                return false;
            }
        }

        bool ok;
        if (is_button) {
            ok = parseButton(str_button, ae.mod, ae.sym);
        } else {
            ok = parseKey(str_button, ae.mod, ae.sym);
        }

        return ok ? parseActions(value->getValue(), ae, mask) : false;
    }

    /**
     * Parse SetGeometry action parameters.
     *
     * SetGeometry 1x+0+0 [(screen|current|0-9) [HonourStrut]]
     */
    void
    parseActionSetGeometry(Action& action, const std::string &str)
    {
        std::vector<std::string> tok;
        if (! Util::splitString(str, tok, " \t", 3)) {
            return;
        }

        // geometry
        action.setParamS(tok[0]);

        // screen, current head or head number
        if (tok.size() > 1) {
            if (strcasecmp(tok[1].c_str(), "SCREEN") == 0) {
                action.setParamI(0, -1);
            } else if (strcasecmp(tok[1].c_str(), "CURRENT") == 0) {
                action.setParamI(0, -2);
            } else {
                action.setParamI(0, strtol(tok[1].c_str(), 0, 10));
            }
        } else {
            action.setParamI(0, -1);
        }

        // honour strut option
        if (tok.size() > 2) {
            auto honour_strut =
                strcasecmp(tok[2].c_str(), "HONOURSTRUT") ? 0 : 1;
            action.setParamI(1, honour_strut);
        } else {
            action.setParamI(1, 0);
        }
    }

    ActionType
    getAction(const std::string &name, uint mask)
    {
        auto val = action_map.get(name);
        if (val.second & mask) {
            return val.first;
        }
        return ACTION_NO;
    }

    BorderPosition
    getBorderPos(const std::string &name)
    {
        return borderpos_map.get(name);
    }

    CfgDeny
    getCfgDeny(const std::string& name)
    {
        return cfg_deny_map.get(name);
    }

    DirectionType
    getDirection(const std::string &name)
    {
        return direction_map.get(name);
    }

    Layer
    getLayer(const std::string &name)
    {
        return layer_map.get(name);
    }

    uint
    getMod(const std::string &name)
    {
        return mod_map.get(name);
    }

    uint
    getMouseButton(const std::string &name)
    {
        uint button;

        if (strcasecmp(name.c_str(), "ANY") == 0) {
            button = BUTTON_ANY;
        } else {
            button = unsigned(strtol(name.c_str(), 0, 10));
        }

        if (button > BUTTON_NO) {
            button = BUTTON_NO;
        }

        return button;
    }

    Skip
    getSkip(const std::string &name)
    {
        return skip_map.get(name);
    }

    /** Return vector with available keyboard actions names. */
    std::vector<std::string>
    getActionNameList(void)
    {
        std::vector<std::string> action_names;
        for (auto it : action_map) {
            if (it.second.second&KEYGRABBER_OK) {
                action_names.push_back(it.first.str());
            }
        }
        return action_names;
    }


    /** Return vector with available state action names. */
    std::vector<std::string> getStateNameList(void) {
        std::vector<std::string> state_names;
        for (auto it : action_state_map) {
            state_names.push_back(it.first.str());
        }
        return state_names;
    }

}

std::string Action::_empty_string = "";
