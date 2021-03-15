//
// PDecor.cc for pekwm
// Copyright (C) 2004-2020 the pekwm development team
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "config.h"

#include <functional>
#include <algorithm>

extern "C" {
#include <X11/Xlib.h>
#include <X11/Xutil.h>
}

#include "Charset.hh"
#include "Compat.hh"
#include "Config.hh"
#include "Debug.hh"
#include "PWinObj.hh"
#include "PFont.hh"
#include "PDecor.hh"
#include "PTexture.hh"
#include "PTexturePlain.hh" // PTextureSolid
#include "ActionHandler.hh"
#include "ManagerWindows.hh"
#include "StatusWindow.hh"
#include "KeyGrabber.hh"
#include "Theme.hh"
#include "Workspaces.hh"
#include "X11.hh"
#include "X11Util.hh"

// PDecor::Button

//! @brief PDecor::Button constructor
PDecor::Button::Button(PWinObj *parent, Theme::PDecorButtonData *data,
                       uint width, uint height)
  : PWinObj(true),
    _data(data), _state(BUTTON_STATE_UNFOCUSED),
    _left(_data->isLeft())
{
    _parent = parent;

    _gm.width = width;
    _gm.height = height;

    XSetWindowAttributes attr;
    attr.event_mask = EnterWindowMask|LeaveWindowMask;
    attr.override_redirect = True;
    _window =
        X11::createWindow(_parent->getWindow(),
                          -_gm.width, -_gm.height, _gm.width, _gm.height, 0,
                          CopyFromParent, InputOutput, CopyFromParent,
                          CWEventMask|CWOverrideRedirect, &attr);
    setState(_state);
}

//! @brief PDecor::Button destructor
PDecor::Button::~Button(void)
{
    X11::destroyWindow(_window);
}

//! @brief Searches the PDecorButtonData for an action matching ev
ActionEvent*
PDecor::Button::findAction(XButtonEvent *ev)
{
    auto it(_data->begin());
    for (; it != _data->end(); ++it) {
        if (it->mod == ev->state && it->sym == ev->button)
            return &*it;
    }
    return 0;
}

//! @brief Sets the state of the button
void
PDecor::Button::setState(ButtonState state)
{
    if (state == BUTTON_STATE_NO) {
        return;
    }

    // Only update if we don't hoover, we want to be able to turn back
    if (state != BUTTON_STATE_HOVER) {
        _state = state;
    }

    auto tex = _data->getTexture(state);
    if (tex) {
        tex->setBackground(_window, 0, 0, _gm.width, _gm.height);

        if (X11::hasExtensionShape() && _data->setShape()) {
            bool need_free;
            auto shape = tex->getMask(0, 0, need_free);
            if (shape != None) {
                X11::shapeSetMask(_window, ShapeBounding, shape);
                if (need_free) {
                    X11::freePixmap(shape);
                }
            } else {
                XRectangle rect = {0 /* x */, 0 /* y */,
                                   static_cast<short uint>(_gm.width),
                                   static_cast<short uint>(_gm.height)};
                X11::shapeSetRect(_window, &rect);
            }
        }

        X11::clearWindow(_window);
    }
}

PDecor::TitleItem::TitleItem(void)
    : _count(0),
      _id(0),
      _info(0),
      _width(0)
{
}

void
PDecor::TitleItem::infoAdd(enum Info info)
{
    _info |= info;
}

void
PDecor::TitleItem::infoRemove(enum Info info)
{
    _info &= ~info;
}

bool
PDecor::TitleItem::infoIs(enum Info info)
{
    return (_info&info);
}

//! @brief Update visible version of title
void
PDecor::TitleItem::updateVisible(void) {
    // Start with empty string
    _visible = L"";

    // Add client info to title
    if ((_info != 0)
        && ((_info != INFO_ID) || pekwm::config()->isShowClientID())) {
        _visible.append(L"[");

        if (infoIs(INFO_ID) && pekwm::config()->isShowClientID()) {
            _visible.append(Charset::to_wide_str(std::to_string(_id)));
        }
        if (infoIs(INFO_MARKED)) {
            _visible.append(L"M");
        }

        _visible.append(L"] ");
    }

    // Add title
    if (_user.size() > 0) {
        _visible.append(_user);
    } else if (_custom.size() > 0) {
        _visible.append(_custom);
    } else {
        _visible.append(_real);
    }

    // Add client number to title
    if (_count > 0) {
      _visible.append(Charset::to_wide_str(pekwm::config()->getClientUniqueNamePre()));
      _visible.append(Charset::to_wide_str(std::to_string(_count)));
      _visible.append(Charset::to_wide_str(pekwm::config()->getClientUniqueNamePost()));
    }
}

// PDecor

const std::string PDecor::DEFAULT_DECOR_NAME = "DEFAULT";
const std::string PDecor::DEFAULT_DECOR_NAME_BORDERLESS = "BORDERLESS";
const std::string PDecor::DEFAULT_DECOR_NAME_TITLEBARLESS = "TITLEBARLESS";
const std::string PDecor::DEFAULT_DECOR_NAME_ATTENTION = "ATTENTION";

std::vector<PDecor*> PDecor::_pdecors;

//! @brief PDecor constructor
//! @param dpy Display
//! @param decor_name String, if not DEFAULT_DECOR_NAME sets _decor_name_saved
PDecor::PDecor(const std::string &decor_name, const Window child_window,
               bool init)
    : PWinObj(true),
      ThemeGm(nullptr),
      _decor_name(decor_name),
      _child(0),
      _button(0),
      _button_press_win(None),
      _pointer_x(0),
      _pointer_y(0),
      _click_x(0),
      _click_y(0),
      _decor_cfg_child_move_overloaded(false),
      _decor_cfg_bpr_replay_pointer(false),
      _decor_cfg_bpr_al_child(MOUSE_ACTION_LIST_CHILD_OTHER),
      _decor_cfg_bpr_al_title(MOUSE_ACTION_LIST_TITLE_OTHER),
      _maximized_vert(false),
      _maximized_horz(false),
      _fullscreen(false),
      _skip(0),
      _data(nullptr),
      _border(true),
      _titlebar(true),
      _shaded(false),
      _attention(0),
      _need_shape(false),
      _real_height(1),
      _title_wo(true),
      _title_active(0),
      _titles_left(0),
      _titles_right(1)
{
    if (init) {
        this->init(child_window);
    }
    _pdecors.push_back(this);
}

void
PDecor::init(Window child_window)
{
    if (_decor_name.empty()) {
        _decor_name = getDecorName();
    }
    if (_decor_name != PDecor::DEFAULT_DECOR_NAME) {
        _decor_name_saved = _decor_name;
    }

    setDataFromDecorName(_decor_name);

    CreateWindowParams window_params;
    createParentWindow(window_params, child_window);
    createTitle(window_params);
    createBorder(window_params);

    // sets buttons etc up
    loadDecor();

    // map title and border windows
    XMapSubwindows(X11::getDpy(), _window);
}

/**
 * Create window attributes
 */
void
PDecor::createParentWindow(CreateWindowParams &params, Window child_window)
{
    params.mask = CWOverrideRedirect|CWEventMask|CWBorderPixel|CWBackPixel;
    params.depth = CopyFromParent;
    params.visual = CopyFromParent;
    params.attr.override_redirect = True;
    params.attr.border_pixel = X11::getBlackPixel();
    params.attr.background_pixel = X11::getBlackPixel();

    if (child_window != None) {
        XWindowAttributes attr;
        if (X11::getWindowAttributes(child_window, &attr)
              && 32 != X11::getDepth()
              && attr.depth == 32) {
            params.mask |= CWColormap;
            params.depth = attr.depth;
            params.visual = attr.visual;
            params.attr.colormap = XCreateColormap(X11::getDpy(),
                                                   X11::getRoot(),
                                                   params.visual, AllocNone);
        }
    }
    params.attr.event_mask = ButtonPressMask|ButtonReleaseMask|
        ButtonMotionMask|EnterWindowMask|SubstructureRedirectMask|
        SubstructureNotifyMask;
    _window = X11::createWindow(X11::getRoot(),
                                _gm.x, _gm.y, _gm.width, _gm.height, 0,
                                params.depth, InputOutput, params.visual,
                                params.mask, &params.attr);

    if (params.mask & CWColormap) {
        XFreeColormap(X11::getDpy(), params.attr.colormap);
        params.depth = X11::getDepth();
        params.visual = X11::getVisual();
        params.attr.colormap = X11::getColormap();
    }
}

/**
 * Create title window.
 */
void
PDecor::createTitle(CreateWindowParams &params)
{
    params.attr.event_mask = ButtonPressMask|ButtonReleaseMask|
        ButtonMotionMask|EnterWindowMask;
    auto title = X11::createWindow(_window,
                                   bdLeft(this), bdTop(this), 1, 1, 0,
                                   params.depth, InputOutput, params.visual,
                                   params.mask, &params.attr);
    _title_wo.setWindow(title);
    addChildWindow(_title_wo.getWindow());
    _title_wo.mapWindow();
}

/**
 * Create border windows
 */
void
PDecor::createBorder(CreateWindowParams &params)
{
    params.attr.event_mask = ButtonPressMask|ButtonReleaseMask|
        ButtonMotionMask|EnterWindowMask;

    for (uint i = 0; i < BORDER_NO_POS; ++i) {
        params.attr.cursor = X11::getCursor(CursorType(i));

        _border_win[i] =
            X11::createWindow(_window, -1, -1, 1, 1, 0,
                              params.depth, InputOutput, params.visual,
                              params.mask|CWCursor, &params.attr);
        addChildWindow(_border_win[i]);
    }
}

//! @brief PDecor destructor
PDecor::~PDecor(void)
{
    _pdecors.erase(std::remove(_pdecors.begin(), _pdecors.end(), this),
                    _pdecors.end());

    while (! _children.empty()) {
        removeChild(_children.back(), false); // Don't call delete this.
    }

    // Make things look smoother, buttons will be noticed as deleted
    // otherwise. Using X call directly to avoid re-drawing and other
    // special features not required when removing the window.
    X11::unmapWindow(_window);

    // free buttons
    unloadDecor();

    removeChildWindow(_title_wo.getWindow());
    X11::destroyWindow(_title_wo.getWindow());
    for (uint i = 0; i < BORDER_NO_POS; ++i) {
        removeChildWindow(_border_win[i]);
        X11::destroyWindow(_border_win[i]);
    }
    X11::destroyWindow(_window);
}

// START - PWinObj interface.

//! @brief Map decor and all children
void
PDecor::mapWindow(void)
{
    if (! _mapped) {
        PWinObj::mapWindow();
        for_each(_children.begin(), _children.end(),
                 std::mem_fn(&PWinObj::mapWindow));
    }
}

//! @brief Maps the window raised
void
PDecor::mapWindowRaised(void)
{
    if (_mapped) {
        return;
    }

    mapWindow();

    raise(); // XMapRaised wouldn't preserver layers
}

//! @brief Unmaps decor and all children
void
PDecor::unmapWindow(void)
{
    if (_mapped) {
        if (_iconified) {
            for_each(_children.begin(), _children.end(),
                     std::mem_fn(&PWinObj::iconify));
        } else {
            for_each(_children.begin(), _children.end(),
                     std::mem_fn(&PWinObj::unmapWindow));
        }
        PWinObj::unmapWindow();
    }
}

//! @brief Moves the decor
void
PDecor::move(int x, int y)
{
    // update real position
    PWinObj::move(x, y);
    if (_child && (_decor_cfg_child_move_overloaded)) {
        _child->move(x + bdLeft(this), y + bdTop(this) + titleHeight(this));
    }
}

//! @brief Resizes the decor, and active child if any
void
PDecor::resize(uint width, uint height)
{
    // If shaded, don't resize to the size specified just update width
    // and set _real_height to height
    if (_shaded) {
        _real_height = height;

        height = titleHeight(this);
        // Shading in non full width title mode will make border go away
        if (! _data->getTitleWidthMin()) {
            height += bdTop(this) + bdBottom(this);
        }
    }

    PWinObj::resize(width, height);

    // Update size before moving and shaping the rest as shaping
    // depends on the child window
    if (_child) {
        _child->resize(getChildWidth(), getChildHeight());
    }

    // place / resize title and border
    resizeTitle();
    placeBorder();

    // Set and apply shape on window, all parts of the border can now
    // be shaped.
    setBorderShape();
    applyBorderShape();

    renderTitle();
    renderBorder();
}

void
PDecor::moveResize(const Geometry &gm, int gm_mask)
{
    if (gm_mask & (X_VALUE|Y_VALUE)) {
        if (gm_mask & X_VALUE) {
            _gm.x = gm.x;
        }
        if (gm_mask & Y_VALUE) {
            _gm.y = gm.y;
        }
        move(_gm.x, _gm.y);
    }

    if (gm_mask & (WIDTH_VALUE|HEIGHT_VALUE)) {
        if (gm_mask & WIDTH_VALUE) {
            _gm.width = gm.width;
        }
        if (gm_mask & HEIGHT_VALUE) {
            _gm.height = gm.height;
        }
        resize(_gm.width, _gm.height);
    }
}

//! @brief Move and resize window.
void
PDecor::moveResize(int x, int y, uint width, uint height)
{
    // If shaded, don't resize to the size specified just update width
    // and set _real_height to height
    if (_shaded) {
        _real_height = height;

        height = titleHeight(this);
        // Shading in non full width title mode will make border go away
        if (! _data->getTitleWidthMin()) {
            height += bdTop(this) + bdBottom(this);
        }
    }

    PWinObj::moveResize(x, y, width, height);

    // Update size before moving and shaping the rest as shaping
    // depends on the child window
    if (_child) {
        _child->moveResize(x + bdLeft(this), y + bdTop(this) + titleHeight(this),
                          getChildWidth(), getChildHeight());

        // The client window may have its window gravity set to something different
        // than NorthWestGravity (see Xlib manual chapter 3.2.3). Therefore the
        // X server may not move its top left corner along with the decoration.
        // We correct these cases by calling alignChild(). It is called only for
        // _child and not all members of _children, because activateChild.*()
        // does it itself.
        alignChild(_child);
    }

    // Place and resize title and border
    resizeTitle();
    placeBorder();

    // Apply shape on window, all parts of the border can now be shaped.
    setBorderShape();
    applyBorderShape();

    renderTitle();
    renderBorder();
}

void
PDecor::resizeTitle(void)
{
    if (titleHeight(this)) {
       _title_wo.resize(calcTitleWidth(), titleHeight(this));
        calcTabsWidth();
    }

    // place buttons, also updates title information
    placeButtons();
}

//! @brief Raises the window, taking _layer into account
void
PDecor::raise(void)
{
    Workspaces::raise(this);
    if (_type == PWinObj::WO_FRAME) {
        Workspaces::updateClientStackingList();
    }
}

//! @brief Lowers the window, taking _layer into account
void
PDecor::lower(void)
{
    Workspaces::lower(this);
    if (_type == PWinObj::WO_FRAME) {
        Workspaces::updateClientStackingList();
    }
}

void
PDecor::setFocused(bool focused)
{
    if (_focused != focused) { // save repaints
        PWinObj::setFocused(focused);

        renderTitle();
        renderButtons();

        renderBorder();
        setBorderShape();
        applyBorderShape();
    }
}

void
PDecor::setWorkspace(uint workspace)
{
    if (workspace != NET_WM_STICKY_WINDOW) {
        if (workspace >= Workspaces::size()) {
            LOG("this == " << this << " workspace == " << workspace
                << " >= number of workspaces == " << Workspaces::size());
            workspace = Workspaces::size() - 1;
        }
        _workspace = workspace;
    }

    for (auto it : _children) {
        it->setWorkspace(workspace);
    }

    if (! _mapped && ! _iconified) {
        if (_sticky || (_workspace == Workspaces::getActive())) {
            mapWindow();
        }
    } else if (! _sticky && (_workspace != Workspaces::getActive())) {
        unmapWindow();
    }
}

//! @brief Gives decor input focus, fails if not mapped or not visible
void
PDecor::giveInputFocus(void)
{
    if (_mapped && _child) {
        _child->giveInputFocus();
    } else {
        LOG("this == " << this << " - reverting to root");
        PWinObj::getRootPWinObj()->giveInputFocus();
    }
}

/**
 * Handle button press events.
 */
ActionEvent*
PDecor::handleButtonPress(XButtonEvent *ev)
{
    ActionEvent *ae = 0;
    std::vector<ActionEvent> *actions = 0;

    // Remove state modifiers from event
    X11::stripStateModifiers(&ev->state);
    X11::stripButtonModifiers(&ev->state);

    // Try to do something about frame buttons
    if (ev->subwindow != None && (_button = findButton(ev->subwindow)) != 0) {
        ae = handleButtonPressButton(ev, _button);

    } else {
        // Record position, used in the motion handler when doing a window move.
        _click_x = _gm.x;
        _click_y = _gm.y;
        _pointer_x = ev->x_root;
        _pointer_y = ev->y_root;

        // Allow us to get clicks from anywhere on the window.
        if (_decor_cfg_bpr_replay_pointer && ev->window == _window) {
            X11::allowEvents(ReplayPointer, CurrentTime);
        }

        auto cfg = pekwm::config();
        if (ev->window == _child->getWindow()
            || (ev->state == 0 && ev->subwindow == _child->getWindow())) {
            // Clicks on the child window
            // NOTE: If the we're matching against the subwindow we need to make
            // sure that the state is 0, meaning we didn't have any modifier
            // because if we don't care about the modifier we'll get two actions
            // performed when using modifiers.
            _button_press_win = _child->getWindow();
            actions =
                cfg->getMouseActionList(_decor_cfg_bpr_al_child);
        } else if (_title_wo == ev->window) {
            // Clicks on the decor title
            _button_press_win = ev->window;
            actions = cfg->getMouseActionList(_decor_cfg_bpr_al_title);
        } else {
            // Clicks on the decor border, default case. Try both
            // window and sub-window.
            uint pos = getBorderPosition(ev->window);
            if (pos != BORDER_NO_POS) {
                _button_press_win = ev->window;
                actions = cfg->getBorderListFromPosition(pos);
            }
        }
    }

    if (! ae && actions) {
        ae = ActionHandler::findMouseAction(ev->button, ev->state,
                                            MOUSE_EVENT_PRESS, actions);
    }

    return ae;
}

/**
 * Handle button press events pressing decor buttons.
 */
ActionEvent*
PDecor::handleButtonPressButton(XButtonEvent *ev, PDecor::Button *button)
{
    // Keep track of pressed button.
    _button->setState(BUTTON_STATE_PRESSED);

    ActionEvent *ae = _button->findAction(ev);

    // if the button is used for resizing, we don't want to wait for release
    if (ae && ae->isOnlyAction(ACTION_RESIZE)) {
        _button->setState(_focused
                          ? BUTTON_STATE_FOCUSED : BUTTON_STATE_UNFOCUSED);
        _button = 0;
    } else {
        ae = 0;
    }

    return ae;
}

/**
 * Handle button release events.
 */
ActionEvent*
PDecor::handleButtonRelease(XButtonEvent *ev)
{
    ActionEvent *ae = 0;
    std::vector<ActionEvent> *actions = 0;
    MouseEventType mb = MOUSE_EVENT_RELEASE;

    // Remove state modifiers from event
    X11::stripStateModifiers(&ev->state);
    X11::stripButtonModifiers(&ev->state);

    // handle titlebar buttons
    if (_button) {
        ae = handleButtonReleaseButton(ev, _button);

    } else {
        // Allow us to get clicks from anywhere on the window.
        if (_decor_cfg_bpr_replay_pointer && ev->window == _window) {
            X11::allowEvents(ReplayPointer, CurrentTime);
        }

        // clicks on the child window
        auto cfg = pekwm::config();
        if (ev->window == _child->getWindow()
            || (ev->state == 0 && ev->subwindow == _child->getWindow())) {
            // NOTE: If the we're matching against the subwindow we need to make
            // sure that the state is 0, meaning we didn't have any modifier
            // because if we don't care about the modifier we'll get two actions
            // performed when using modifiers.
            if (_button_press_win == _child->getWindow()) {
                actions = cfg->getMouseActionList(_decor_cfg_bpr_al_child);
            }

        } else if (_title_wo == ev->window) {
            if (_button_press_win == ev->window) {
                // Handle clicks on the decor title, checking double
                // clicks first.
                if (X11::isDoubleClick(ev->window, ev->button - 1, ev->time,
                                       cfg->getDoubleClickTime())) {
                    X11::setLastClickID(ev->window);
                    X11::setLastClickTime(ev->button - 1, 0);
                    mb = MOUSE_EVENT_DOUBLE;
                } else {
                    X11::setLastClickID(ev->window);
                    X11::setLastClickTime(ev->button - 1, ev->time);
                }

                actions = cfg->getMouseActionList(_decor_cfg_bpr_al_title);
            }
        } else {
            // Clicks on the decor border, check subwindow then window.
            uint pos = getBorderPosition(ev->window);
            if (pos != BORDER_NO_POS && (_button_press_win == ev->window)) {
                actions = cfg->getBorderListFromPosition(pos);
            }
        }
    }

    if (! ae && actions) {
        ae = ActionHandler::findMouseAction(ev->button, ev->state, mb, actions);
    }

    return ae;
}

/**
 * Handle button release events when button is in pressed state.
 */
ActionEvent*
PDecor::handleButtonReleaseButton(XButtonEvent *ev, PDecor::Button *button)
{
    // First restore the pressed buttons state
    _button->setState(_focused ? BUTTON_STATE_FOCUSED : BUTTON_STATE_UNFOCUSED);

    ActionEvent *ae = 0;

    // Then see if the button was released over ( to execute an action )
    if (*_button == ev->subwindow) {
        ae = _button->findAction(ev);

        // This is a little hack, resizing isn't wanted on both press and release
        if (ae && ae->isOnlyAction(ACTION_RESIZE)) {
            ae = 0;
        }
    }

    _button = 0;

    return ae;
}

ActionEvent*
PDecor::handleMotionEvent(XMotionEvent *ev)
{
    uint button = X11::getButtonFromState(ev->state);
    return ActionHandler::findMouseAction(button, ev->state,
                                          MOUSE_EVENT_MOTION,
                                          pekwm::config()->getMouseActionList(MOUSE_ACTION_LIST_OTHER));
}

/**
 * Handle enter event, find action and toggle hoover state if enter
 * was on a button.
 */
ActionEvent*
PDecor::handleEnterEvent(XCrossingEvent *ev)
{
    PDecor::Button *button = findButton(ev->window);
    if (button) {
        button->setState(BUTTON_STATE_HOVER);
    }

    return ActionHandler::findMouseAction(BUTTON_ANY, ev->state, MOUSE_EVENT_ENTER,
                                          pekwm::config()->getMouseActionList(MOUSE_ACTION_LIST_OTHER));
}

/**
 * Handle leave event, find action and toggle hoover state if leave
 * was from a button.
 */
ActionEvent*
PDecor::handleLeaveEvent(XCrossingEvent *ev)
{
    PDecor::Button *button = findButton(ev->window);
    if (button) {
        button->setState(button->getState());
    }

    return ActionHandler::findMouseAction(BUTTON_ANY, ev->state, MOUSE_EVENT_LEAVE,
                                          pekwm::config()->getMouseActionList(MOUSE_ACTION_LIST_OTHER));
}

// END - PWinObj interface.

//! @brief Adds a children in another decor to this decor
void
PDecor::addDecor(PDecor *decor)
{
    if (this == decor) {
        LOG("this == decor (" << this << ")");
        return;
    }

    auto it(decor->begin());
    for (; it != decor->end(); ++it) {
        addChild(*it);
        (*it)->setWorkspace(_workspace);
    }

    decor->_children.clear();
    delete decor;
}

//! @return True on decor change, False if decor unchanged
bool
PDecor::updateDecor(void)
{
    std::string name = getDecorName();
    if (! _decor_name_saved.empty()) {
        _decor_name_saved = name;
        return false;
    }

    if (_decor_name == name) {
        return false;
    }

    _decor_name = name;
    loadDecor();
    return true;
}

//! @brief Sets override decor name
void
PDecor::setDecorOverride(StateAction sa, const std::string &name)
{
    if (sa == STATE_SET ||
            (sa == STATE_TOGGLE && _decor_name_saved.empty())) {
        if (_decor_name_saved.empty()) {
            _decor_name_saved = _decor_name;
        }
        if (_decor_name != name) {
            _decor_name = name;
            loadDecor();
        }
    } else if (! _decor_name_saved.empty()) {
        _decor_name = _decor_name_saved;
        _decor_name_saved.clear();
        loadDecor();
    }
}

//! @brief Load and update decor.
void
PDecor::loadDecor(void)
{
    unloadDecor();
    setDataFromDecorName(_decor_name);

    // Load decor.
    auto b_it(_data->buttonBegin());
    for (; b_it != _data->buttonEnd(); ++b_it) {
        uint width = std::max(static_cast<uint>(1), (*b_it)->getWidth() ? (*b_it)->getWidth() : titleHeight(this));
        uint height = std::max(static_cast<uint>(1), (*b_it)->getHeight() ? (*b_it)->getHeight() : titleHeight(this));

        _buttons.push_back(new PDecor::Button(&_title_wo, *b_it, width, height));
        _buttons.back()->mapWindow();
        addChildWindow(_buttons.back()->getWindow());
    }

    if (! _data->getTitleHeight() && ! _data->isTitleHeightAdapt()) {
        _title_wo.unmapWindow();
    } else {
        _title_wo.mapWindow();
    }

    // Update title position.
    if (_data->getTitleWidthMin() == 0) {
        _title_wo.move(titleLeftOffset(this), bdTop(this));
        _need_shape = false;
    } else {
        _title_wo.move(0, 0);
        _need_shape = true;
    }

    // Update child positions.
    for (auto c_it : _children) {
        alignChild(c_it);
    }

    // Make sure it gets rendered correctly.
    _focused = ! _focused;
    setFocused(! _focused);

    // Update the dimension of the frame.
    if (_child) {
        resizeChild(_child->getWidth(), _child->getHeight());
    }

    // Child theme change.
    loadTheme();
    // notify on decor change.
    decorUpdated();
}

void
PDecor::setDataFromDecorName(const std::string &decor_name)
{
    _data = pekwm::theme()->getPDecorData(_decor_name);
    if (! _data) {
        _data = pekwm::theme()->getPDecorData(DEFAULT_DECOR_NAME);
    }
    setData(_data);
    LOG_IF(!_data, "_data == 0");
}

//! @brief Frees resources used by PDecor.
void
PDecor::unloadDecor(void)
{
    // Set active button to 0 as it can not be valid after deleting
    // the current buttons.
    _button = 0;

    for (auto it : _buttons) {
        removeChildWindow(it->getWindow());
        delete it;
    }
    _buttons.clear();
}

PDecor::Button*
PDecor::findButton(Window win)
{
    for (auto it : _buttons) {
        if (*it == win) {
            return it;
        }
    }
    return 0;
}

/**
 * Returns height of PDecor ignoring shaded state.
 */
uint
PDecor::getRealHeight(void) const
{
    return (_shaded ? _real_height : _gm.height);
}

/**
 * Returns width of child container.
 */
uint
PDecor::getChildWidth(void) const
{
    if ((bdLeft(this) + bdRight(this)) >= _gm.width) {
        return 1;
    }
    return (_gm.width - bdLeft(this) - bdRight(this));
}

/**
 * Returns height of child container.
 */
uint
PDecor::getChildHeight(void) const
{
    if (decorHeight(this) >= getRealHeight()) {
        return 1;
    }
    return getRealHeight() - decorHeight(this);
}

PWinObj*
PDecor::getChildFromPos(int x)
{
    if (_children.empty() || _children.size() != _titles.size()) {
        return 0;
    }
    if (_children.size() == 1)
        return _children.front();

    if (x < static_cast<int>(_titles_left)) {
        return _children.front();
    } else if (x > static_cast<int>(_title_wo.getWidth() - _titles_right)) {
        return _children.back();
    }

    PTexture *t_sep = _data->getTextureSeparator(getFocusedState(false));
    uint sepw = t_sep?t_sep->getWidth():0;

    uint pos = _titles_left, xx = x;
    for (uint i = 0; i < _titles.size(); ++i) {
        if (xx >= pos && xx <= pos + _titles[i]->getWidth() + sepw) {
            return _children[i];
        }
        pos += _titles[i]->getWidth() + sepw;
    }

    return 0;
}

//! @brief Moves making the child be positioned at x y
void
PDecor::moveChild(int x, int y)
{
    move(x - bdLeft(this), y - bdTop(this) - titleHeight(this));
}

//! @brief Resizes the decor, giving width x height space for the child
void
PDecor::resizeChild(uint width, uint height)
{
    resize(width + bdLeft(this) + bdRight(this),
           height + bdTop(this) + bdBottom(this) + titleHeight(this));
}

//! @brief Sets border state of the decor
void
PDecor::setBorder(StateAction sa)
{
    if (! ActionUtil::needToggle(sa, _border)) {
        return;
    }

    // If we are going to remove the border, we need to check carefully
    // that we don't try to make the window disappear.
    if (! _border) {
        _border = true;
    } else if (! _shaded || _titlebar) {
        _border = false;
    }

    restackBorder();
    if (updateDecor()) {
        // updateDecorName returns true, decor already loaded no need
        // to notify decorUpdated
    } else if (_child) {
        resizeChild(_child->getWidth(), _child->getHeight());
    }
    decorUpdated();
}

//! @brief Sets titlebar state of the decor
void
PDecor::setTitlebar(StateAction sa)
{
    if (! ActionUtil::needToggle(sa, _titlebar))
        return;

    // If we are going to remove the titlebar, we need to check carefully
    // that we don't try to make the window disappear.
    if (! _titlebar) {
        _title_wo.mapWindow();
        _titlebar = true;
    } else if (! _shaded || _border) {
        _title_wo.unmapWindow();
        _titlebar = false;
    }

    if (updateDecor()) {
        // updateDecorName returns true, decor already loaded no need
        // to notify decorUpdated
    } else if (_child) {
        alignChild(_child);
        resizeChild(_child->getWidth(), _child->getHeight());
    }
    decorUpdated();
}

//! @brief Adds a child to the decor, reparenting the window
void
PDecor::addChild(PWinObj *child, std::vector<PWinObj*>::iterator *it)
{
    child->reparent(this, bdLeft(this), bdTop(this) + titleHeight(this));
    if (it == 0) {
        _children.push_back(child);
    } else {
        _children.insert(*it, child);
    }

    updatedChildOrder();

    // Sync focused state if it is the first child, the child will be
    // activated later on. If there are children here already fit the
    // child into the decor.
    if (_children.size() == 1) {
        _focused = ! _focused;
        setFocused(! _focused);
    } else {
      alignChild(child);
      child->resize(getChildWidth(), getChildHeight());
    }
}

//! @brief Removes PWinObj from this PDecor.
//! @param child PWinObj to remove from the this PDecor.
//! @param do_delete Wheter to call delete this when empty. (Defaults to true)
void
PDecor::removeChild(PWinObj *child, bool do_delete)
{
    child->reparent(PWinObj::getRootPWinObj(),
                    _gm.x + bdLeft(this),
                    _gm.y + bdTop(this) + titleHeight(this));

    auto it(find(_children.begin(), _children.end(), child));
    if (it == _children.end()) {
        LOG("this == " << this << " child == " << child
            << " - child not in _child_list, bailing out");
        return;
    }

    it = _children.erase(it);

    if (! _children.empty()) {
        if (_child == child) {
            if (it == _children.end()) {
                --it;
            }

            activateChild(*it);
        }

        updatedChildOrder();

    } else if (do_delete) {
        delete this; // no children, and we don't want empty PDecors
    }
}

//! @brief Activates the child, updating size and position
void
PDecor::activateChild(PWinObj *child)
{
    // sync state
    _focusable = child->isFocusable();

    alignChild(child); // place correct acording to border and title
    child->resize(getChildWidth(), getChildHeight());
    child->raise();
    _child = child;

    restackBorder();

    updatedChildOrder();
}

void
PDecor::getDecorInfo(wchar_t *buf, uint size, const Geometry& gm)
{
    swprintf(buf, size, L"%dx%d+%d+%d", gm.width, gm.height, gm.x, gm.y);
}

void
PDecor::activateChildNum(uint num)
{
    if (num >= _children.size()) {
        LOG("this == " << this << " num == " << num
             << " >= _children.size() == " << _children.size());
        return;
    }

    activateChild(_children[num]);
}

void
PDecor::activateChildRel(int off)
{
    std::vector<PWinObj*>::size_type cur=0, size = _children.size();
    for (; cur < size; ++cur) {
        if (_child == _children[cur]) {
            break;
        }
    }

    if (cur == size) {
        _child = _children.front();
        cur = 0;
    }

    off = (off+signed(cur))%signed(size);
    if (off < 0) {
        off += size;
    }
    activateChild(_children[off]);
}

//! @brief Moves the current child off steps
//! @param off - for left and + for right
void
PDecor::moveChildRel(int off)
{
    std::vector<PWinObj*>::size_type idx=0, size = _children.size();
    for (; idx < size; ++idx) {
        if (_child == _children[idx]) {
            break;
        }
    }

    if (idx == size) {
        if (_children.empty()) {
            ERR("_children is empty! off == " << off << " Please report.");
            return;
        }
        _child = _children.front();
        idx = 0;
    }

    off = (off+idx)%size;
    if (off < 0) {
        off += size;
    }

    _children.erase(std::remove(_children.begin(), _children.end(), _child),
                    _children.end());
    _children.insert(_children.begin()+off, _child);
    updatedChildOrder();
}

//! @brief Sets shaded state
void
PDecor::setShaded(StateAction sa)
{
    if (! ActionUtil::needToggle(sa, _shaded))
        return;

    // If we are going to shade the window, we need to check carefully
    // that we don't try to make the window disappear.
    if (_shaded) {
        _gm.height = _real_height;
        _shaded = false;
        // If we have a titlebar OR border (border will only be visible in
        // full-width mode only)
    } else if (_titlebar || (_border && ! _data->getTitleWidthMin())) {
        _real_height = _gm.height;
        _gm.height = titleHeight(this);
        // shading in non full width title mode will make border go away
        if (! _data->getTitleWidthMin()) {
            _gm.height += bdTop(this) + bdBottom(this);
        }
        _shaded = true;
    }

    setBorderShape();
    placeBorder();
    restackBorder();
    PWinObj::resize(_gm.width, _gm.height);
}

//! @brief Sets skip state.
void
PDecor::setSkip(uint skip)
{
    _skip = skip;
}

/**
 * Returns the name of the decor for the current titlebar/border state.
 */
std::string
PDecor::getDecorName(void)
{
    if (_attention) {
        return DEFAULT_DECOR_NAME_ATTENTION;
    }
    if (_titlebar && _border) {
        return DEFAULT_DECOR_NAME;
    }
    if (_border) {
        return DEFAULT_DECOR_NAME_TITLEBARLESS;
    }
    return DEFAULT_DECOR_NAME_BORDERLESS;
}

//! @brief Remove iconified state.
void PDecor::deiconify(void) {
    if (_iconified) {
        if (_workspace == Workspaces::getActive()) {
            mapWindow();
        }
        _iconified = false;
    }
}

//! @brief Renders and sets title background
void
PDecor::renderTitle(void)
{
    if (! titleHeight(this)) {
        return;
    }

    if (_data->getTitleWidthMin()) {
        resizeTitle();
        applyBorderShape(); // update title shape
    } else {
        calcTabsWidth();
    }

    auto t_sep = _data->getTextureSeparator(getFocusedState(false));
    auto title_bg = X11::createPixmap(_title_wo.getWidth(),
                                      _title_wo.getHeight());
    // Render main background on pixmap
    auto t_main = _data->getTextureMain(getFocusedState(false));
    t_main->render(title_bg, 0, 0,
                   _title_wo.getWidth(), _title_wo.getHeight());

    PFont *font;
    bool sel; // Current tab selected flag
    uint x = _titles_left; // Position
    // Amount of horizontal padding
    uint pad_horiz =  _data->getPad(PAD_LEFT) + _data->getPad(PAD_RIGHT);

    uint size = _titles.size();
    for (uint i = 0; i < size; ++i) {
        sel = (_title_active == i);

        // render tab
        auto tab = _data->getTextureTab(getFocusedState(sel));
        tab->render(title_bg, x, 0,
                    _titles[i]->getWidth(), _title_wo.getHeight());

        font = getFont(getFocusedState(sel));
        font->setColor(_data->getFontColor(getFocusedState(sel)));

        auto trim = PFont::FONT_TRIM_MIDDLE;
        if (_titles[i]->isCustom() || _titles[i]->isUserSet()) {
            trim = PFont::FONT_TRIM_END;
        }

        font->draw(title_bg,
                   x + _data->getPad(PAD_LEFT), // X position
                   _data->getPad(PAD_UP), // Y position
                   _titles[i]->getVisible(), 0, // Text and max chars
                   _titles[i]->getWidth() - pad_horiz, // Available width
                   trim); // Type of trim

        // move to next tab (or separator if any)
        x += _titles[i]->getWidth();

        // draw separator
        if (size > 1 && i < size - 1) {
            t_sep->render(title_bg, x, 0, 0, 0);
            x += t_sep->getWidth();
        }
    }

    X11::setWindowBackgroundPixmap(_title_wo.getWindow(), title_bg);
    X11::clearWindow(_title_wo.getWindow());
    X11::freePixmap(title_bg);
}

void
PDecor::renderButtons(void)
{
    for (auto it : _buttons) {
        it->setState(_focused ? BUTTON_STATE_FOCUSED : BUTTON_STATE_UNFOCUSED);
    }
}

void
PDecor::renderBorder(void)
{
    if (! _border) {
        return;
    }

    uint width, height;
    FocusedState state = getFocusedState(false);
    for (int i=0; i < BORDER_NO_POS; ++i) {
        auto tex =
            _data->getBorderTexture(state, static_cast<BorderPosition>(i));
        getBorderSize(static_cast<BorderPosition>(i), width, height);
        tex->setBackground(_border_win[i], 0, 0, width, height);
        X11::clearWindow(_border_win[i]);
    }
}

//! @brief Sets shape on the border windows.
void
PDecor::setBorderShape(void)
{
#ifdef HAVE_SHAPE
    Pixmap pix;
    bool do_free;
    unsigned int width, height;
    PTexture *tex;
    FocusedState state = getFocusedState(false);

    XRectangle rect = {0, 0, 0, 0};

    for (int i=0; i < BORDER_NO_POS; ++i) {
        // Get the size of the border at position
        getBorderSize(static_cast<BorderPosition>(i), width, height);

        tex = _data->getBorderTexture(state, static_cast<BorderPosition>(i));
        pix = tex->getMask(width, height, do_free);

        if (pix != None) {
            _need_shape = true;
            X11::shapeSetMask(_border_win[i], ShapeBounding, pix);
            if (do_free) {
                X11::freePixmap(pix);
            }
        } else {
            rect.width = width;
            rect.height = height;
            X11::shapeSetRect(_border_win[i], &rect);
        }
    }
#endif // HAVE_SHAPE
}

void
PDecor::checkSnap(PWinObj *skip_wo, Geometry &gm)
{
    auto cfg = pekwm::config();
    if (cfg->getWOAttract() > 0 || cfg->getWOResist() > 0) {
        checkWOSnap(skip_wo, gm);
    }
    if (cfg->getEdgeAttract() > 0 || cfg->getEdgeResist() > 0) {
        checkEdgeSnap(gm);
    }
}

inline bool
isBetween(int x1, int x2, int t1, int t2)
{
    if (x1 > t1) {
        if (x1 < t2) {
            return true;
        }
    } else if (x2 > t1) {
        return true;
    }
    return false;
}

//! @todo PDecor/PWinObj doesn't have _skip property
void
PDecor::checkWOSnap(PWinObj *skip_wo, Geometry &gm)
{
    PDecor *decor;
    Geometry orig_gm = gm;

    int x = gm.x + gm.width;
    int y = gm.y + gm.height;

    int attract = pekwm::config()->getWOAttract();
    int resist = pekwm::config()->getWOResist();

    bool snapped;

    auto it = _wo_list.rbegin();
    for (; it != _wo_list.rend(); ++it) {
        if (((*it) == skip_wo)
            || ! (*it)->isMapped()
            || ((*it)->getType() != PWinObj::WO_FRAME)) {
            continue;
        }

        // Skip snapping, only valid on PDecor and up.
        decor = dynamic_cast<PDecor*>(*it);
        if (decor && decor->isSkip(SKIP_SNAP)) {
            continue;
        }

        snapped = false;

        // check snap
        if ((x >= ((*it)->getX() - attract))
            && (x <= ((*it)->getX() + resist))) {
            if (isBetween(gm.y, y, (*it)->getY(), (*it)->getBY())) {
                gm.x = (*it)->getX() - orig_gm.width;
                snapped = true;
            }
        } else if ((gm.x >= signed((*it)->getRX() - resist)) &&
                   (gm.x <= signed((*it)->getRX() + attract))) {
            if (isBetween(gm.y, y, (*it)->getY(), (*it)->getBY())) {
                gm.x = (*it)->getRX();
                snapped = true;
            }
        }

        if (y >= ((*it)->getY() - attract) && (y <= (*it)->getY() + resist)) {
            if (isBetween(gm.x, x, (*it)->getX(), (*it)->getRX())) {
                gm.y = (*it)->getY() - orig_gm.height;
                if (snapped)
                    break;
            }
        } else if ((gm.y >= signed((*it)->getBY() - resist)) &&
                   (gm.y <= signed((*it)->getBY() + attract))) {
            if (isBetween(gm.x, x, (*it)->getX(), (*it)->getRX())) {
                gm.y = (*it)->getBY();
                if (snapped)
                    break;
            }
        }
    }
}

//! @brief Snaps decor agains head edges. Only updates _gm, no real move.
//! @todo Add support for checking for harbour and struts
void
PDecor::checkEdgeSnap(Geometry &gm)
{
    int attract = pekwm::config()->getEdgeAttract();
    int resist = pekwm::config()->getEdgeResist();

    Geometry head;
    pekwm::rootWo()->getHeadInfoWithEdge(X11::getNearestHead(gm.x, gm.y),
                                         head);

    if ((gm.x >= (head.x - resist)) && (gm.x <= (head.x + attract))) {
        gm.x = head.x;
    } else if ((gm.x + gm.width) >= (head.x + head.width - attract) &&
               ((gm.x + gm.width) <= (head.x + head.width + resist))) {
        gm.x = head.x + head.width - gm.width;
    }
    if ((gm.y >= (head.y - resist)) && (gm.y <= (head.y + attract))) {
        gm.y = head.y;
    } else if (((gm.y + gm.height) >= (head.y + head.height - attract)) &&
               ((gm.y + gm.height) <= (head.y + head.height + resist))) {
        gm.y = head.y + head.height - gm.height;
    }
}


/**
 * Move child into position with regards to title and border.
 */
void
PDecor::alignChild(PWinObj *child)
{
    if (child) {
        X11::moveWindow(child->getWindow(),
                        bdLeft(this),
                        bdTop(this) + titleHeight(this));
    }
}

//! @brief Draws outline of the decor with geometry gm
void
PDecor::drawOutline(const Geometry &gm)
{
    XDrawRectangle(X11::getDpy(), X11::getRoot(),
                   pekwm::theme()->getInvertGC(),
                   gm.x, gm.y, gm.width,
                   _shaded ? _gm.height : gm.height);
}

//! @brief Places decor buttons
void
PDecor::placeButtons(void)
{
    _titles_left = 0;
    _titles_right = 0;

    for (auto it : _buttons) {
        if (it->isLeft()) {
            it->move(_titles_left, 0);
            _titles_left += it->getWidth();
        } else {
            _titles_right += it->getWidth();
            it->move(_title_wo.getWidth() - _titles_right, 0);
        }
    }
}

//! @brief Places border windows
void
PDecor::placeBorder(void)
{
    // if we have tab min == 0 then we have full width title and place the
    // border ontop, else we put the border under the title
    uint bt_off = (_data->getTitleWidthMin() > 0) ? titleHeight(this) : 0;

    if (bdTop(this) > 0) {
        X11::moveResizeWindow(_border_win[BORDER_TOP],
                              bdTopLeft(this),
                              bt_off,
                              _gm.width - bdTopLeft(this) - bdTopRight(this),
                              bdTop(this));

        X11::moveResizeWindow(_border_win[BORDER_TOP_LEFT],
                              0, bt_off,
                              bdTopLeft(this),
                              bdTopLeftHeight(this));
        X11::moveResizeWindow(_border_win[BORDER_TOP_RIGHT],
                              _gm.width - bdTopRight(this),
                              bt_off,
                              bdTopRight(this),
                              bdTopRightHeight(this));

        if (bdLeft(this)) {
            X11::moveResizeWindow(_border_win[BORDER_LEFT],
                                  0,
                                  bdTopLeftHeight(this) + bt_off,
                                  bdLeft(this),
                                  _gm.height - bdTopLeftHeight(this) - bdBottomLeftHeight(this));
        }

        if (bdRight(this)) {
            X11::moveResizeWindow(_border_win[BORDER_RIGHT],
                                  _gm.width - bdRight(this),
                                  bdTopRightHeight(this) + bt_off,
                                  bdRight(this),
                                  _gm.height - bdTopRightHeight(this) - bdBottomRightHeight(this));
        }
    } else {
        if (bdLeft(this)) {
            X11::moveResizeWindow(_border_win[BORDER_LEFT],
                                  0,
                                  titleHeight(this),
                                  bdLeft(this),
                                  _gm.height - titleHeight(this) - bdBottom(this));
        }

        if (bdRight(this)) {
            X11::moveResizeWindow(_border_win[BORDER_RIGHT],
                                  _gm.width - bdRight(this),
                                  titleHeight(this),
                                  bdRight(this),
                                  _gm.height - titleHeight(this) - bdBottom(this));
        }
    }

    if (bdBottom(this)) {
        X11::moveResizeWindow(_border_win[BORDER_BOTTOM],
                              bdBottomLeft(this),
                              _gm.height - bdBottom(this),
                              _gm.width - bdBottomLeft(this) - bdBottomRight(this),
                              bdBottom(this));
        X11::moveResizeWindow(_border_win[BORDER_BOTTOM_LEFT],
                              0,
                              _gm.height - bdBottomLeftHeight(this),
                              bdBottomLeft(this),
                              bdBottomLeftHeight(this));
        X11::moveResizeWindow(_border_win[BORDER_BOTTOM_RIGHT],
                              _gm.width - bdBottomRight(this),
                              _gm.height - bdBottomRightHeight(this),
                              bdBottomRight(this),
                              bdBottomRightHeight(this));
    }

    applyBorderShape();
}

#ifdef HAVE_SHAPE
/**
 * Apply shaping on the window based on the shape of the client and
 * the borders.
 */
void
PDecor::applyBorderShape(int kind)
{
    bool client_shape = _child ? _child->hasShapeRegion(kind) : false;
    if ((_need_shape && kind == ShapeBounding) || client_shape) {
        if (_shaded) {
            applyBorderShapeShaded(kind);
        } else {
            applyBorderShapeNormal(kind, client_shape);
        }
    } else {
        // Reinstate default region
        X11::shapeSetMask(_window, kind, None);
    }
}

void
PDecor::applyBorderShapeNormal(int kind, bool client_shape)
{
    auto shape = X11::createSimpleWindow(X11::getRoot(),
                                         0, 0, _gm.width, _gm.height,
                                         0, 0, 0);
    if (_child) {
        X11::shapeCombine(shape, kind,
                          bdLeft(this), bdTop(this) + titleHeight(this),
                          _child->getWindow(), ShapeSet);
    }

    // Apply border shapek, shaped clients do not display a border
    if (_border && ! client_shape) {
        applyBorderShapeBorder(kind, shape);
    }
    if (_titlebar) {
        X11::shapeCombine(shape, kind,
                          _title_wo.getX(), _title_wo.getY(),
                          _title_wo.getWindow(), ShapeUnion);
    }

    // The borders might extend beyond the window area
    // (causing artifacts under xcompmgr), so we cut the shape
    // down to the original window size.
    XRectangle rect_square = {
        0, 0,
        static_cast<short unsigned>(_gm.width),
        static_cast<short unsigned>(_gm.height)
    };
    X11::shapeIntersectRect(shape, &rect_square);

    X11::shapeCombine(_window, kind, 0, 0, shape, ShapeSet);
    X11::destroyWindow(shape);
}

void
PDecor::applyBorderShapeShaded(int kind)
{
    Window shape;
    if (_data->getTitleWidthMin()) {
        shape =
            X11::createSimpleWindow(X11::getRoot(),
                                    0, 0,
                                    _title_wo.getWidth(), _title_wo.getHeight(),
                                    0, 0, 0);
    } else {
        shape =
            X11::createSimpleWindow(X11::getRoot(),
                                    0, 0, _gm.width, _gm.height, 0, 0, 0);
        if (_border) {
            applyBorderShapeBorder(kind, shape);
        }
    }

    X11::shapeCombine(_window, kind, 0, 0, shape, ShapeSet);
    X11::destroyWindow(shape);
}

void
PDecor::applyBorderShapeBorder(int kind, Window shape)
{
    uint bd_top_offset = bdTopOffset(this);

    if (bdTop(this) > 0) {
        X11::shapeCombine(shape, kind, 0, bd_top_offset,
                          _border_win[BORDER_TOP_LEFT], ShapeUnion);
        X11::shapeCombine(shape, kind, bdTopLeft(this), bd_top_offset,
                          _border_win[BORDER_TOP], ShapeUnion);
        X11::shapeCombine(shape, kind,
                          _gm.width - bdTopRight(this),
                          bd_top_offset,
                          _border_win[BORDER_TOP_RIGHT], ShapeUnion);
    }

    bool use_bt_off = bd_top_offset || bdTop(this);
    if (bdLeft(this) > 0) {
        X11::shapeCombine(shape, kind,
                          0,
                          use_bt_off ? bd_top_offset + bdTopLeftHeight(this)
                                     : titleHeight(this),
                          _border_win[BORDER_LEFT], ShapeUnion);
    }

    if (bdRight(this) > 0) {
        X11::shapeCombine(shape, kind, _gm.width - bdRight(this),
                          use_bt_off ? bd_top_offset + bdTopRightHeight(this)
                                     : titleHeight(this),
                          _border_win[BORDER_RIGHT], ShapeUnion);
    }

    if (bdBottom(this) > 0) {
        X11::shapeCombine(shape, kind,
                          0, _gm.height - bdBottomLeftHeight(this),
                          _border_win[BORDER_BOTTOM_LEFT], ShapeUnion);
        X11::shapeCombine(shape, kind,
                          bdBottomLeft(this), _gm.height - bdBottom(this),
                          _border_win[BORDER_BOTTOM], ShapeUnion);
        X11::shapeCombine(shape, kind,
                          _gm.width - bdBottomRight(this),
                          _gm.height - bdBottomRightHeight(this),
                          _border_win[BORDER_BOTTOM_RIGHT],
                          ShapeUnion);
    }
}
#endif // HAVE_SHAPE

//! @brief Restacks child, title and border windows.
void
PDecor::restackBorder(void)
{
    // List of windows, adding two possible for title and child window
    int extra = 0;
    Window windows[BORDER_NO_POS + 2];


    // Only put the Child over if not shaded.
    if (_child && ! _shaded) {
        windows[extra++] = _child->getWindow();
    }
    // Add title if any
    if (_titlebar) {
        windows[extra++] = _title_wo.getWindow();
    }

    // Add border windows
    for (int i = 0; i < BORDER_NO_POS; ++i) {
         windows[i + extra] = _border_win[i];
    }

    // Raise the top window so actual restacking is done.
    X11::raiseWindow(windows[0]);
    XRestackWindows(X11::getDpy(), windows, BORDER_NO_POS + extra);
}

/**
 * Get size of border at position.
 *
 * @param pos Position to get size for.
 * @param width Width of border at position.
 * @param height Height of border at position.
 */
void
PDecor::getBorderSize(BorderPosition pos, uint &width, uint &height)
{
    FocusedState state = getFocusedState(false); // convenience

    switch (pos) {
    case BORDER_TOP_LEFT:
    case BORDER_TOP_RIGHT:
    case BORDER_BOTTOM_LEFT:
    case BORDER_BOTTOM_RIGHT:
        width = _data->getBorderTexture(state, pos)->getWidth();
        height = _data->getBorderTexture(state, pos)->getHeight();
        break;
    case BORDER_TOP:
        if ((bdTopLeft(this) + bdTopRight(this)) < _gm.width) {
            width = _gm.width - bdTopLeft(this) - bdTopRight(this);
        } else {
            width = 1;
        }
        height = _data->getBorderTexture(state, pos)->getHeight();
        break;
    case BORDER_BOTTOM:
        if ((bdBottomLeft(this) + bdBottomRight(this)) < _gm.width) {
            width = _gm.width - bdBottomLeft(this) - bdBottomRight(this);
        } else {
            width = 1;
        }
        height = _data->getBorderTexture(state, pos)->getHeight();
        break;
    case BORDER_LEFT:
        width = _data->getBorderTexture(state, pos)->getWidth();
        if ((bdTopLeftHeight(this) + bdBottomLeftHeight(this)) < _gm.height) {
            height = _gm.height - bdTopLeftHeight(this) - bdBottomLeftHeight(this);
        } else {
            height = 1;
        }
        break;
    case BORDER_RIGHT:
        width = _data->getBorderTexture(state, pos)->getWidth();
        if ((bdTopRightHeight(this) + bdBottomRightHeight(this)) < _gm.height) {
            height = _gm.height - bdTopRightHeight(this) - bdBottomRightHeight(this);
        } else {
            height = 1;
        }
        break;
    default:
        LOG("this == " << this << " - invalid border position");
        width = 1;
        height = 1;
        break;
    };
}

/**
 * Calculate width of title, if width min is 0 use the full available
 * size minus borders. Else get width from content.
 */
uint
PDecor::calcTitleWidth(void)
{
    if (_data->getTitleWidthMin() != 0) {
        return std::max(static_cast<uint>(1), calcTitleWidthDynamic());
    }

    uint width = _gm.width;
    uint title_offsets = titleLeftOffset(this) + titleRightOffset(this);
    if (width > title_offsets) {
        width -= title_offsets;
    }
    return std::max(static_cast<uint>(1), width);
}

uint
PDecor::calcTitleWidthDynamic(void)
{
    uint width = 0;
    uint width_max = 0;
    // FIXME: what about selected tabs?
    PFont *font = getFont(getFocusedState(false));

    if (_data->isTitleWidthSymetric()) {
        // Symetric mode, get max tab width, multiply with number of tabs
        for (auto it : _titles ){
            width = font->getWidth(it->getVisible());
            if (width > width_max) {
                width_max = width;
            }
        }

        width = width_max + _data->getPad(PAD_LEFT) + _data->getPad(PAD_RIGHT);
        width *= _titles.size();

    } else {
        // Asymetric mode, get individual widths
        for (auto it : _titles) {
            width += font->getWidth(it->getVisible())
                + _data->getPad(PAD_LEFT) + _data->getPad(PAD_RIGHT);
        }
    }

    // Add width of separators and buttons
    width += (_titles.size() - 1)
        * _data->getTextureSeparator(getFocusedState(false))->getWidth();
    width += _titles_left + _titles_right;

    // Validate sizes, make sure it is not less than width min
    // pixels and more than width max percent.
    if (width < static_cast<uint>(_data->getTitleWidthMin())) {
        width = _data->getTitleWidthMin();
    }
    if (width > (_gm.width * _data->getTitleWidthMax() / 100)) {
        width = _gm.width * _data->getTitleWidthMax() / 100;
    }
    return width;
}

/**
 * Calculate tab width, wrapper to choose correct algorithm..
 */
void
PDecor::calcTabsWidth(void)
{
    if (! _titles.size()) {
        return;
    }

    if (_data->isTitleWidthSymetric()) {
        calcTabsWidthSymetric();
    } else {
        calcTabsWidthAsymetric();
    }
}

/**
 * Get available tabs width and average width of a single tab.
 *
 * @param width_avail Number of pixels available in the titlebar.
 * @param tab_width Width in pixels to use for one tab.
 * @param off Number of pixels left with tab_width pixels wide tabs.
 */
void
PDecor::calcTabsGetAvailAndTabWidth(uint &width_avail, uint &tab_width, int &off)
{
    // Calculate width
    width_avail = _title_wo.getWidth();

    // Remove spacing if enough space is available
    if (width_avail > (_titles_left + _titles_right)) {
        width_avail -= _titles_left + _titles_right;
    }
 
    // Remove separators if enough space is available
    uint sep_width = (_titles.size() - 1)
        * _data->getTextureSeparator(getFocusedState(false))->getWidth();
    if (width_avail > sep_width) {
        width_avail -= sep_width;
    }

    tab_width = width_avail / _titles.size();
    off = width_avail % _titles.size();
}

//! @brief Calculate tab width symetric, sets up _titles widths.
void
PDecor::calcTabsWidthSymetric(void)
{
    int off;
    uint width_avail, tab_width;
    calcTabsGetAvailAndTabWidth(width_avail, tab_width, off);

    // Assign width to elements
    for (auto it : _titles) {
        it->setWidth(tab_width + ((off-- > 0) ? 1 : 0));
    }
}

/**
 * Calculate tab width asymmetrically. This is done with the following
 * priority:
 *
 *   1. Give tabs their required width, if it fits this is complete.
 *   2. Tabs did not fit, calculate average width.
 *   3. Add width of tabs requiring less space to average.
 *   4. Re-assign width equally to tabs using more than average.
 */
void
PDecor::calcTabsWidthAsymetric(void)
{
    int off;
    uint width, width_avail, tab_width;
    calcTabsGetAvailAndTabWidth(width_avail, tab_width, off);

    // Convenience
    PFont *font = getFont(getFocusedState(false));

    // 1. give tabs their required width.
    uint width_total = 0;
    for (auto it : _titles) {
        // This should set the tab width to be only the size needed
        width = font->getWidth(it->getVisible().c_str())
            + _data->getPad(PAD_LEFT) + _data->getPad(PAD_RIGHT) + ((off-- > 0) ? 1 : 0);
        width_total += width;
        it->setWidth(width);
    }

    if (width_total > width_avail) {
        calcTabsWidthAsymetricShrink(width_avail, tab_width);
    } else if (width_total < static_cast<uint>(_data->getTitleWidthMin())) {
        calcTabsWidthAsymetricGrow(width_avail, tab_width);
    }
}

/**
 * This is called to shrink the tabs that are over the tab_width in
 * order to make room for all
 */
void
PDecor::calcTabsWidthAsymetricShrink(uint width_avail, uint tab_width)
{
    // 2. Tabs did not fit
    uint tabs_left = _titles.size();
    for (auto it : _titles) {
        if (it->getWidth() < tab_width) {
            // 3. Add width of tabs requiring less space to the average.
            tabs_left--;
            width_avail -= it->getWidth();
        }
    }

    // 4. Re-assign width equally to tabs using more than average
    tab_width = width_avail / tabs_left;
    uint off = width_avail % tabs_left;
    for (auto it : _titles) {
        if (it->getWidth() >= tab_width) {
            it->setWidth(tab_width + ((off-- > 0) ? 1 : 0));
        }
    }
}

/**
 * This is called when tabs in the decor are using less space than
 * width min, grow tabs that are smaller than the average.
 */
void
PDecor::calcTabsWidthAsymetricGrow(uint width_avail, uint tab_width)
{
    // FIXME: Implement growing of asymetric tabs.
}
