//
// Action.hh for pekwm
// Copyright (C) 2003-2021 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#pragma once

#include "config.h"

#include "CfgParser.hh"
#include "Types.hh"
#include "pekwm.hh"
#include "X11.hh"

#include <vector>
#include <string>
#include <cstring>

class PWinObj;

//! @brief Masks used to set Action context validity.
enum ActionOk {
    KEYGRABBER_OK = (1<<1), //!< Keygrabber ok.
    FRAME_OK = (1<<2), //!< Frame title ok.
    CLIENT_OK = (1<<3), //!< Client click ok.
    ROOTCLICK_OK = (1<<4), //!< Root window click ok.
    BUTTONCLICK_OK = (1<<5), //!< Button{Press,Release} ok.
    WINDOWMENU_OK = (1<<6), //!< Ok from WindowMenu.
    ROOTMENU_OK = (1<<7), //!< Ok from RootMenu.
    FRAME_BORDER_OK  = (1<<8), //!< Frame border ok.
    SCREEN_EDGE_OK = (1<<9), //!< ScreenEdge ok.
    CMD_OK = (1<<10) //!< Command from pekwm_ctrl
};

/**
 * Mask used in auto properties granting/disallowing actions.
 */
enum ActionAccessMask {
    ACTION_ACCESS_NO = 0,
    ACTION_ACCESS_MOVE = 1<<1,
    ACTION_ACCESS_RESIZE = 1<<2,
    ACTION_ACCESS_ICONIFY = 1<<3,
    ACTION_ACCESS_SHADE = 1<<4,
    ACTION_ACCESS_STICK = 1<<5,
    ACTION_ACCESS_MAXIMIZE_HORZ = 1<<6,
    ACTION_ACCESS_MAXIMIZE_VERT = 1<<7,
    ACTION_ACCESS_FULLSCREEN = 1<<8,
    ACTION_ACCESS_CHANGE_DESKTOP = 1<<9,
    ACTION_ACCESS_CLOSE = 1<<10
};

enum ActionType {
    ACTION_UNSET = 0, ACTION_SET = 1, // to conform with bool values
    ACTION_TOGGLE,

    ACTION_FOCUS, ACTION_UNFOCUS,

    ACTION_GROW_DIRECTION, ACTION_MAXFILL,
    ACTION_RESIZE, ACTION_MOVE_RESIZE,
    ACTION_RAISE, ACTION_LOWER,
    ACTION_ACTIVATE_OR_RAISE,
    ACTION_CLOSE, ACTION_CLOSE_FRAME, ACTION_KILL,
    ACTION_SET_GEOMETRY, ACTION_MOVE_TO_HEAD, ACTION_MOVE_TO_EDGE,
    ACTION_NEXT_FRAME, ACTION_NEXT_FRAME_MRU,
    ACTION_PREV_FRAME, ACTION_PREV_FRAME_MRU,
    ACTION_FOCUS_DIRECTIONAL,
    ACTION_GOTO_CLIENT,
    ACTION_ACTIVATE_CLIENT_REL, ACTION_MOVE_CLIENT_REL,
    ACTION_ACTIVATE_CLIENT, ACTION_ACTIVATE_CLIENT_NUM,
    ACTION_SEND_TO_WORKSPACE, ACTION_GOTO_WORKSPACE,
    ACTION_WARP_TO_WORKSPACE,
    ACTION_SHOW_MENU, ACTION_HIDE_ALL_MENUS,
    ACTION_DETACH, ACTION_ATTACH_MARKED,
    ACTION_ATTACH_CLIENT_IN_NEXT_FRAME, ACTION_ATTACH_CLIENT_IN_PREV_FRAME,
    ACTION_ATTACH_FRAME_IN_NEXT_FRAME, ACTION_ATTACH_FRAME_IN_PREV_FRAME,

    ACTION_GOTO_CLIENT_ID, ACTION_FIND_CLIENT,

    ACTION_EXEC,
    ACTION_SHELL_EXEC,
    ACTION_RELOAD,
    ACTION_RESTART,
    ACTION_RESTART_OTHER,
    ACTION_EXIT,

    ACTION_MENU_NEXT, ACTION_MENU_PREV, ACTION_MENU_GOTO, ACTION_MENU_SELECT,
    ACTION_MENU_ENTER_SUBMENU, ACTION_MENU_LEAVE_SUBMENU,

    ACTION_MENU_SUB,
    ACTION_MENU_DYN,

    ACTION_MOVE, ACTION_GROUPING_DRAG,
    ACTION_SHOW_CMD_DIALOG,
    ACTION_SHOW_SEARCH_DIALOG,

    ACTION_SEND_KEY,
    ACTION_WARP_POINTER,
    ACTION_SET_OPACITY,

    ACTION_DEBUG,
    ACTION_NO
};

enum ActionStateType {
    ACTION_STATE_MAXIMIZED, ACTION_STATE_FULLSCREEN,
    ACTION_STATE_SHADED, ACTION_STATE_STICKY,
    ACTION_STATE_ALWAYS_ONTOP, ACTION_STATE_ALWAYS_BELOW,
    ACTION_STATE_DECOR_BORDER, ACTION_STATE_DECOR_TITLEBAR,
    ACTION_STATE_DECOR, ACTION_STATE_TITLE,
    ACTION_STATE_ICONIFIED, ACTION_STATE_TAGGED,
    ACTION_STATE_MARKED, ACTION_STATE_SKIP,
    ACTION_STATE_CFG_DENY,
    ACTION_STATE_OPAQUE,
    ACTION_STATE_HARBOUR_HIDDEN,
    ACTION_STATE_GLOBAL_GROUPING,

    ACTION_STATE_NO
};

enum StateAction {
    STATE_SET = ACTION_SET,
    STATE_UNSET = ACTION_UNSET,
    STATE_TOGGLE = ACTION_TOGGLE
};

enum MoveResizeActionType {
    MOVE_HORIZONTAL = 1, MOVE_VERTICAL,
    RESIZE_HORIZONTAL, RESIZE_VERTICAL,
    MOVE_SNAP,
    MOVE_CANCEL, MOVE_END,
    NO_MOVERESIZE_ACTION = 0
};

enum InputDialogAction {
    INPUT_INSERT, INPUT_REMOVE,
    INPUT_CLEAR, INPUT_CLEARFROMCURSOR, INPUT_EXEC, INPUT_CLOSE,
    INPUT_COMPLETE, INPUT_COMPLETE_ABORT,
    INPUT_CURS_NEXT, INPUT_CURS_PREV,
    INPUT_CURS_END, INPUT_CURS_BEGIN,
    INPUT_HIST_NEXT, INPUT_HIST_PREV,
    INPUT_NO_ACTION
};

enum ActionParamUnit {
    UNIT_NONE,
    UNIT_PIXEL,
    UNIT_PERCENT
};

/**
 * Last byte of data buffer in _PEKWM_CMD messages, indicates if the
 * message is part of a larger message.
 */
enum PekwmCmdBuf {
    PEKWM_CMD_SINGLE = 0,
    PEKWM_CMD_MULTI_FIRST = 1,
    PEKWM_CMD_MULTI_CONT = 2,
    PEKWM_CMD_MULTI_END = 3
};

// Action Utils
namespace ActionUtil {
    //! @brief Determines if state needs toggling.
    //! @return true if state needs toggling, else false.
    inline bool needToggle(StateAction sa, bool state) {
        if ((state && (sa == STATE_SET))
            || (! state && (sa == STATE_UNSET))) {
            return false;
        }
        return true;
    }
}

// Structs and Classes

class Action {
public:
    Action(uint action = ACTION_UNSET);
    ~Action(void);

    inline uint getAction(void) const { return _action; }
    inline int getParamI() const {
        return getParamI(0);
    }
    inline int getParamI(uint n) const {
        return n < _i.size() ? _i[n] : 0;
    }
    size_t numParamI(void) const { return _i.size(); }
    inline const std::string &getParamS(void) const {
        return getParamS(0);
    }
    inline const std::string &getParamS(uint n) const {
        return n < _s.size() ? _s[n] : _empty_string;
    }
    size_t numParamS(void) const { return _s.size(); }

    inline void setAction(uint action) { _action = action; }
    inline void setParamI(uint n, int param) {
        while (n >= _i.size()) {
            _i.push_back(0);
        }
        _i[n] = param;
    }
    inline void setParamS(const std::string param) {
        setParamS(0, param);
    }
    inline void setParamS(uint n, const std::string param) {
        while (n >= _s.size()) {
            _s.push_back("");
        }
        _s[n] = param;
    }

    inline void clear()
    {
        _action = ACTION_UNSET;
        _i.clear();
        _s.clear();
    }
private:
    uint _action;
    std::vector<int> _i;
    std::vector<std::string> _s;

    static std::string _empty_string;
};

class ActionEvent {
public:
    ActionEvent(void);
    ActionEvent(Action action);
    ~ActionEvent(void);

    inline bool isOnlyAction(uint action) const {
        return ((action_list.size() == 1) &&
                (action_list.front().getAction() == action));
    }

    uint mod, sym; // event matching
    uint type, threshold; // more matching, press, release etc
    std::vector<Action> action_list;
};

class ActionPerformed {
public:
    ActionPerformed(PWinObj *w, const ActionEvent &a)
        : wo(w), ae(a), type(0)
    {
    }
    ~ActionPerformed(void)
    {
    }

    PWinObj *wo;
    const ActionEvent &ae;

    int type;
    union _event {
        XButtonEvent *button;
        XKeyEvent *key;
        XMotionEvent *motion;
        XCrossingEvent *crossing;
        XExposeEvent *expose;
        XClientMessageEvent *client;
    } event;
};

namespace ActionConfig
{
    bool parseKey(const std::string &key_string, uint& mod, uint &key);

    bool parseAction(const std::string &action_string,
                     Action &action, uint mask);
    bool parseActions(const std::string &action_string,
                      ActionEvent &ae, uint mask);
    bool parseActionEvent(CfgParser::Entry *section, ActionEvent &ae,
                          uint mask, bool is_button);

    void parseActionSetGeometry(Action& action, const std::string &str);

    ActionType getAction(const std::string &name, uint mask);
    BorderPosition getBorderPos(const std::string &name);
    CfgDeny getCfgDeny(const std::string& name);
    DirectionType getDirection(const std::string &name);
    Layer getLayer(const std::string& name);
    uint getMod(const std::string &name);
    uint getMouseButton(const std::string &name);
    Skip getSkip(const std::string& name);

    std::vector<std::string> getActionNameList(void);
    std::vector<std::string> getStateNameList(void);
}
