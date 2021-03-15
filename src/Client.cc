//
// Client.cc for pekwm
// Copyright (C) 2002-2020 Claes Nästén <pekdon@gmail.com>
//
// client.cc for aewm++
// Copyright (C) 2000 Frank Hale <frankhale@yahoo.com>
// http://sapphire.sourceforge.net/
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "config.h"

#include <cstdio>
#include <iostream>
#include <algorithm>

extern "C" {
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <assert.h>
}

#include "Charset.hh"
#include "Compat.hh" // setenv, unsetenv
#include "Debug.hh"
#include "PWinObj.hh"
#include "PDecor.hh" // PDecor::TitleItem
#include "Client.hh"
#include "ClientMgr.hh"
#include "Config.hh"
#include "KeyGrabber.hh"
#include "AutoProperties.hh"
#include "Frame.hh"
#include "Workspaces.hh"
#include "PImageIcon.hh"
#include "ImageHandler.hh"
#include "TextureHandler.hh"
#include "ManagerWindows.hh"
#include "X11Util.hh"
#include "X11.hh"

const long Client::_clientEventMask = \
    PropertyChangeMask|StructureNotifyMask|FocusChangeMask|KeyPressMask;
std::vector<Client*> Client::_clients;
std::vector<uint> Client::_clientids;

Client::Client(Window new_client, ClientInitConfig &initConfig, bool is_new)
    : PWinObj(true),
      _id(0),
      _size(0),
      _transient_for(nullptr),
      _strut(nullptr),
      _icon(nullptr),
      _pid(0), _is_remote(false), _class_hint(0),
      _window_type(WINDOW_TYPE_NORMAL),
      _alive(false), _marked(false),
      _send_focus_message(false), _send_close_message(false),
      _wm_hints_input(true), _cfg_request_lock(false),
      _extended_net_name(false),
      _demands_attention(false)
{
    // PWinObj attributes, required by validate etc.
    _window = new_client;
    _type = WO_CLIENT;

    // Construct the client
    X11::grabServer();
    if (! validate() || ! getAndUpdateWindowAttributes()) {
        X11::ungrabServer(true);
        return;
    }

    // Get unique Client id
    _id = findClientID();
    _title.setId(_id);
    _title.infoAdd(PDecor::TitleItem::INFO_ID);

    if (X11::hasExtensionShape()) {
        X11::shapeSelectInput(_window, ShapeNotifyMask);

        int isShaped;
        X11::shapeQuery(_window, &isShaped);
        _shape_bounding = isShaped;

        int num = 0;
        auto *rect = X11::shapeGetRects(_window, ShapeBounding, &num);
        if (rect) {
            if (num == 1) {
                unsigned w, h, bw;
                X11::getGeometry(_window, &w, &h, &bw);
                if (rect->x > 0 || rect->y > 0 || rect->width < w+bw
                                || rect->height < h+bw) {
                   _shape_input = true;
                }
            } else {
                _shape_input = true;
            }
            X11::free(rect);
        }
    }

    XAddToSaveSet(X11::getDpy(), _window);
    XSetWindowBorderWidth(X11::getDpy(), _window, 0);

    // Load the Class hint before loading the autoprops and
    // getting client title as we search for TitleRule in the autoprops
    _class_hint = new ClassHint();
    readClassRoleHints();

    getWMNormalHints();
    readName();

    // cyclic dependency, getting the name requires quiering autoprops
    _class_hint->title = _title.getReal();

    // Get Autoproperties before EWMH as we need the cfg_deny
    // property, however the _transient hint needs to be setup to
    // avoid auto-grouping to be to greedy.
    getTransientForHint();

    auto ap = readAutoprops(pekwm::isStartup() ? APPLY_ON_NEW : APPLY_ON_START);
    readHints();

    // We need to set the state before acquiring a frame,
    // so that Frame's state can match the state of the Client.
    setInitialState();

    initConfig.parent_is_new = findOrCreateFrame(ap);

    // Grab keybindings and mousebutton actions
    pekwm::keyGrabber()->grabKeys(_window);
    grabButtons();

    // Tell the world about our state
    updateEwmhStates();

    X11::ungrabServer(true);

    setClientInitConfig(initConfig, is_new, ap);

    _alive = true;

    findAndRaiseIfTransient();

    // Finished creating the client, so now adding it to the client list.
    woListAdd(this);
    _wo_map[_window] = this;
    _clients.push_back(this);

    TRACE(this << " client constructed for window " << FMT_HEX(_window));
}

//! @brief Client destructor
Client::~Client(void)
{
    while (! _transients.empty()) {
        _transients[0]->setTransientFor(nullptr);
    }
    setTransientFor(nullptr);

    _wo_map.erase(_window);
    woListRemove(this);
    _clients.erase(std::remove(_clients.begin(), _clients.end(), this),
                   _clients.end());
    returnClientID(_id);

    X11::grabServer();

    // removes gravity and moves it back to root if we are alive
    bool focus = false;
    if (_parent && (_parent->getType() == PWinObj::WO_FRAME)) {
        focus = _parent->isFocused();
        static_cast<PDecor*>(_parent)->removeChild(this);
    }

    // Focus the parent if we had focus before
    if (focus && _transient_for) {
        Frame *trans_frame = static_cast<Frame*>(_transient_for->getParent());
        if (trans_frame->getActiveChild() == _transient_for) {
            trans_frame->giveInputFocus();
        }
    }

    // Clean up if the client still is alive, it'll be dead all times
    // except when we exit pekwm
    if (_alive) {
        X11::ungrabButton(AnyButton, AnyModifier, _window);
        pekwm::keyGrabber()->ungrabKeys(_window);
        XRemoveFromSaveSet(X11::getDpy(), _window);
        PWinObj::mapWindow();
    }

    // free names and size hint
    if (_size) {
        X11::free(_size);
    }

    removeStrutHint();

    if (_class_hint) {
        delete _class_hint;
    }

    if (_icon) {
        pekwm::textureHandler()->returnTexture(_icon);
    }

    X11::ungrabServer(true);

    TRACE(this << " client for window " << FMT_HEX(_window) << " destructed");
}

/**
 * Read basic window attributes including geometry and update the
 * window attributes being listened to. Returns false if the client
 * disappears during the check.
 */
bool
Client::getAndUpdateWindowAttributes(void)
{
    XWindowAttributes attr;
    if (! XGetWindowAttributes(X11::getDpy(), _window, &attr)) {
        return false;
    }
    _gm.x = attr.x;
    _gm.y = attr.y;
    _gm.width = attr.width;
    _gm.height = attr.height;

    _cmap = attr.colormap;
    _size = XAllocSizeHints();

    XSetWindowAttributes sattr;
    sattr.event_mask =
        PropertyChangeMask|StructureNotifyMask|FocusChangeMask;
    sattr.do_not_propagate_mask =
        ButtonPressMask|ButtonReleaseMask|ButtonMotionMask;

    // We don't want these masks to be propagated down to the frame
    X11::changeWindowAttributes(_window,
                                CWEventMask|CWDontPropagate, sattr);

    return true;
}

/**
 * Find frame for client based on tagging, hints and
 * autoproperties. Create a new one if not found and add the client.
 *
 * \return true if a new find was created.
 */
bool
Client::findOrCreateFrame(AutoProperty *autoproperty)
{
    bool parent_is_new = false;

    if (! _parent) {
        findTaggedFrame();
    }
    if (! _parent) {
        findPreviousFrame();
    }

    // Apply Autoproperties again to override EWMH state. It's done twice as
    // we need the cfg_deny property when reading the EWMH state.
    if (autoproperty != 0) {
        applyAutoprops(autoproperty);
        if (! _parent) {
            findAutoGroupFrame(autoproperty);
        }
    }

    // if we don't have a frame already, create a new one
    if (! _parent) {
        parent_is_new = true;
        _parent = new Frame(this, autoproperty);
    }

    return parent_is_new;
}

/**
 * Find from client from for the currently tagged client.
 */
bool
Client::findTaggedFrame(void)
{
    if (! pekwm::isStartup()) {
        return false;
    }

    // Check for tagged frame
    Frame *frame = Frame::getTagFrame();
    if (frame && frame->isMapped()) {
        _parent = frame;
        frame->addChild(this);

        if (! Frame::getTagBehind()) {
            frame->activateChild(this);
        }
    }

    return _parent != 0;
}

/**
 * Find frame for client on PEKWM_FRAME_ID hint.
 */
bool
Client::findPreviousFrame(void)
{
    if (pekwm::isStartup()) {
        return false;
    }

    Cardinal id;
    if (X11::getCardinal(_window, PEKWM_FRAME_ID, id)) {
        _parent = Frame::findFrameFromID(id);
        if (_parent) {
            Frame *frame = static_cast<Frame*>(_parent);
            frame->addChildOrdered(this);
            if (getPekwmFrameActive()) {
                frame->activateChild(this);
            }
        }
    }

    return _parent != 0;
}

/**
 * Find frame for client based on autoproperties.
 */
bool
Client::findAutoGroupFrame(AutoProperty *autoproperty)
{
    if (autoproperty->group_size < 0) {
        return false;
    }

    auto frame = ClientMgr::findGroup(autoproperty);
    if (frame) {
        frame->addChild(this);

        if (! autoproperty->group_behind) {
            frame->activateChild(this);
        }
        if (autoproperty->group_raise) {
            frame->raise();
        }
    }

    return _parent != 0;
}

/**
 * Get the client state and set iconified and mapped flags.
 */
void
Client::setInitialState(void)
{
    // Set state either specified in hint
    ulong initial_state = getWMHints();
    if (getWmState() == IconicState) {
        _iconified = true;
    }

    if (_iconified || initial_state == IconicState) {
        _iconified = true;
        _mapped = true;
        unmapWindow();
    } else {
        setWmState(initial_state);
    }
}

/**
 * Ensure the Client is (un) mapped and give input focus if requested.
 */
void
Client::setClientInitConfig(ClientInitConfig &initConfig, bool is_new,
                            AutoProperty *prop)
{
    initConfig.map = (! _iconified && _parent->isMapped());
    initConfig.focus = false;

    if (is_new && _parent->isMapped()) {
        initConfig.focus = pekwm::config()->isFocusNew();
        if (! initConfig.focus && _transient_for) {
            initConfig.focus = _transient_for->isFocused()
                && pekwm::config()->isFocusNewChild();
        }

        // overwrite focus if set in the autoproperties
        if (prop && prop->isMask(AP_FOCUS_NEW)) {
            initConfig.focus = prop->focus_new;
        }
    }
}

/**
 * Re-lookup transient client if window is set but not client, raise over
 * transient for window if found/set.
 */
void
Client::findAndRaiseIfTransient(void)
{
    if (_transient_for_window != None && ! _transient_for) {
        getTransientForHint();
    }

    if (_transient_for) {
        // Ensure layer is at least as high as the parent
        if (_transient_for->getLayer() > getLayer()) {
            setLayer(_transient_for->getLayer());
            updateParentLayerAndRaiseIfActive();
        }
    }
}

// START - PWinObj interface.

//! @brief Maps the window.
void
Client::mapWindow(void)
{
    if (_mapped) {
        return;
    }

    if (_iconified) {
        _iconified = false;
        setWmState(NormalState);
        updateEwmhStates();
    }

    if(! _transient_for) {
        // Unmap our transient windows if we have any
        mapOrUnmapTransients(_window, false);
    }

    X11::selectInput(_window, NoEventMask);
    PWinObj::mapWindow();
    X11::selectInput(_window, _clientEventMask);
}


//! @brief Sets the client to WithdrawnState and unmaps it.
void
Client::unmapWindow(void)
{
    if (! _mapped) {
        return;
    }

    if (_iconified) {
        // Set the state of the window
        setWmState(IconicState);
        updateEwmhStates();
    }

    X11::selectInput(_window, NoEventMask);
    PWinObj::unmapWindow();
    X11::selectInput(_window, _clientEventMask);
}

//! @brief Iconifies the client and adds it to the iconmenu
void
Client::iconify(void)
{
    if (_iconified) {
        return;
    }

    _iconified = true;
    if (! _transient_for) {
        mapOrUnmapTransients(_window, true);
    }

    unmapWindow();
}

//! @brief Toggle client sticky state
void
Client::stick(void)
{
    PWinObj::stick();

    updateEwmhStates();
}

//! @brief Update the position variables.
void
Client::move(int x, int y)
{
    bool request = ((_gm.x != x) || (_gm.y != y));

    _gm.x = x;
    _gm.y = y;

    if (request) {
        configureRequestSend();
    }
}

//! @brief Resizes the client window to specified size.
void
Client::resize(uint width, uint height)
{
    bool request = ((_gm.width != width) || (_gm.height != height));

    PWinObj::resize(width, height);

    if (request) {
        configureRequestSend();
    }
}

//! @brief Move and resizes the client window to specified size.
void
Client::moveResize(int x, int y, uint width, uint height)
{
    bool request = ((_gm.x != x) || (_gm.y != y) || (_gm.width != width) || (_gm.height != height));

    _gm.x = x;
    _gm.y = y;

    PWinObj::resize(width, height);

    if (request) {
        configureRequestSend();
    }
}

//! @brief Sets the workspace and updates the _NET_WM_DESKTOP hint.
void
Client::setWorkspace(uint workspace)
{
    if (workspace != NET_WM_STICKY_WINDOW) {
        if (workspace >= Workspaces::size()) {
            workspace = Workspaces::size() - 1;
        }
        _workspace = workspace;

        if (_sticky) {
            X11::setCardinal(_window, NET_WM_DESKTOP, NET_WM_STICKY_WINDOW);
        } else {
            X11::setCardinal(_window, NET_WM_DESKTOP, _workspace);
        }
    }
}

//! @brief Gives the Client input focus.
void
Client::giveInputFocus(void)
{
    Frame *frame;
    if (demandsAttention() && (frame = static_cast<Frame *>(_parent))) {
        setDemandsAttention(false);
        frame->decrAttention();
    }

    if (_wm_hints_input) {
        PWinObj::giveInputFocus();
    }

    sendTakeFocusMessage();
}

//! @brief Reparents and sets _parent member, filtering unmap events
void
Client::reparent(PWinObj *parent, int x, int y)
{
    X11::selectInput(_window, NoEventMask);
    PWinObj::reparent(parent, x, y);
    _gm.x = parent->getX() + x;
    _gm.y = parent->getY() + y;
    X11::selectInput(_window,
                         PropertyChangeMask|StructureNotifyMask|FocusChangeMask);
}

ActionEvent*
Client::handleMapRequest(XMapRequestEvent *ev)
{
       if (_parent && dynamic_cast<PDecor *>(_parent)) {
           dynamic_cast<PDecor*>(_parent)->deiconify();
       }
       return 0;
}

ActionEvent*
Client::handleUnmapEvent(XUnmapEvent *ev)
{
    if ((ev->window != ev->event) && (ev->send_event != true)) {
        return 0;
    }

    // The window might not exist any more, so just ignore the errors.
    setXErrorsIgnore(true);

    // ICCCM 4.1.4 advices the window manager to trigger the transition to
    // Withdrawn state on real and synthetic UnmapNotify events.
    setWmState(WithdrawnState);

    // Extended Window Manager Hints 1.3 specifies that a window manager
    // should remove the _NET_WM_STATE property when a window is withdrawn.
    X11::unsetProperty(_window, STATE);

    // Extended Window Manager Hints 1.3 specifies that a window manager
    // should remove the _NET_WM_DESKTOP property when a window is withdrawn.
    // (to allow legacy applications to reuse a withdrawn window)
    X11::unsetProperty(_window, NET_WM_DESKTOP);

    // FIXME: Listen mask should change as this doesn't work?
    _alive = false;
    delete this;

    setXErrorsIgnore(false);
    return 0;
}

// END - PWinObj interface.

#ifdef HAVE_SHAPE
void
Client::handleShapeEvent(XShapeEvent *ev)
{
    if (ev->kind == ShapeBounding) {
        _shape_bounding = ev->shaped;
    } else if (ev->kind == ShapeInput) {
        _shape_input = ev->shaped;
    } else {
        return;
    }

    if (_parent) {
        static_cast<Frame *>(_parent)->handleShapeEvent(ev);
    }
}
#endif // HAVE_SHAPE

// START - Observer interface

void
Client::notify(Observable *observable, Observation *observation)
{
    auto layer_observation = dynamic_cast<LayerObservation*>(observation);
    if (layer_observation && layer_observation->layer > getLayer()) {
        setLayer(layer_observation->layer);
        updateParentLayerAndRaiseIfActive();
    }
}

// END - Observer interface

//! @brief Finds the Client which holds the Window w.
//! @param win Window to search for.
//! @return Pointer to the client if found, else 0
Client*
Client::findClient(Window win)
{
    // Validate input window.
    if ((win == None) || (win == X11::getRoot())) {
        return 0;
    }

    for (auto it : _clients) {
        if (win == it->getWindow()
            || (it->getParent() && (*(it->getParent()) == win))) {
            return it;
        }
    }

    return 0;
}

//! @brief Finds the Client of Window win.
//! @param win Window to search for.
Client*
Client::findClientFromWindow(Window win)
{
    // Validate input window.
    if (! win || win == X11::getRoot()) {
        return 0;
    }

    for (auto it : _clients) {
        if (win == it->getWindow()) {
            return it;
        }
    }

    return 0;
}

//! @brief Finds Client with equal ClassHint.
//! @param class_hint ClassHint to search for.
//! @return Client if found, else 0.
Client*
Client::findClientFromHint(const ClassHint *class_hint)
{
    for (auto it : _clients) {
        if (*class_hint == *it->getClassHint()) {
            return it;
        }
    }

    return 0;
}

//! @brief Finds Client with id.
//! @param id ID to search for.
//! @return Client if found, else 0.
Client*
Client::findClientFromID(uint id)
{
    for (auto it : _clients) {
        if (it->getClientID() == id) {
            return it;
        }
    }

    return 0;
}

/**
 * Insert all clients with the transient for set to win.
 */
void
Client::findFamilyFromWindow(std::vector<Client*> &client_list, Window win)
{
    for (auto it : _clients) {
        if (it->getTransientForClientWindow() == win) {
            client_list.push_back(it);
        }
    }
}


/**
 * (Un)Maps all windows having transient_for set to win
 */
void
Client::mapOrUnmapTransients(Window win, bool hide)
{
    std::vector<Client*> client_list;
    findFamilyFromWindow(client_list, win);

    for (auto it : client_list) {
        if (static_cast<Frame*>(it->getParent())->getActiveChild() == it) {
            if (hide) {
                it->getParent()->iconify();
            } else {
                it->getParent()->mapWindow();
            }
        }
    }
}

/**
 * Checks if the window has any Destroy or Unmap notifys.
 */
bool
Client::validate(void)
{
    X11::sync(False);

    XEvent ev;
    if (XCheckTypedWindowEvent(X11::getDpy(), _window, DestroyNotify, &ev)
        || XCheckTypedWindowEvent(X11::getDpy(), _window, UnmapNotify, &ev)) {
        XPutBackEvent(X11::getDpy(), &ev);
        return false;
    }

    return true;
}

//! @brief Checks if the window has attribute IsViewable set
bool
Client::isViewable(void)
{
    XWindowAttributes attr;
    XGetWindowAttributes(X11::getDpy(), _window, &attr);

    return (attr.map_state == IsViewable);
}

//! @brief Grabs all the mouse button actions on the client.
void
Client::grabButtons(void)
{
    // Make sure we don't have any buttons grabbed.
    X11::ungrabButton(AnyButton, AnyModifier, _window);

    auto actions =
        pekwm::config()->getMouseActionList(MOUSE_ACTION_LIST_CHILD_FRAME);
    for (auto it : *actions) {
        if ((it.type == MOUSE_EVENT_PRESS)
            || (it.type == MOUSE_EVENT_RELEASE)) {
            // No need to grab mod less events, replied with the frame
            if ((it.mod == 0) || (it.mod == MOD_ANY)) {
                continue;
            }

            uint mask = ButtonPressMask|ButtonReleaseMask;
            X11Util::grabButton(it.sym, it.mod, mask, _window);
        } else if (it.type == MOUSE_EVENT_MOTION) {
            // FIXME: Add support for MOD_ANY
            uint mask = ButtonPressMask|ButtonReleaseMask|ButtonMotionMask;
            X11Util::grabButton(it.sym, it.mod, mask, _window);
        }
    }
}

//! @brief Sets CfgDeny state on Client
void
Client::setStateCfgDeny(StateAction sa, uint deny)
{
    if (! ActionUtil::needToggle(sa, _state.cfg_deny&deny)) {
        return;
    }

    if (_state.cfg_deny&deny) {
        _state.cfg_deny &= ~deny;
    } else {
        _state.cfg_deny |= deny;
    }
}

/**
 * Read "all" hints required during creation of Client.
 */
void
Client::readHints(void)
{
    readMwmHints();
    readEwmhHints();
    readPekwmHints();
    readIcon();
    readClientPid();
    readClientRemote();
    getWMProtocols();
}

/**
 * Read the WM_CLASS hint and set information on the _class_hint used
 * in matching auto properties.
 */
void
Client::readClassRoleHints(void)
{
    // class hint
    XClassHint class_hint;
    if (XGetClassHint(X11::getDpy(), _window, &class_hint)) {
        _class_hint->h_name = Charset::to_wide_str(class_hint.res_name);
        _class_hint->h_class = Charset::to_wide_str(class_hint.res_class);
        X11::free(class_hint.res_name);
        X11::free(class_hint.res_class);
    }

    // wm window role
    std::string role;
    X11::getString(_window, WM_WINDOW_ROLE, role);

    _class_hint->h_role = Charset::to_wide_str(role);
}

//! @brief Loads the Clients state from EWMH atoms.
void
Client::readEwmhHints(void)
{
    // which workspace do we belong to?
    Cardinal workspace = -1;
    X11::getCardinal(_window, NET_WM_DESKTOP, workspace);
    if (workspace < 0) {
        _workspace = Workspaces::getActive();
        X11::setCardinal(_window, NET_WM_DESKTOP, _workspace);
    } else {
        _workspace = workspace;
    }

    updateWinType(true);

    // Apply autoproperties for window type
    auto auto_property =
        pekwm::autoProperties()->findWindowTypeProperty(_window_type);
    if (auto_property) {
        applyAutoprops(auto_property);
    }

    // The _NET_WM_STATE overrides the _NET_WM_TYPE
    NetWMStates win_states;
    if (X11Util::readEwmhStates(_window, win_states)) {
        if (win_states.hidden
            && ! isCfgDeny(CFG_DENY_STATE_HIDDEN)) {
            _iconified = true;
        }
        if (win_states.shaded) _state.shaded = true;
        if (win_states.max_vert
            && ! isCfgDeny(CFG_DENY_STATE_MAXIMIZED_VERT)) {
            _state.maximized_vert = true;
        }
        if (win_states.max_horz
            && ! isCfgDeny(CFG_DENY_STATE_MAXIMIZED_HORZ)) {
            _state.maximized_horz = true;
        }
        if (win_states.skip_taskbar) _state.skip |= SKIP_TASKBAR;
        if (win_states.skip_pager) _state.skip |= SKIP_PAGER;
        if (win_states.sticky) _sticky = true;
        if (win_states.above
            && ! isCfgDeny(CFG_DENY_STATE_ABOVE)) {
            setLayer(LAYER_ABOVE_DOCK);
        }
        if (win_states.below
            && ! isCfgDeny(CFG_DENY_STATE_BELOW)) {
            setLayer(LAYER_BELOW);
        }
        if (win_states.fullscreen
            && ! isCfgDeny(CFG_DENY_STATE_FULLSCREEN)) {
            _state.fullscreen = true;
        }
        _demands_attention = win_states.demands_attention;
    }

    // check if we have a strut
    getStrutHint();
}

/**
 * Loads the Clients state from MWM atoms.
 */
void
Client::readMwmHints(void)
{
    MwmHints mwm_hints;
    if (! X11Util::readMwmHints(_window, mwm_hints)) {
        return;
    }

    if (mwm_hints.flags&MWM_HINTS_FUNCTIONS) {
        bool state = ! (mwm_hints.functions&MWM_FUNC_ALL);
#define IS_MWM_FUNC(flag) (mwm_hints.functions&(flag)) ? state : !state;
        _actions.resize = IS_MWM_FUNC(MWM_FUNC_RESIZE);
        _actions.move = IS_MWM_FUNC(MWM_FUNC_MOVE);
        _actions.iconify = IS_MWM_FUNC(MWM_FUNC_ICONIFY);
        _actions.close = IS_MWM_FUNC(MWM_FUNC_CLOSE);
        _actions.maximize_vert = IS_MWM_FUNC(MWM_FUNC_MAXIMIZE);
        _actions.maximize_horz = IS_MWM_FUNC(MWM_FUNC_MAXIMIZE);
#undef IS_MWM_FUNC
    }

    // Check decoration flags
    if (mwm_hints.flags & MWM_HINTS_DECORATIONS) {
        if (mwm_hints.decorations & MWM_DECOR_ALL) {
            setTitlebar(true);
            setBorder(true);
        } else {
            if (! (mwm_hints.decorations & MWM_DECOR_TITLE)) {
                setTitlebar(false);
            }
            if (! (mwm_hints.decorations & MWM_DECOR_BORDER)) {
                setBorder(false);
            }
            // Do not handle HANDLE, MENU, ICONFIY or MAXIMIZE. Maybe
            // one should set the allowed actions for the client based
            // on this but that might be annoying so ignoring these.
        }
    }
}

//! @brief Reads non-standard pekwm hints
void
Client::readPekwmHints(void)
{
    Cardinal value;
    std::string str;

    // Get decor state
    if (X11::getCardinal(_window, PEKWM_FRAME_DECOR, value)) {
        _state.decor = value;
    }
    // Get skip state
    if (X11::getCardinal(_window, PEKWM_FRAME_SKIP, value)) {
        _state.skip = value;
    }

    // Get custom title
    if (X11::getString(_window, PEKWM_TITLE, str)) {
        _title.setUser(Charset::to_wide_str(str));
    }

    _state.initial_frame_order = getPekwmFrameOrder();
}

/**
 * Read _NET_WM_ICON from client window.
 */
void
Client::readIcon(void)
{
    auto image = PImageIcon::newFromWindow(_window);
    if (image) {
        if (_icon) {
            _icon->setImage(image);
        } else {
            _icon = new PTextureImage(image);
            pekwm::textureHandler()->referenceTexture(_icon);
        }
    } else {
        delete image;
        if (_icon) {
            pekwm::textureHandler()->returnTexture(_icon);
            _icon = nullptr;
        }
    }
}

/**
 * Match current client against list of autoproperties.
 *
 * @param type Match type, defaults to APPLY_ON_ALWAYS.
 * @return AutoProperty if any is found, else nullptr.
 */
AutoProperty*
Client::readAutoprops(ApplyOn type)
{
    auto aps = pekwm::autoProperties();
    auto property =
        aps->findAutoProperty(_class_hint, Workspaces::getActive(), type);
    if (property) {
        DBG("auto property found for client " << _class_hint);

        // Make sure transient state matches
        if (isTransient()
            ? property->isApplyOn(APPLY_ON_TRANSIENT|APPLY_ON_TRANSIENT_ONLY)
            : ! property->isApplyOn(APPLY_ON_TRANSIENT_ONLY)) {
            applyAutoprops(property);
        } else {
            property = nullptr;
        }
    } else {
        DBG("no auto property found for client " << _class_hint);
    }
    return property;
}

//! @brief Applies AutoPropery to this Client.
void
Client::applyAutoprops(AutoProperty *ap)
{
    // Set the correct group of the window
    _class_hint->group = ap->group_name;

    // We only apply grouping if it's a new client or if we are restarting
    // and have APPLY_ON_START set
    if (ap->isMask(AP_STICKY))
        _sticky = ap->sticky;
    if (ap->isMask(AP_SHADED))
        _state.shaded = ap->shaded;
    if (ap->isMask(AP_MAXIMIZED_VERTICAL))
        _state.maximized_vert = ap->maximized_vertical;
    if (ap->isMask(AP_MAXIMIZED_HORIZONTAL))
        _state.maximized_horz = ap->maximized_horizontal;
    if (ap->isMask(AP_FULLSCREEN))
        _state.fullscreen = ap->fullscreen;
    if (ap->isMask(AP_ICONIFIED))
        _iconified = ap->iconified;
    if (ap->isMask(AP_TITLEBAR))
        setTitlebar(ap->titlebar);
    if (ap->isMask(AP_BORDER))
        setBorder(ap->border);
    if (ap->isMask(AP_LAYER) && (ap->layer <= LAYER_MENU)) {
        setLayer(ap->layer);
    }
    if (ap->isMask(AP_SKIP))
        _state.skip = ap->skip;
    if (ap->isMask(AP_FOCUSABLE))
        _focusable = ap->focusable;
    if (ap->isMask(AP_WORKSPACE)) {
        _workspace = ap->workspace;
    }
    if (ap->isMask(AP_CFG_DENY)) {
        _state.cfg_deny = ap->cfg_deny;
    }
    if (ap->isMask(AP_ALLOWED_ACTIONS)) {
        applyActionAccessMask(ap->allowed_actions, true);
    }
    if (ap->isMask(AP_DISALLOWED_ACTIONS)) {
        applyActionAccessMask(ap->disallowed_actions, false);
    }
    if (ap->isMask(AP_OPACITY)) {
        setOpacity(ap->focus_opacity, ap->unfocus_opacity);
    }
    if (ap->isMask(AP_ICON)) {
        TRACE("set _NET_WM_ICON on " << _window);
        ap->icon->setOnWindow(_window);
    }
}

void
Client::applyActionAccessMask(uint mask, bool value)
{
    if (mask & ACTION_ACCESS_MOVE) {
        _actions.move = value;
    }
    if (mask & ACTION_ACCESS_RESIZE) {
        _actions.resize = value;
    }
    if (mask & ACTION_ACCESS_ICONIFY) {
        _actions.iconify = value;
    }
    if (mask & ACTION_ACCESS_SHADE) {
        _actions.shade = value;
    }
    if (mask & ACTION_ACCESS_STICK) {
        _actions.stick = value;
    }
    if (mask & ACTION_ACCESS_MAXIMIZE_HORZ) {
        _actions.maximize_horz = value;
    }
    if (mask & ACTION_ACCESS_MAXIMIZE_VERT) {
        _actions.maximize_vert = value;
    }
    if (mask & ACTION_ACCESS_FULLSCREEN) {
        _actions.fullscreen = value;
    }
    if (mask & ACTION_ACCESS_CHANGE_DESKTOP) {
        _actions.change_ws = value;
    }
    if (mask & ACTION_ACCESS_CLOSE) {
        _actions.close = value;
    }
}

/**
 * Read _NET_WM_PID.
 */
void
Client::readClientPid(void)
{
    X11::getCardinal(_window, NET_WM_PID, _pid);
}

/**
 * Read WM_CLIENT_MACHINE and check against local hostname and set
 * _is_remote if it does not match.
 */
void
Client::readClientRemote(void)
{
    std::string client_machine;
    if (X11::getTextProperty(_window, XA_WM_CLIENT_MACHINE, client_machine)) {
        _is_remote = Util::getHostname() != client_machine;
    }
}

//! @brief Finds free Client ID.
//! @return First free Client ID.
uint
Client::findClientID(void)
{
    uint id = 0;    

    if (_clientids.size()) {
        // Check for used Frame IDs
        id = _clientids.back();
        _clientids.pop_back();
    } else {
        // No free, get next number (Client is not in list when this is called.)
        id = _clients.size() + 1;
    }

    return id;
}

//! @brief Returns Client ID to used client id list.
//! @param id ID to return.
void
Client::returnClientID(uint id)
{
    auto it(_clientids.begin());
    for (; it != _clientids.end() && id < *it; ++it)
        ;
    _clientids.insert(it, id);
}

/**
 * Tries to get the NET_WM name, else fall back to WM_NAME
 */
void
Client::readName(void)
{
    // Read title, bail out if it fails.
    std::string title;
    std::wstring wtitle;
    if (X11::getUtf8String(_window, NET_WM_NAME, title)) {
        wtitle = Charset::from_utf8_str(title);
    } else if (X11::getTextProperty(_window, XA_WM_NAME, title)) {
        wtitle = Charset::to_wide_str(title);
    } else {
        return;
    }

    // Mirror it on the visible
    _title.setCustom(L"");
    _title.setCount(titleFindID(wtitle));
    _title.setReal(wtitle);

    // Apply title rules and find unique name, doesn't apply on
    // user-set titles
    if (titleApplyRule(wtitle)) {
        _title.setCustom(wtitle);
        X11::setUtf8String(_window, NET_WM_VISIBLE_NAME, Charset::to_utf8_str(wtitle));
    } else {
        X11::unsetProperty(_window, NET_WM_VISIBLE_NAME);
    }
}

//! @brief Searches for an TitleRule and if found, applies it
//! @param title Title to apply rule on.
//! @return true if rule was applied, else false.
bool
Client::titleApplyRule(std::wstring &title)
{
    _class_hint->title = title;
    auto data = pekwm::autoProperties()->findTitleProperty(_class_hint);
    if (data) {
        return data->getTitleRule().ed_s(title);
    } else {
        return false;
    }
}

//! @brief Searches for a unique ID within Clients having the same title
//! @param title Title of client to find ID for.
//! @return Number of clients with that id.
uint
Client::titleFindID(std::wstring &title)
{
    // Do not search for unique IDs if it is not enabled.
    if (! pekwm::config()->getClientUniqueName()) {
        return 0;
    }

    uint id_found = 0;
    std::vector<uint> ids_used;

    for (auto it : _clients) {
        if (it != this) {
            if (it->getTitle()->getReal() == title) {
                ids_used.push_back(it->getTitle()->getCount());
            }
        }
    }

    // more than one client having this name
    if (ids_used.size() > 0) {
        std::sort(ids_used.begin(), ids_used.end());

        auto ui_it(ids_used.begin());
        for (uint i = 0; ui_it != ids_used.end(); ++i, ++ui_it) {
            if (i < *ui_it) {
                id_found = i;
                break;
            }
        }

        if (ui_it == ids_used.end()) {
            id_found = ids_used.size();
        }
    }

    return id_found;
}

//! @brief Sets the WM_STATE of the client to state
//! @param state State to set.
void
Client::setWmState(ulong state)
{
    ulong data[2];

    data[0] = state;
    data[1] = None; // No Icon

    X11::changeProperty(_window,
                        X11::getAtom(WM_STATE),
                        X11::getAtom(WM_STATE),
                        32, PropModeReplace, (uchar*) data, 2);
}

// If we can't find a wm_state we're going to have to assume
// Withdrawn. This is not exactly optimal, since we can't really
// distinguish between the case where no WM has run yet and when the
// state was explicitly removed (Clients are allowed to either set the
// atom to Withdrawn or just remove it... yuck.)
long
Client::getWmState(void)
{
    Atom real_type;
    int real_format;
    long *data, state = WithdrawnState;
    ulong items_read, items_left;
    uchar *udata;

    int status =
        XGetWindowProperty(X11::getDpy(), _window, X11::getAtom(WM_STATE),
                           0L, 2L, False, X11::getAtom(WM_STATE),
                           &real_type, &real_format, &items_read, &items_left,
                           &udata);
    if ((status  == Success) && items_read) {
        data = reinterpret_cast<long*>(udata);
        state = *data;
        X11::free(udata);
    }

    return state;
}

//! @brief Send XConfigureEvent, letting the client know about changes
void
Client::configureRequestSend(void)
{
    if (_cfg_request_lock) {
      return;
    }

    XConfigureEvent e;

    e.type = ConfigureNotify;
    e.event = _window;
    e.window = _window;
    e.x = _gm.x;
    e.y = _gm.y;
    e.width = _gm.width;
    e.height = _gm.height;
    e.border_width = 0;
    e.above = None;
    e.override_redirect = False;

    XSendEvent(X11::getDpy(), _window, false, StructureNotifyMask, (XEvent *) &e);
}

//! @brief Send a TAKE_FOCUS client message to the client (if requested by it).
void Client::sendTakeFocusMessage(void)
{
    if (_send_focus_message) {
        {
            XEvent ev;
            X11::changeProperty(X11::getRoot(),
                                XA_PRIMARY, XA_STRING, 8, PropModeAppend, 0, 0);
            XWindowEvent(X11::getDpy(), X11::getRoot(),
                         PropertyChangeMask, &ev);
            X11::setLastEventTime(ev.xproperty.time);
        }
        X11::sendEvent(_window, _window,
                       X11::getAtom(WM_PROTOCOLS), NoEventMask,
                       X11::getAtom(WM_TAKE_FOCUS),
                       X11::getLastEventTime());
    }
}

/**
 * Toggles the clients always on top state
 */
void
Client::alwaysOnTop(bool top)
{
    setLayer(top ? LAYER_ONTOP : LAYER_NORMAL);
    updateEwmhStates();
}

/**
 * Toggles the clients always below state.
 */
void
Client::alwaysBelow(bool below)
{
    setLayer(below ? LAYER_BELOW : LAYER_NORMAL);
    updateEwmhStates();
}

//! @brief Sets the skip state, and updates the _PEKWM_FRAME_SKIP atom
void
Client::setSkip(uint skip)
{
    _state.skip = skip;
    X11::setCardinal(_window, PEKWM_FRAME_SKIP, _state.skip);
}

std::string
Client::getAPDecorName(void)
{
    auto props = pekwm::autoProperties();
    auto ap = props->findAutoProperty(getClassHint());
    if (ap && ap->isMask(AP_DECOR)) {
        return ap->frame_decor;
    }

    ap = props->findWindowTypeProperty(getWinType());
    if (ap && ap->isMask(AP_DECOR)) {
        return ap->frame_decor;
    }

    auto dp = props->findDecorProperty(getClassHint());
    if (dp) {
        return dp->getName();
    }

    return "";
}

//! @brief Sends an WM_DELETE message to the client, else kills it.
void
Client::close(void)
{
    // Check for DisallowedActions="Close".
    if (! allowClose()) {
        return;
    }

    if (_send_close_message) {
        X11::sendEvent(_window, _window, X11::getAtom(WM_PROTOCOLS),
                       NoEventMask,
                       X11::getAtom(WM_DELETE_WINDOW), CurrentTime);
    } else {
        kill();
    }
}

//! @brief Kills the client using XKillClient
void
Client::kill(void)
{
    XKillClient(X11::getDpy(), _window);
}

/**
 * Get the closest size confirming to the PAspect and PResizeInc.
 *
 * @param r_w Pointer to put the new width in
 * @param r_h Pointer to put the new height in
 * @param w Width to calculate from
 * @param h Height to calculate from
 * @return true if width/height have changed
 */
bool
Client::getAspectSize(uint *r_w, uint *r_h, uint w, uint h)
{
    auto size = getActiveSizeHints();

    // see ICCCM 4.1.2.3 for PAspect and {min,max}_aspect
    bool incr;
    if (size.flags & PAspect && pekwm::config()->isHonourAspectRatio()) {
        // shorthand
        const uint amin_x = size.min_aspect.x;
        const uint amin_y = size.min_aspect.y;
        const uint amax_x = size.max_aspect.x;
        const uint amax_y = size.max_aspect.y;

        uint base_w = 0, base_h = 0;

        // If PBaseSize is specified, base_{width,height} should be
        // subtracted before checking the aspect ratio
        // (c.f. ICCCM). The additional checks avoid underflows in w
        // and h. Keep in mind that size.base_{width,height} are
        // guaranteed to be non-negative by getWMNormalHints().
        if (size.flags & PBaseSize) {
            if (static_cast<uint>(size.base_width) < w) {
                base_w = size.base_width;
                w -= base_w;
            }
            if (static_cast<uint>(size.base_height) < h) {
                base_h = size.base_height;
                h -= base_h;
            }
        }

        double tmp;

        // Ensure: min_aspect.x/min_aspect.y <= w/h <= max_aspect.x/max_aspect.y
        if (w * amin_y < h * amin_x) {
            tmp = ((double)(w * amin_x + h * amin_y)) /
                  ((double)(amin_x * amin_x + amin_y * amin_y));

            w = static_cast<uint>(amin_x * tmp) + base_w;
            h = static_cast<uint>(amin_y * tmp) + base_h;

        } else if (w * amax_y > amax_x * h) {
            tmp = ((double)(w * amax_x + h * amax_y)) /
                  ((double)(amax_x * amax_x + amax_y * amax_y));

            w = static_cast<uint>(amax_x * tmp) + base_w;
            h = static_cast<uint>(amax_y * tmp) + base_h;
        }

        incr = false;
    } else {
        incr = true;
    }
    return getIncSize(size, r_w, r_h, w, h, incr);
}

/**
 * Get the size closest to the ResizeInc incrementer
 *
 * @param size Size hints to use for calculation.
 * @param r_w Pointer to put the new width in
 * @param r_h Pointer to put the new height in
 * @param w Width to calculate from
 * @param h Height to calculate from
 * @param incr Increase w, h to match increments if true, else decrease.
 */
bool
Client::getIncSize(const XSizeHints& size,
                   uint *r_w, uint *r_h, uint w, uint h, bool incr)
{
    uint basex, basey;

    if (size.flags&PResizeInc) {
        basex = (size.flags&PBaseSize)
                ? size.base_width
                : (size.flags&PMinSize) ? size.min_width : 0;

        basey = (size.flags&PBaseSize)
                ? size.base_height
                : (size.flags&PMinSize) ? size.min_height : 0;

        if (w < basex || h < basey) {
            basex=basey=0;
        }

        uint dw = (w - basex) % size.width_inc;
        uint dh = (h - basey) % size.height_inc;

        *r_w = w - dw + ((incr && dw)?size.width_inc:0);
        *r_h = h - dh + ((incr && dh)?size.height_inc:0);
        return true;
    }

    *r_w = w;
    *r_h = h;
    return false;
}

// This happens when a window is iconified and destroys itself. An
// Unmap event wouldn't happen in that case because the window is
// already unmapped.
void
Client::handleDestroyEvent(XDestroyWindowEvent *e)
{
    _alive = false;
    delete this;
}

void
Client::handleColormapChange(XColormapEvent *e)
{
    if (e->c_new) {
        _cmap = e->colormap;
        XInstallColormap(X11::getDpy(), _cmap);
    }
}

//! @brief Tells the world about our states, such as shaded etc.
void
Client::updateEwmhStates(void)
{
    std::vector<Atom> states;

    if (false) // we don't yet support modal state
        states.push_back(X11::getAtom(STATE_MODAL));
    if (_sticky)
        states.push_back(X11::getAtom(STATE_STICKY));
    if (_state.maximized_vert)
        states.push_back(X11::getAtom(STATE_MAXIMIZED_VERT));
    if (_state.maximized_horz)
        states.push_back(X11::getAtom(STATE_MAXIMIZED_HORZ));
    if (_state.shaded)
        states.push_back(X11::getAtom(STATE_SHADED));
    if (isSkip(SKIP_TASKBAR))
        states.push_back(X11::getAtom(STATE_SKIP_TASKBAR));
    if (isSkip(SKIP_PAGER))
        states.push_back(X11::getAtom(STATE_SKIP_PAGER));
    if (_iconified)
        states.push_back(X11::getAtom(STATE_HIDDEN));
    if (_state.fullscreen)
        states.push_back(X11::getAtom(STATE_FULLSCREEN));
    if (getLayer() == LAYER_ABOVE_DOCK) {
        states.push_back(X11::getAtom(STATE_ABOVE));
    }
    if (getLayer() == LAYER_BELOW) {
        states.push_back(X11::getAtom(STATE_BELOW));
    }

    Atom *atoms = new Atom[(states.size() > 0) ? states.size() : 1];
    if (states.size() > 0) {
        copy(states.begin(), states.end(), atoms);
    }
    X11::setAtoms(_window, STATE, atoms, states.size());
    delete [] atoms;
}

void
Client::updateWinType(bool set)
{
    // try to figure out what kind of window we are and alter it accordingly
    int items;
    Atom *atoms = 0;

    _window_type = WINDOW_TYPE;
    atoms = (Atom*) X11::getEwmhPropData(_window, WINDOW_TYPE, XA_ATOM, items);
    if (atoms) {
        for (int i = 0; _window_type == WINDOW_TYPE && i < items; ++i) {
            if (atoms[i] == X11::getAtom(WINDOW_TYPE_DESKTOP)) {
                _window_type = WINDOW_TYPE_DESKTOP;
            } else if (atoms[i] == X11::getAtom(WINDOW_TYPE_DOCK)) {
                _window_type = WINDOW_TYPE_DOCK;
            } else if (atoms[i] == X11::getAtom(WINDOW_TYPE_TOOLBAR)) {
                _window_type = WINDOW_TYPE_TOOLBAR;
            } else if (atoms[i] == X11::getAtom(WINDOW_TYPE_MENU)) {
                _window_type = WINDOW_TYPE_MENU;
            } else if (atoms[i] == X11::getAtom(WINDOW_TYPE_UTILITY)) {
                _window_type = WINDOW_TYPE_UTILITY;
            } else if (atoms[i] == X11::getAtom(WINDOW_TYPE_DIALOG)) {
                _window_type = WINDOW_TYPE_DIALOG;
            } else if (atoms[i] == X11::getAtom(WINDOW_TYPE_SPLASH)) {
                _window_type = WINDOW_TYPE_SPLASH;
            } else if (atoms[i] == X11::getAtom(WINDOW_TYPE_DROPDOWN_MENU)) {
                _window_type = WINDOW_TYPE_DROPDOWN_MENU;
            } else if (atoms[i] == X11::getAtom(WINDOW_TYPE_POPUP_MENU)) {
                _window_type = WINDOW_TYPE_POPUP_MENU;
            } else if (atoms[i] == X11::getAtom(WINDOW_TYPE_TOOLTIP)) {
                _window_type = WINDOW_TYPE_TOOLTIP;
            } else if (atoms[i] == X11::getAtom(WINDOW_TYPE_NOTIFICATION)) {
                _window_type = WINDOW_TYPE_NOTIFICATION;
            } else if (atoms[i] == X11::getAtom(WINDOW_TYPE_COMBO)) {
                _window_type = WINDOW_TYPE_COMBO;
            } else if (atoms[i] == X11::getAtom(WINDOW_TYPE_DND)) {
                _window_type = WINDOW_TYPE_DND;
            } else if (atoms[i] == X11::getAtom(WINDOW_TYPE_NORMAL)) {
                _window_type = WINDOW_TYPE_NORMAL;
            }
        }
        X11::free(atoms);
    }

    // Set window type to WINDOW_TYPE_NORMAL if it did not match
    if (_window_type == WINDOW_TYPE) {
        _window_type = WINDOW_TYPE_NORMAL;
        if (set) {
            X11::setAtom(_window, WINDOW_TYPE, WINDOW_TYPE_NORMAL);
        }
    }
}

/**
 * Read WM Hints, return the initial state of the window.
 */
ulong
Client::getWMHints(void)
{
    ulong initial_state = NormalState;
    XWMHints* hints = XGetWMHints(X11::getDpy(), _window);
    if (hints) {
        // get the input focus mode
        if (hints->flags&InputHint) { // FIXME: More logic needed
            _wm_hints_input = hints->input;
        }

        // Get initial state of the window
        if (hints->flags&StateHint) {
            initial_state = hints->initial_state;
        }

        setDemandsAttention(hints->flags&XUrgencyHint);
        X11::free(hints);
    }
    return initial_state;
}

void
Client::getWMNormalHints(void)
{
    long dummy;
    XGetWMNormalHints(X11::getDpy(), _window, _size, &dummy);

    // let's do some sanity checking
    if (_size->flags&PBaseSize) {
        if (_size->base_width < 1 || _size->base_height < 1) {
            _size->base_width = _size->base_height = 0;
            _size->flags &= ~PBaseSize;
        }
    }
    if (_size->flags&PAspect) {
        if (_size->min_aspect.x < 1 || _size->min_aspect.y < 1 ||
            _size->max_aspect.x < 1 || _size->max_aspect.y < 1) {

            _size->min_aspect.x = _size->min_aspect.y = 0;
            _size->max_aspect.x = _size->max_aspect.y = 0;
            _size->flags &= ~PAspect;
        }
    }
    if (_size->flags&PResizeInc) {
        if (_size->width_inc < 1 || _size->height_inc < 1) {
            _size->width_inc = 0;
            _size->height_inc = 0;
            _size->flags &= ~PResizeInc;
        }
    }

    if (_size->flags & PMaxSize) {
        if (_size->max_width < 1 || _size->max_height < 1 ||
            ((_size->flags & PMinSize) &&
                (_size->max_width < _size->min_width ||
                 _size->max_height < _size->min_height) )) {
            _size->max_width = 0;
            _size->max_height = 0;
            _size->flags &= ~PMaxSize;
        }
    }
}

void
Client::getWMProtocols(void)
{
    int count;
    Atom *protocols;

    if (XGetWMProtocols(X11::getDpy(), _window, &protocols, &count) != 0) {
        for (int i = 0; i < count; ++i) {
            if (protocols[i] == X11::getAtom(WM_TAKE_FOCUS)) {
                _send_focus_message = true;
            } else if (protocols[i] == X11::getAtom(WM_DELETE_WINDOW)) {
                _send_close_message = true;
            }
        }

        X11::free(protocols);
    }
}

/**
 * Read WM_TRANSIENT_FOR hint.
 */
void
Client::getTransientForHint(void)
{
    _transient_for_window = None;

    Client *transient_for = nullptr;
    XGetTransientForHint(X11::getDpy(), _window, &_transient_for_window);
    if (_transient_for_window != None) {
        if (_transient_for_window == _window) {
            ERR(this << " client set transient hint for itself");
            _transient_for_window = None;
        } else {
            transient_for = findClientFromWindow(_transient_for_window);
            TRACE(this << " transient for " << _transient_for_window
                  << " client " << transient_for);
        }
    }

    setTransientFor(transient_for);
}

void
Client::setTransientFor(Client *transient_for)
{
    if (_transient_for == transient_for) {
        return;
    }

    TRACE(this << " transient for changed from " << _transient_for
          << " to " << transient_for);

    if (_transient_for) {
        _transient_for->removeTransient(this);
    }
    if (transient_for) {
        transient_for->addTransient(this);
    }
    _transient_for = transient_for;
}

void
Client::addTransient(Client *transient)
{
    TRACE(this << " add transient: " << transient);
    assert(transient != this);
    _transients.push_back(transient);
}

void
Client::removeTransient(Client *transient)
{
    TRACE(this << " remove transient: " << transient);
    assert(transient != this);
    _transients.erase(std::remove(_transients.begin(), _transients.end(),
                                  transient),
                      _transients.end());
}

void
Client::updateParentLayerAndRaiseIfActive(void)
{
    Frame *frame = static_cast<Frame*>(getParent());
    if (frame->getActiveChild() == this) {
        frame->setLayer(getLayer());
        frame->raise();
    }
}

/**
   Read the NET_WM_STRUT hint from the client window if any, adding it to
   the global list of struts if the client does not have Strut in it's CfgDeny.
*/
void
Client::getStrutHint(void)
{
    // Clear out old strut, well re-add if a new one is found.
    removeStrutHint();

    int num = 0;
    long *strut = static_cast<long*>(X11::getEwmhPropData(_window, NET_WM_STRUT,
                                                          XA_CARDINAL, num));
    if (strut) {
        _strut = new Strut();
        *_strut = strut;
        X11::free(strut);
        if (! isCfgDeny(CFG_DENY_STRUT)) {
            pekwm::rootWo()->addStrut(_strut);
        }
    }
}

/**
   Free resources used by the clients strut hint if any and unregister it from
   the screen if Strut is not in it's CfgDeny.
*/
void
Client::removeStrutHint(void)
{
    if ( _strut) {
        if (! isCfgDeny(CFG_DENY_STRUT)) {
            pekwm::rootWo()->removeStrut(_strut);
        }
        delete _strut;
        _strut = 0;
    }
}

/**
 * Get _PEKWM_FRAME_ORDER hint from client, return < 0 on failure.
 */
long
Client::getPekwmFrameOrder(void)
{
    Cardinal num = -1;
    X11::getCardinal(_window, PEKWM_FRAME_ORDER, num);
    return num;
}

/**
 * Update _PEKWM_FRAME_ORDER hint on client window.
 */
void
Client::setPekwmFrameOrder(long num)
{
    X11::setCardinal(_window, PEKWM_FRAME_ORDER, num);
}

/**
 * Get _PEKWM_FRAME_ACTIVE hint from client window, return true if
 * client is treated as active.
 */
bool
Client::getPekwmFrameActive(void)
{
    Cardinal act = 0;
    return (X11::getCardinal(_window, PEKWM_FRAME_ACTIVE, act)
            && act == 1);
}

/**
 * Set _PEKWM_FRAME_ACTIVE hint on client window.
 */
void
Client::setPekwmFrameActive(bool act)
{
    X11::setCardinal(_window, PEKWM_FRAME_ACTIVE, act ? 1 : 0);
}

/**
 * Update the environment with CLIENT_PID and CLIENT_WINDOW.
 */
void
Client::setClientEnvironment(Client *client)
{
    if (client) {
        setenv("CLIENT_PID",
               std::to_string(client->isRemote()
                              ? -1 : client->getPid()).c_str(), 1);
        setenv("CLIENT_WINDOW", std::to_string(client->getWindow()).c_str(), 1);
    } else {
        unsetenv("CLIENT_PID");
        unsetenv("CLIENT_WINDOW");
    }
}
