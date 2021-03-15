//
// PWinObj.hh for pekwm
// Copyright (C) 2003-2020 Claes Nästen <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#pragma once

#include "config.h"

#include <algorithm>
#include <map>

#include "pekwm.hh"
#include "X11.hh"
#include "Action.hh"
#include "Observable.hh"

//! @brief X11 Window wrapper class.
class PWinObj : public Observable
{
public:
    /**
     * Observation sent when a PWinObj is destructed.
     */
    class PWinObjDeleted : public Observation {
    };

    //! @brief PWinObj inherited types.
    enum Type {
        WO_FRAME = (1<<1), //!< Frame type.
        WO_CLIENT = (1<<2), //!< Client type.
        WO_MENU = (1<<3), //!< PMenu type.
        WO_DOCKAPP = (1<<4), //!< DockApp type.
        WO_SCREEN_EDGE = (1<<5), //!< ScreenEdge type.
        WO_SCREEN_ROOT = (1<<6), //!< PWinObj type for root Window.
        WO_CMD_DIALOG = (1<<7), //!< CmdDialog type.
        WO_STATUS = (1<<8), //!< StatusWindow type.
        WO_WORKSPACE_INDICATOR = (1<<9), //!< WorkspaceIndicator type.
        WO_SCREEN_HINT = (1<<10), /**< Invisible hint window. */
        WO_SEARCH_DIALOG = (1<<11), //!< SearchDialog type
        WO_NO_TYPE = 0 //!< No type.
    };

    PWinObj(bool keyboard_input);
    virtual ~PWinObj(void);

    //! @brief Returns the focused PWinObj.
    static inline PWinObj *getFocusedPWinObj(void) { return _focused_wo; }
    //! @brief Returns the PWinObj representing the root Window.
    static inline PWinObj *getRootPWinObj(void) { return _root_wo; }
    //! @brief Sets the focused PWinObj.
    static inline void setFocusedPWinObj(PWinObj *wo) { _focused_wo = wo; }
    //! @brief Sets the PWinObj representing the root Window.
    static inline void setRootPWinObj(PWinObj *wo) { _root_wo = wo; }
    //! @brief Checks if focused window is of type
    static inline bool isFocusedPWinObj(Type type) {
        return _focused_wo ? _focused_wo->getType() == type : false;
    }

    //! @brief Searches for the PWinObj matching Window win.
    //! @param win Window to match PWinObjs against.
    //! @return PWinObj pointer on match, else 0.
    static PWinObj *findPWinObj(Window win) {
        auto it(_wo_map.find(win));
        return (it != _wo_map.end()) ? it->second : 0;
    }

    //! @brief Searches in PWinObj list if PWinObj wo exists.
    //! @param wo PWinObj to search for.
    //! @return true if found, else false.
    static inline bool windowObjectExists(PWinObj *wo) {
        auto it = std::find(_wo_list.begin(), _wo_list.end(), wo);
        if (it != _wo_list.end())
            return true;
        return false;
    }

    static bool isSkipEnterAfter(Window win) {
        return (win == _win_skip_enter_after
                || (_skip_enter_after != nullptr && *_skip_enter_after == win));
    }

    static void setSkipEnterAfter(PWinObj *wo) {
        _win_skip_enter_after = None;
        _skip_enter_after = wo;
    }

    //! @brief Return Window this PWinObj represents.
    inline Window getWindow(void) const { return _window; }
    //! @brief Sets Window this PWinObj represents.
    inline void setWindow(Window window) { _window = window; }
    //! @brief Returns parent PWinObj.
    inline PWinObj *getParent(void) const { return _parent; }
    //! @brief Sets parent PWinObj.
    inline void setParent(PWinObj *wo) { _parent = wo; }
    //! @brief Returns type of PWinObj.
    inline Type getType(void) const { return _type; }
    //! @brief Returns the last activity time for this window
    inline Time getLastActivity(void) const { return _lastActivity; }
    //! @brief Sets the last activity time for this window.
    inline void setLastActivity(Time lastActivity) {
        _lastActivity = lastActivity;
    }

    inline void addChildWindow(Window win) { _wo_map[win] = this; }
    inline void removeChildWindow(Window win) { _wo_map.erase(win); }

    //! @brief Returns x coordinate of PWinObj.
    inline int getX(void) const { return _gm.x; }
    //! @brief Returns y coordinate of PWinObj.
    inline int getY(void) const { return _gm.y; }
    //! @brief Returns right edge x coordinate of PWinObj.
    inline int getRX(void) const { return _gm.x + _gm.width; }
    //! @brief Returns bottom edge y coordinate of PWinObj.
    inline int getBY(void) const { return _gm.y + _gm.height; }

    //! @brief Returns width of PWinObj.
    inline uint getWidth(void) const { return _gm.width; }
    //! @brief Returns height of PWinObj:
    inline uint getHeight(void) const { return _gm.height; }

    //! @brief Sets gm to geometry of window.
    inline void getGeometry(Geometry &gm) const { gm = _gm; }

    uint getHead(void);
    //! @brief Returns workspace PWinObj is on.
    inline uint getWorkspace(void) const { return _workspace; }
    /** @brief Returns layer PWinObj is in. */
    inline Layer getLayer(void) const { return _layer; }

    //! @brief Returns mapped state of PWinObj.
    inline bool isMapped(void) const { return _mapped; }
    //! @brief Returns iconofied state of PWinObj.
    inline bool isIconified(void) const { return _iconified; }
    //! @brief Returns hidden state of PWinObj.
    inline bool isHidden(void) const { return _hidden; }
    //! @brief Returns focused state of PWinObj.
    inline bool isFocused(void) const { return _focused; }
    //! @brief Returns sticky state of PWinObj.
    inline bool isSticky(void) const { return _sticky; }
    //! @brief Returns Focusable state of PWinObj.
    inline bool isFocusable(void) const { return _focusable; }
    //! @brief Returns true if the PWinObj is interested in keyboard input
    inline bool isKeyboardInput(void) const { return _keyboard_input; }

    //! @brief Returns transparency state of PWinObj
    inline bool isOpaque(void) const { return _opaque; }
    void setOpacity(uint focused, uint unfocused, bool enabled=true);
    inline void setOpacity(uint value) { setOpacity(value, value); }
    inline void setOpacity(PWinObj *child) {
        setOpacity(child->_opacity.focused,
                   child->_opacity.unfocused,
                   !child->_opaque);
    }
    void updateOpacity(void);
    void setOpaque(bool opaque);

    //! Returns true if a custom region of kind @kind (ShapeBounding
    //! or ShapeInput) was set via the shape extension for this window.
    inline bool hasShapeRegion(int kind) const
    {
        if (kind == ShapeBounding) {
            return _shape_bounding;
        } else if (kind == ShapeInput) {
            return _shape_input;
        }
        return false;
    }

    // interface
    virtual void mapWindow(void);
    virtual void mapWindowRaised(void);
    virtual void unmapWindow(void);
    virtual void iconify(void);
    virtual void stick(void);

    virtual void move(int x, int y);
    virtual void resize(uint width, uint height);
    virtual void moveResize(int x, int y, uint width, uint height);

    //! @brief Raises PWinObj without respect of layer.
    virtual void raise(void) { X11::raiseWindow(_window); }
    //! @brief Lowers PWinObj without respect of layer.
    virtual void lower(void) { X11::lowerWindow(_window); }

    virtual void setWorkspace(uint workspace);
    virtual void setLayer(Layer layer);
    virtual void setFocused(bool focused);
    virtual void setSticky(bool sticky);

    /** Set focusable flag. */
    virtual void setFocusable(bool focusable) { _focusable = focusable; }
    virtual void setHidden(bool hidden);

    virtual void giveInputFocus(void);
    virtual void reparent(PWinObj *parent, int x, int y);

    virtual bool getSizeRequest(Geometry &request);

    // event interface

    virtual ActionEvent *handleButtonPress(XButtonEvent*) { return nullptr; }
    virtual ActionEvent *handleButtonRelease(XButtonEvent*) { return nullptr; }
    virtual ActionEvent *handleKeyPress(XKeyEvent*) { return nullptr; }
    virtual ActionEvent *handleKeyRelease(XKeyEvent*) { return nullptr; }
    virtual ActionEvent *handleMotionEvent(XMotionEvent*) { return nullptr; }
    virtual ActionEvent *handleEnterEvent(XCrossingEvent*) { return nullptr; }
    virtual ActionEvent *handleLeaveEvent(XCrossingEvent*) { return nullptr; }
    virtual ActionEvent *handleExposeEvent(XExposeEvent*) { return nullptr; }
    virtual ActionEvent *handleMapRequest(XMapRequestEvent*) { return nullptr; }
    virtual ActionEvent *handleUnmapEvent(XUnmapEvent*) { return nullptr; }

    // operators

    //! @brief Operator matching against Window PWinObj represents..
    virtual bool operator == (const Window &window) {
        return (_window == window);
    }
    //! @brief Operator matching against Window PWinObj represents.
    virtual bool operator != (const Window &window) {
        return (_window != window);
    }

    // other window commands

public:
    static PWinObjDeleted pwin_obj_deleted;

protected:
    static void woListAdd(PWinObj *wo);
    static void woListRemove(PWinObj *wo);

protected:
    Window _window; //!< Window PWinObj represents.
    PWinObj *_parent; //!< Parent PWinObj.

    Type _type; //!< Type of PWinObj.
    Time _lastActivity; //!< Last time PWinObj received input.

    // Opacity information
    class Opacity {
    public:
        Opacity(void)
            : current(EWMH_OPAQUE_WINDOW),
              focused(EWMH_OPAQUE_WINDOW),
              unfocused(EWMH_OPAQUE_WINDOW) { }
        uint current, focused, unfocused;
    } _opacity;
    bool _opaque; //!< Opaque set state of PWinObj

    Geometry _gm; //!< Geometry of PWinObj (always in absolute coordinates).
    uint _workspace; //!< Workspace PWinObj is on.
    Layer _layer; //!< Layer PWinObj is in.
    bool _mapped:1; //!< Mapped state of PWinObj.
    bool _iconified:1; //!< Iconified state of PWinObj.
    bool _hidden:1; //!< Hidden state of PWinObj.
    bool _focused:1; //!< Focused state of PWinObj.
    bool _sticky:1; //!< Sticky state of PWinObj.
    bool _focusable:1; //!< Focusable state of PWinObj.
    bool _shape_bounding:1; //!< _window has a custom bounding region (shape)
    bool _shape_input:1; //!< _window has a custom input region (shape)
    bool _keyboard_input:1; //!< PWinObj is consuming keyboard input.

    /**
     * Window to skip (instead of PWinObj), set on PWinObj destructor
     * to preserve skip functionality when PWinObj goes away.
     */
    static Window _win_skip_enter_after;
    static PWinObj *_skip_enter_after;

    static PWinObj *_root_wo; //!< Static root PWinObj pointer.
    static PWinObj *_focused_wo; //!< Static focused PWinObj pointer.
    static std::vector<PWinObj*> _wo_list; //!< List of PWinObjs.
    static std::map<Window, PWinObj*> _wo_map; //!< Mapping of Window to PWinObj
};
