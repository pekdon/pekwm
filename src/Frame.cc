//
// Frame.cc for pekwm
// Copyright (C) 2002-2020 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "config.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <iostream>

extern "C" {
#include <X11/Xatom.h>
}

#include "Debug.hh"
#include "PWinObj.hh"
#include "PDecor.hh"
#include "Frame.hh"

#include "Charset.hh"
#include "Compat.hh"
#include "X11.hh"
#include "Config.hh"
#include "ActionHandler.hh"
#include "AutoProperties.hh"
#include "Client.hh"
#include "ClientMgr.hh"
#include "ManagerWindows.hh"
#include "StatusWindow.hh"
#include "Workspaces.hh"
#include "KeyGrabber.hh"
#include "Theme.hh"
#include "X11Util.hh"

std::vector<Frame*> Frame::_frames;
std::vector<uint> Frame::_frameid_list;

ActionEvent Frame::_ae_move = ActionEvent(Action(ACTION_MOVE));
ActionEvent Frame::_ae_move_resize = ActionEvent(Action(ACTION_MOVE_RESIZE));

Frame* Frame::_tag_frame = nullptr;
bool Frame::_tag_behind = false;


Frame::Frame()
    : PDecor(DEFAULT_DECOR_NAME, None, false),
      _id(0),
      _client(nullptr),
      _class_hint(nullptr),
      _non_fullscreen_decor_state(0),
      _non_fullscreen_layer(LAYER_NORMAL)
{
}

Frame::Frame(Client *client, AutoProperty *ap)
    : PDecor(client->getAPDecorName(), client->getWindow(), true),
      _id(0),
      _client(client),
      _class_hint(nullptr),
      _non_fullscreen_decor_state(0),
      _non_fullscreen_layer(LAYER_NORMAL)
{
    // PWinObj attributes
    _type = WO_FRAME;

    // PDecor attributes
    _decor_cfg_child_move_overloaded = true;
    _decor_cfg_bpr_replay_pointer = true;
    _decor_cfg_bpr_al_title = MOUSE_ACTION_LIST_TITLE_FRAME;
    _decor_cfg_bpr_al_child = MOUSE_ACTION_LIST_CHILD_FRAME;

    // grab buttons so that we can reply them
    for (auto it : pekwm::config()->getClientMouseActionButtons()) {
        if (it.button == BUTTON_ANY) {
            continue;
        }

        for (auto mod : it.mods) {
            X11Util::grabButton(it.button, mod,
                                ButtonPressMask|ButtonReleaseMask,
                                _window, GrabModeSync);
        }
    }

    // get unique id of the frame, if the client didn't have an id
    if (! pekwm::isStartup()) {
        Cardinal id;
        if (X11::getCardinal(client->getWindow(), PEKWM_FRAME_ID, id)) {
            _id = id;
        }

    } else {
        _id = findFrameID();
    }

    // get the clients class_hint
    _class_hint = new ClassHint();
    *_class_hint = *client->getClassHint();

    X11::grabServer();

    // We don't send any ConfigurRequests during setup, we send one when we
    // are finished to minimize traffic and confusion
    client->setConfigureRequestLock(true);

    // Before setting position and size up we make sure the decor state
    // of the client match the decore state of the framewidget
    if (! client->hasBorder()) {
        setBorder(STATE_UNSET);
    }
    if (! client->hasTitlebar()) {
        setTitlebar(STATE_UNSET);
    }

    // We first get the size of the window as it will be needed when placing
    // the window with the help of WinGravity.
    resizeChild(client->getWidth(), client->getHeight());

    // setup position
    bool place = false;
    if (client->isViewable() || client->isPlaced()
          || (client->cameWithPosition()
              && ! client->isCfgDeny(CFG_DENY_POSITION))) {
        moveChild(client->getX(), client->getY());
    } else {
        place = pekwm::config()->isPlaceNew();
    }

    // override both position and size with autoproperties
    bool ap_geometry = false;
    if (ap) {
        if (ap->isMask(AP_FRAME_GEOMETRY|AP_CLIENT_GEOMETRY)) {
            ap_geometry = true;

            setupAPGeometry(client, ap);

            if (ap->frame_gm_mask&(X_VALUE|Y_VALUE)
                || ap->client_gm_mask&(X_VALUE|Y_VALUE)) {
                place = false;
            }
        }

        if (ap->isMask(AP_PLACE_NEW)) {
            place = ap->place_new;
        }
    }

    _non_fullscreen_decor_state = client->getDecorState();
    _non_fullscreen_layer = client->getLayer();

    X11::ungrabServer(true); // ungrab and sync

    // now insert the client in the frame we created.
    addChild(client);

    // needs to be done before the workspace insert and after the client
    // has been inserted, in order for layer settings to be propagated
    setLayer(client->getLayer());

    // I add these to the list before I insert the client into the frame to
    // be able to skip an extra updateClientList
    _frames.push_back(this);
    Workspaces::addToMRUBack(this);

    activateChild(client);

    // set the window states, shaded, maximized...
    getState(client);

    if (! _client->hasStrut() && ! ap_geometry) {
        if (fixGeometry()) {
            moveResize(_gm.x, _gm.y, _gm.width, _gm.height);
        }
    }

    client->setConfigureRequestLock(false);
    client->configureRequestSend();

    // Figure out if we should be hidden or not, do not read autoprops
    PDecor::setWorkspace(_client->getWorkspace());

    woListAdd(this);
    _wo_map[_window] = this;

    // still need a position?
    if (place) {
        Workspaces::layout(this, client->getTransientForClientWindow());
    }

    _old_gm = _gm;
}

Frame::~Frame(void)
{
    // remove from lists
    _wo_map.erase(_window);
    woListRemove(this);
    _frames.erase(std::remove(_frames.begin(), _frames.end(), this),
                  _frames.end());
    Workspaces::removeFromMRU(this);
    if (_tag_frame == this) {
        _tag_frame = 0;
    }

    returnFrameID(_id);

    if (_class_hint) {
        delete _class_hint;
    }

    workspacesRemove();
}

// START - PWinObj interface.

//! @brief Iconifies the Frame.
void
Frame::iconify(void)
{
    if (_iconified) {
        return;
    }
    _iconified = true;

    unmapWindow();
}

//! @brief Toggles the Frame's sticky state
void
Frame::stick(void)
{
    _client->setSticky(_sticky); // FIXME: FRAME
    _client->stick();

    _sticky = ! _sticky;

    // make sure it's visible/hidden
    PDecor::setWorkspace(Workspaces::getActive());
    updateDecor();
}

//! @brief Sets workspace on frame, wrapper to allow autoproperty loading
void
Frame::setWorkspace(unsigned int workspace)
{
    // Duplicate the behavior done in PDecor::setWorkspace to have a sane
    // value on _workspace and not NET_WM_STICKY_WINDOW.
    if (workspace != NET_WM_STICKY_WINDOW) {
        // Check for DisallowedActions="SetWorkspace".
        if (! _client->allowChangeWorkspace()) {
            return;
        }

        // First we set the workspace, then load autoproperties for possible
        // overrun of workspace and then set the workspace.
        _workspace = workspace;
        readAutoprops(APPLY_ON_WORKSPACE);
        workspace = _workspace;
    }

    PDecor::setWorkspace(workspace);
    updateDecor();
}

void
Frame::setLayer(Layer layer)
{
    PDecor::setLayer(layer);

    if (_client->getLayer() != layer) {
        _client->setLayer(layer);

        LayerObservation observation(layer);
        _client->notifyObservers(&observation);
    }
}

// event handlers

ActionEvent*
Frame::handleMotionEvent(XMotionEvent *ev)
{
    // This is true when we have a title button pressed and then we don't want
    // to be able to drag windows around, therefore we ignore the event
    if (_button) {
        return 0;
    }

    ActionEvent *ae = nullptr;
    uint button = X11::getButtonFromState(ev->state);

    if (ev->window == getTitleWindow()) {
        ae = ActionHandler::findMouseAction(button, ev->state, MOUSE_EVENT_MOTION,
                                            pekwm::config()->getMouseActionList(MOUSE_ACTION_LIST_TITLE_FRAME));
    } else if (ev->window == _client->getWindow()) {
        ae = ActionHandler::findMouseAction(button, ev->state, MOUSE_EVENT_MOTION,
                                            pekwm::config()->getMouseActionList(MOUSE_ACTION_LIST_CHILD_FRAME));
    } else {
        uint pos = getBorderPosition(ev->subwindow);

        // If ev->subwindow wasn't one of the border windows, perhaps
        // ev->window is.
        if (pos == BORDER_NO_POS) {
            pos = getBorderPosition(ev->window);
        }
        if (pos != BORDER_NO_POS) {
            auto *bl = pekwm::config()->getBorderListFromPosition(pos);
            ae = ActionHandler::findMouseAction(button, ev->state,
                                                MOUSE_EVENT_MOTION, bl);
        }
    }

    // check motion threshold
    if (ae && (ae->threshold > 0)) {
        if (! ActionHandler::checkAEThreshold(ev->x_root, ev->y_root,
                                             _pointer_x, _pointer_y,
                                              ae->threshold)) {
            ae = nullptr;
        }
    }

    return ae;
}

ActionEvent*
Frame::handleEnterEvent(XCrossingEvent *ev)
{
    // Run event handler to get hoovering to work but ignore action
    // returned.
    PDecor::handleEnterEvent(ev);

    ActionEvent *ae = 0;
    std::vector<ActionEvent> *al = 0;
    auto cfg = pekwm::config();

    if (ev->window == getTitleWindow() || findButton(ev->window)) {
        al = cfg->getMouseActionList(MOUSE_ACTION_LIST_TITLE_FRAME);
    } else if (ev->subwindow == _client->getWindow()) {
        al = cfg->getMouseActionList(MOUSE_ACTION_LIST_CHILD_FRAME);
    } else {
        uint pos = getBorderPosition(ev->window);
        if (pos != BORDER_NO_POS) {
            al = cfg->getBorderListFromPosition(pos);
        }
    }

    if (al) {
        ae = ActionHandler::findMouseAction(BUTTON_ANY, ev->state, MOUSE_EVENT_ENTER, al);
    }

    return ae;
}

ActionEvent*
Frame::handleLeaveEvent(XCrossingEvent *ev)
{
    // Run event handler to get hoovering to work but ignore action
    // returned.
    PDecor::handleLeaveEvent(ev);

    ActionEvent *ae;

    MouseActionListName ln = MOUSE_ACTION_LIST_TITLE_FRAME;
    if (ev->window == _client->getWindow()) {
        ln = MOUSE_ACTION_LIST_CHILD_FRAME;
    }

    ae = ActionHandler::findMouseAction(BUTTON_ANY, ev->state, MOUSE_EVENT_LEAVE,
                                        pekwm::config()->getMouseActionList(ln));

    return ae;
}

ActionEvent*
Frame::handleMapRequest(XMapRequestEvent *ev)
{
    if (ev->window != _client->getWindow()) {
        return 0;
    }

    if (! _sticky && _workspace != Workspaces::getActive()) {
        LOG("Ignoring MapRequest, not on current workspace!");
        return 0;
    }

    mapWindow();

    return 0;
}

ActionEvent*
Frame::handleUnmapEvent(XUnmapEvent *ev)
{
    for (auto it : _children) {
        if (*it == ev->window) {
            it->handleUnmapEvent(ev);
            break;
        }
    }

    return 0;
}

// END - PWinObj interface.

#ifdef HAVE_SHAPE
void
Frame::handleShapeEvent(XShapeEvent *ev)
{
    if (ev->window != _client->getWindow()) {
        return;
    }
    applyBorderShape(ev->kind);
}
#endif // HAVE_SHAPE

// START - PDecor interface.

bool
Frame::allowMove(void) const
{
    return _client->allowMove();
}

/**
 * Return active client, or 0 if no clients or active child is not a Client.
 */
Client*
Frame::getActiveClient(void)
{
    if (getActiveChild() && getActiveChild()->getType() == WO_CLIENT) {
        return static_cast<Client*>(getActiveChild());
    } else {
        return 0;
    }
}

//! @brief Adds child to the frame.
void
Frame::addChild(PWinObj *child, std::vector<PWinObj*>::iterator *it)
{
    PDecor::addChild(child, it);
    X11::setCardinal(child->getWindow(), PEKWM_FRAME_ID, _id);
    child->lower();

    Client *client = dynamic_cast<Client*>(child);
    if (client && client->demandsAttention()) {
        incrAttention();
    }
}

/**
 * Add child preserving order from the previous pekwm run.
 */
void
Frame::addChildOrdered(Client *child)
{
    Client *client;
    auto it(_children.begin());
    for (; it != _children.end(); ++it) {
        client = static_cast<Client*>(*it);
        if (child->getInitialFrameOrder() < client->getInitialFrameOrder()) {
            break;
        }
    }

    addChild(child, &it);
}

//! @brief Removes child from the frame.
void
Frame::removeChild(PWinObj *child, bool do_delete)
{
    if (static_cast<Client *>(child)->demandsAttention()) {
        decrAttention();
    }
    PDecor::removeChild(child, do_delete);
}

/**
 * Activates child in Frame, updating it's state and re-loading decor
 * rules to match updated title.
 */
void
Frame::activateChild(PWinObj *child)
{
    // FIXME: Update default decoration for this child, can change
    // decoration from DEFAULT to REMOTE or WARNING

    // Sync the frame state with the client only if we already had a client
    auto new_client = static_cast<Client*>(child);
    if (_client != new_client) {
        applyState(new_client);
    }
    setFrameExtents(new_client);

    _client = new_client;
    if (_client->demandsAttention()) {
        _client->setDemandsAttention(false);
        decrAttention();
    }
    PDecor::activateChild(child);

    // applyBorderShape() uses current active child, so we need to activate
    // the child before setting shape
    if (X11::hasExtensionShape()) {
        applyBorderShape(ShapeBounding);
        applyBorderShape(ShapeInput);
    }

    setOpacity(_client);

    if (_focused) {
        child->giveInputFocus();
    }

    // Reload decor rules if needed.
    handleTitleChange(_client, false);
}

/**
 * Called when child order is updated, re-sets the titles and updates
 * the _PEKWM_FRAME hints.
 */
void
Frame::updatedChildOrder(void)
{
    titleClear();

    Client *client;
    auto it(_children.begin());
    for (long num = 0; it != _children.end(); ++num, ++it) {
        client = static_cast<Client*>(*it);
        client->setPekwmFrameOrder(num);
        titleAdd(client->getTitle());
    }

    updatedActiveChild();
}

/**
 * Set the active title and update the _PEKWM_FRAME hints.
 */
void
Frame::updatedActiveChild(void)
{
    titleSetActive(0);

    Client *client;
    uint size = _children.size();
    for (uint i = 0; i < size; ++i) {
        client = static_cast<Client*>(_children[i]);
        client->setPekwmFrameActive(_child == client);
        if (_child == client) {
            titleSetActive(i);
        }
    }

    renderTitle();
}

void
Frame::getDecorInfo(wchar_t *buf, uint size, const Geometry& gm)
{
    uint width, height;
    calcSizeInCells(width, height, gm);
    swprintf(buf, size, L"%d+%d+%d+%d", width, height, _gm.x, _gm.y);
}

void
Frame::giveInputFocus(void)
{
    if (_client->demandsAttention()) {
        _client->setDemandsAttention(false);
        decrAttention();
    }
    PDecor::giveInputFocus();
}

void
Frame::setShaded(StateAction sa)
{
    bool shaded = isShaded();

    // Check for DisallowedActions="Shade"
    if (! _client->allowShade()) {
        sa = STATE_UNSET;
    }

    PDecor::setShaded(sa);
    if (shaded != isShaded()) {
        _client->setShade(isShaded());
        _client->updateEwmhStates();
    }
}

/**
 * Decor has been changed or titlebar/border state has been changed,
 * update the _NET_FRAME_EXTENTS.
 */
void
Frame::decorUpdated(void)
{
    if (_client) {
        setFrameExtents(_client);
    }
}

int
Frame::resizeHorzStep(int diff) const
{
    int diff_ret = 0;
    uint min = _gm.width - getChildWidth();
    if (min == 0) { // borderless windows, we don't want X errors
        min = 1;
    }
    auto hints = _client->getActiveSizeHints();

    // if we have ResizeInc hint set we use it instead of pixel diff
    if (hints.flags&PResizeInc) {
        if (diff > 0) {
            diff_ret = hints.width_inc;
        } else if ((_gm.width - hints.width_inc) >= min) {
            diff_ret = -hints.width_inc;
        }
    } else if ((_gm.width + diff) >= min) {
        diff_ret = diff;
    }

    // check max/min size hints
    if (diff > 0) {
        if ((hints.flags&PMaxSize) &&
                ((getChildWidth() + diff) > unsigned(hints.max_width))) {
            diff_ret = _gm.width - hints.max_width + min;
        }
    } else if ((hints.flags&PMinSize) &&
               ((getChildWidth() + diff) < unsigned(hints.min_width))) {
        diff_ret = _gm.width - hints.min_width + min;
    }

    return diff_ret;
}

int
Frame::resizeVertStep(int diff) const
{
    int diff_ret = 0;
    uint min = _gm.height - getChildHeight();
    if (min == 0) { // borderless windows, we don't want X errors
        min = 1;
    }
    auto hints = _client->getActiveSizeHints();

    // if we have ResizeInc hint set we use it instead of pixel diff
    if (hints.flags&PResizeInc) {
        if (diff > 0) {
            diff_ret = hints.height_inc;
        } else if ((_gm.height - hints.height_inc) >= min) {
            diff_ret = -hints.height_inc;
        }
    } else {
        diff_ret = diff;
    }

    // check max/min size hints
    if (diff > 0) {
        if ((hints.flags&PMaxSize) &&
                ((getChildHeight() + diff) > unsigned(hints.max_height))) {
            diff_ret = _gm.height - hints.max_height + min;
        }
    } else if ((hints.flags&PMinSize) &&
               ((getChildHeight() + diff) < unsigned(hints.min_height))) {
        diff_ret = _gm.height - hints.min_width + min;
    }

    return diff_ret;
}

/**
 * Return decor name for the current client or attention decor if
 * Frame has client which demands attention.
 */
std::string
Frame::getDecorName(void)
{
    if (! demandAttention()) {
        auto name = _client->getAPDecorName();
        if (! name.empty()) {
            return name;
        }
    }
    return PDecor::getDecorName();
}

// END - PDecor interface.

//! @brief Sets _PEKWM_FRAME_ID on all children in the frame
void
Frame::setId(uint id)
{
    _id = id;
    for (auto it : _children) {
        X11::setCardinal(it->getWindow(), PEKWM_FRAME_ID, id);
    }
}

//! @brief Gets the state from the Client
void
Frame::getState(Client *cl)
{
    if (! cl)
        return;

    bool b_client_iconified = cl->isIconified ();

    if (_sticky != cl->isSticky())
        _sticky = ! _sticky;
    if (_maximized_horz != cl->isMaximizedHorz())
        setStateMaximized(STATE_TOGGLE, true, false, false);
    if (_maximized_vert != cl->isMaximizedVert())
        setStateMaximized(STATE_TOGGLE, false, true, false);
    if (isShaded() != cl->isShaded())
        setShaded(STATE_TOGGLE);
    if (getLayer() != cl->getLayer()) {
        setLayer(cl->getLayer());
    }
    if (_workspace != cl->getWorkspace())
        PDecor::setWorkspace(cl->getWorkspace());

    // We need to set border and titlebar before setting fullscreen, as
    // fullscreen will unset border and titlebar if needed.
    if (hasBorder() != _client->hasBorder())
        setBorder(STATE_TOGGLE);
    if (hasTitlebar() != _client->hasTitlebar())
        setTitlebar(STATE_TOGGLE);
    if (_fullscreen != cl->isFullscreen())
        setStateFullscreen(STATE_TOGGLE);

    if (_iconified != b_client_iconified) {
        if (_iconified) {
            mapWindow();
        } else {
            iconify();
        }
    }

    if (_skip != cl->getSkip()) {
        setSkip(cl->getSkip());
    }
}

//! @brief Applies the frame's state on the Client
void
Frame::applyState(Client *cl)
{
    if (! cl) {
        return;
    }
    
    cl->setSticky(_sticky);
    cl->setMaximizedHorz(_maximized_horz);
    cl->setMaximizedVert(_maximized_vert);
    cl->setShade(isShaded());
    cl->setWorkspace(_workspace);
    cl->setLayer(getLayer());

    // fix border / titlebar state
    cl->setBorder(hasBorder());
    cl->setTitlebar(hasTitlebar());
    // make sure the window has the correct mapped state
    if (_mapped != cl->isMapped()) {
        if (! _mapped) {
            cl->unmapWindow();
        } else {
            cl->mapWindow();
        }
    }

    cl->updateEwmhStates();
}

void
Frame::setFrameExtents(Client *cl)
{
    Cardinal extents[4];
    extents[0] = bdLeft(this);
    extents[1] = bdRight(this);
    extents[2] = bdTop(this) + titleHeight(this);
    extents[3] = bdBottom(this);
    X11::setCardinals(cl->getWindow(), NET_FRAME_EXTENTS, extents, 4);
}

//! @brief Sets skip state.
void
Frame::setSkip(uint skip)
{
    PDecor::setSkip(skip);
    _client->setSkip(skip);
}

//! @brief Find Frame with Window
//! @param win Window to search for.
//! @return Frame if found, else 0.
Frame*
Frame::findFrameFromWindow(Window win)
{
    // Validate input window.
    if ((win == None) || (win == X11::getRoot())) {
        return 0;
    }

    for (auto it : _frames) {
        if (win == it->getWindow()) { // operator == does more than that
            return it;
        }
    }
    return 0;
}

//! @brief Find Frame with id.
//! @param id ID to search for.
//! @return Frame if found, else 0.
Frame*
Frame::findFrameFromID(uint id)
{
    for (auto it : _frames) {
        if (it->getId() == id) {
            return it;
        }
    }
    return 0;
}

void
Frame::setupAPGeometry(Client *client, AutoProperty *ap)
{
    // frame geomtry overides client geometry

    // get client geometry
    if (ap->isMask(AP_CLIENT_GEOMETRY)) {
        Geometry gm(client->_gm);
        applyGeometry(gm, ap->client_gm, ap->client_gm_mask);

        if (ap->client_gm_mask&(X_VALUE|Y_VALUE)) {
            moveChild(gm.x, gm.y);
        }
        if(ap->client_gm_mask&(WIDTH_VALUE|HEIGHT_VALUE)) {
            resizeChild(gm.width, gm.height);
        }
        TRACE("applied ClientGeometry property "
              << ap->client_gm << " -> " << gm);
    }

    // get frame geometry
    if (ap->isMask(AP_FRAME_GEOMETRY)) {
        Geometry screen_gm = X11::getScreenGeometry();
        Geometry gm(_gm);
        applyGeometry(gm, ap->frame_gm, ap->frame_gm_mask, screen_gm);
        moveResize(gm, ap->frame_gm_mask);
        TRACE("applied FrameGeometry property "
              << ap->frame_gm << " -> " << gm);
    }
}

void
Frame::applyGeometry(Geometry &gm, const Geometry &ap_gm, int mask)
{
    applyGeometry(gm, ap_gm, mask, X11::getScreenGeometry());
}

/**
 * Apply ap_gm on gm using screen_gm for percent/negative.
 *
 * @param gm Geometry to modify.
 * @param ap_gm Geometry to get values from.
 * @param mask Geometry mask.
 * @param screen_gm Geometry of the screen/head for position.
 */
void
Frame::applyGeometry(Geometry &gm, const Geometry &ap_gm, int mask,
                     const Geometry &screen_gm)
{
    // Read size before position so negative position works, if size is
    // < 1 consider it to be full screen size.
    if (mask & WIDTH_VALUE) {
        if (mask & WIDTH_PERCENT) {
            gm.width = int(screen_gm.width * (float(ap_gm.width) / 100));
        } else if (ap_gm.width < 1) {
            gm.width = screen_gm.width;
        } else {
            gm.width = ap_gm.width;
        }
    }

    if (mask & HEIGHT_VALUE) {
        if (mask & HEIGHT_PERCENT) {
            gm.height = int(screen_gm.height * (float(ap_gm.height) / 100));
        } else if (ap_gm.height < 1) {
            gm.height = screen_gm.height;
        } else {
            gm.height = ap_gm.height;
        }
    }

    // Read position
    if (mask & X_VALUE) {
        if (mask & X_PERCENT) {
            gm.x = int(screen_gm.width * (float(ap_gm.x) / 100));
        } else if (mask & X_NEGATIVE) {
            gm.x = screen_gm.width - gm.width - ap_gm.x;
        } else {
            gm.x = screen_gm.x + ap_gm.x;
        }
    }
    if (mask & Y_VALUE) {
        if (mask & Y_PERCENT) {
            gm.y = int(screen_gm.height * (float(ap_gm.y) / 100));
        } else if (mask & Y_NEGATIVE) {
            gm.y = screen_gm.height - gm.height - ap_gm.y;
        } else {
            gm.y = screen_gm.y + ap_gm.y;
        }
    }
}

//! @brief Finds free Frame ID.
//! @return First free Frame ID.
uint
Frame::findFrameID(void)
{
    uint id = 0;

    if (_frameid_list.size()) {
        // Check for used Frame IDs
        id = _frameid_list.back();
        _frameid_list.pop_back();
    } else {
        // No free, get next number (Frame is not in list when this is called.)
        id = _frames.size() + 1;
    }

    return id;
}

//! @brief Returns Frame ID to used frame id list.
//! @param id ID to return.
void
Frame::returnFrameID(uint id)
{
    auto it(_frameid_list.begin());
    for (; it != _frameid_list.end() && id < *it; ++it)
        ;
    _frameid_list.insert(it, id);
}

//! @brief Resets Frame IDs.
void
Frame::resetFrameIDs(void)
{
    auto it(_frames.begin());
    for (uint id = 1; it != _frames.end(); ++id, ++it) {
        (*it)->setId(id);
    }
}


//! @brief Removes the client from the Frame and creates a new Frame for it
void
Frame::detachClient(Client *client)
{
    if (client->getParent() != this) {
        return;
    }

    if (_children.size() > 1) {
        removeChild(client);

        client->move(_gm.x, _gm.y + bdTop(this));

        Frame *frame = new Frame(client, 0);
        frame->workspacesInsert();

        client->setParent(frame);
        client->setWorkspace(Workspaces::getActive());

        setFocused(false);
    }
}

//! @brief Makes sure the frame doesn't cover any struts / the harbour.
bool
Frame::fixGeometry(void)
{
    uint head_nr = X11Util::getNearestHead(*this);
    Geometry head, before;
    if (_fullscreen) {
        X11::getHeadInfo(head_nr, head);
    } else {
        pekwm::rootWo()->getHeadInfoWithEdge(head_nr, head);
    }

    before = _gm;

    // fix size
    if (_gm.width > head.width) {
        _gm.width = head.width;
    }
    if (_gm.height > head.height) {
        _gm.height = head.height;
    }

    // fix position
    if (_gm.x < head.x) {
        _gm.x = head.x;
    } else if ((_gm.x + _gm.width) > (head.x + head.width)) {
        _gm.x = head.x + head.width - _gm.width;
    }
    if (_gm.y < head.y) {
        _gm.y = head.y;
    } else if ((_gm.y + _gm.height) > (head.y + head.height)) {
        _gm.y = head.y + head.height - _gm.height;
    }

    return (_gm != before);
}

/**
 * Initiates grouping move, based on a XMotionEvent.
 *
 * @fixme rewrite
 */
void
Frame::doGroupingDrag(XMotionEvent *ev, Client *client, bool behind)
{
    if (! client) {
        return;
    }

    int o_x, o_y;
    o_x = ev ? ev->x_root : 0;
    o_y = ev ? ev->y_root : 0;

    std::wstring name(L"Grouping ");
    if (client->getTitle()->getVisible().size() > 0) {
        name += client->getTitle()->getVisible();
    } else {
        name += L"No Name";
    }

    bool status = X11::grabPointer(X11::getRoot(),
                                   ButtonReleaseMask|PointerMotionMask,
                                   CURSOR_NONE);
    if (status != true) {
        return;
    }

    StatusWindow *sw = pekwm::statusWindow();

    sw->draw(name); // resize window and render bg
    sw->move(o_x, o_y);
    sw->mapWindowRaised();
    sw->draw(name); // redraw after map

    XEvent e;
    while (true) { // this breaks when we get an button release
        XMaskEvent(X11::getDpy(), PointerMotionMask|ButtonReleaseMask, &e);

        switch (e.type)  {
        case MotionNotify:
            // update the position
            o_x = e.xmotion.x_root;
            o_y = e.xmotion.y_root;

            sw->move(o_x, o_y);
            sw->draw(name);
            break;

        case ButtonRelease:
            sw->unmapWindow();
            X11::ungrabPointer();

            Client *search = 0;

            // only group if we have grouping turned on
            if (ClientMgr::isAllowGrouping()) {
                int x, y;
                Window win;

                // find the frame we dropped the client on
                XTranslateCoordinates(X11::getDpy(),
                                      X11::getRoot(), X11::getRoot(),
                                      e.xmotion.x_root, e.xmotion.y_root,
                                      &x, &y, &win);

                search = Client::findClient(win);
            }

            // if we found a client, and it's not in the current frame and
            // it has a "normal" ( make configurable? ) layer we group
            if (search && search->getParent() &&
                    (search->getParent() != this) &&
                    (search->getLayer() > LAYER_BELOW) &&
                    (search->getLayer() < LAYER_ONTOP)) {

                // if we currently have focus and the frame exists after we remove
                // this client we need to redraw it as unfocused
                bool focus = behind ? false : (_children.size() > 1);

                removeChild(client);

                Frame *frame = static_cast<Frame*>(search->getParent());
                frame->addChild(client);
                if (! behind) {
                    frame->activateChild(client);
                    frame->giveInputFocus();
                }

                if (focus) {
                    setFocused(false);
                }

            }  else if (_children.size() > 1) {
                // if we have more than one client in the frame detach this one
                removeChild(client);

                client->move(e.xmotion.x_root, e.xmotion.y_root);

                Frame *frame = new Frame(client, 0);
                client->setParent(frame);
                // make sure the client ends up on the current workspace
                client->setWorkspace(Workspaces::getActive());
                frame->workspacesInsert();

                // make sure it get's focus
                setFocused(false);
                frame->giveInputFocus();
            }

            return;
        }
    }
}

//! @brief Initiates resizing of a window based on motion event
void
Frame::doResize(XMotionEvent *ev)
{
    if (! ev) {
        return;
    }
    
    // figure out which part of the window we are in
    bool left = false, top = false;
    if (ev->x < signed(_gm.width / 2)) {
        left = true;
    }
    if (ev->y < signed(_gm.height / 2)) {
        top = true;
    }
    
    doResize(left, true, top, true);
}

//! @brief Initiates resizing of a window based border position
void
Frame::doResize(BorderPosition pos)
{
    bool x = false, y = false;
    bool left = false, top = false, resize = true;

    switch (pos) {
    case BORDER_TOP_LEFT:
        x = y = left = top = true;
        break;
    case BORDER_TOP:
        y = top = true;
        break;
    case BORDER_TOP_RIGHT:
        x = y = top = true;
        break;
    case BORDER_LEFT:
        x = left = true;
        break;
    case BORDER_RIGHT:
        x = true;
        break;
    case BORDER_BOTTOM_LEFT:
        x = y = left = true;
        break;
    case BORDER_BOTTOM:
        y = true;
        break;
    case BORDER_BOTTOM_RIGHT:
        x = y = true;
        break;
    default:
        resize = false;
        break;
    }

    if (resize) {
        doResize(left, x, top, y);
    }
}

//! @brief Resizes the frame by handling MotionNotify events.
void
Frame::doResize(bool left, bool x, bool top, bool y)
{
    if (! _client->allowResize() || isShaded()) {
        return;
    }

    if (! X11::grabPointer(X11::getRoot(), ButtonMotionMask|ButtonReleaseMask,
                           CURSOR_RESIZE)) {
        return;
    }

    setShaded(STATE_UNSET); // make sure the frame isn't shaded
    setStateFullscreen(STATE_UNSET);

    // Initialize variables
    int start_x, new_x;
    int start_y, new_y;
    uint old_width;
    uint old_height;

    start_x = new_x = left ? _gm.x : (_gm.x + _gm.width);
    start_y = new_y = top ? _gm.y : (_gm.y + _gm.height);
    old_width = _gm.width;
    old_height = _gm.height;

    // the basepoint of our window
    _click_x = left ? (_gm.x + _gm.width) : _gm.x;
    _click_y = top ? (_gm.y + _gm.height) : _gm.y;

    int pointer_x = _gm.x, pointer_y = _gm.y;
    X11::getMousePosition(pointer_x, pointer_y);

    wchar_t buf[128];
    getDecorInfo(buf, 128, _gm);

    bool center_on_root = pekwm::config()->isShowStatusWindowOnRoot();
    StatusWindow *sw = pekwm::statusWindow();
    if (pekwm::config()->isShowStatusWindow()) {
        sw->draw(buf, true, center_on_root ? 0 : &_gm);
        sw->mapWindowRaised();
        sw->draw(buf, true, center_on_root ? 0 : &_gm);
    }

    bool outline = ! pekwm::config()->getOpaqueResize();

    // grab server, we don't want invert traces
    if (outline) {
        X11::grabServer();
    }

    const long resize_mask = ButtonPressMask|ButtonReleaseMask|ButtonMotionMask;
    XEvent ev;
    bool exit = false;
    while (exit != true) {
        if (outline) {
            drawOutline(_gm);
        }
        XMaskEvent(X11::getDpy(), resize_mask, &ev);
        if (outline) {
            drawOutline(_gm); // clear
        }

        switch (ev.type) {
        case MotionNotify:
            // Flush all pointer motion, no need to redraw and redraw.
            X11::removeMotionEvents();

            if (x) {
                new_x = start_x - pointer_x + ev.xmotion.x;
            }
            if (y) {
                new_y = start_y - pointer_y + ev.xmotion.y;
            }

            recalcResizeDrag(new_x, new_y, left, top);

            getDecorInfo(buf, 128, _gm);
            if (pekwm::config()->isShowStatusWindow()) {
                sw->draw(buf, true, center_on_root ? 0 : &_gm);
            }

            // only updated when needed when in opaque mode
            if (! outline) {
                if ((old_width != _gm.width) || (old_height != _gm.height)) {
                    moveResize(_gm.x, _gm.y, _gm.width, _gm.height);
                }
                old_width = _gm.width;
                old_height = _gm.height;
            }
            break;
        case ButtonRelease:
            exit = true;
            break;
        }
    }

    if (pekwm::config()->isShowStatusWindow()) {
        sw->unmapWindow();
    }

    X11::ungrabPointer();

    // Make sure the state isn't set to maximized after we've resized.
    if (_maximized_horz || _maximized_vert) {
        _maximized_horz = false;
        _maximized_vert = false;
        _client->setMaximizedHorz(false);
        _client->setMaximizedVert(false);
        _client->updateEwmhStates();
    }

    if (outline) {
        moveResize(_gm.x, _gm.y, _gm.width, _gm.height);
        X11::ungrabServer(true);
    }
}

//! @brief Updates the width, height of the frame when resizing it.
void
Frame::recalcResizeDrag(int nx, int ny, bool left, bool top)
{
    if (left) {
        if (nx >= signed(_click_x - decorWidth(this)))
            nx = _click_x - decorWidth(this) - 1;
    } else {
        if (nx <= signed(_click_x + decorWidth(this)))
            nx = _click_x + decorWidth(this) + 1;
    }

    if (top) {
        if (ny >= signed(_click_y - decorHeight(this))) {
            ny = _click_y - decorHeight(this) - 1;
        }
    } else {
        if (ny <= signed(_click_y + decorHeight(this))) {
            ny = _click_y + decorHeight(this) + 1;
        }
    }

    uint width = left ? (_click_x - nx) : (nx - _click_x);
    uint height = top ? (_click_y - ny) : (ny - _click_y);

    width -= decorWidth(this);
    height -= decorHeight(this);

    _client->getAspectSize(&width, &height, width, height);

    auto hints = _client->getActiveSizeHints();
    // check so we aren't overriding min or max size
    if (hints.flags & PMinSize) {
        if (signed(width) < hints.min_width)
            width = hints.min_width;
        if (signed(height) < hints.min_height)
            height = hints.min_height;
    }

    if (hints.flags & PMaxSize) {
        if (signed(width) > hints.max_width)
            width = hints.max_width;
        if (signed(height) > hints.max_height)
            height = hints.max_height;
    }

    _gm.width = width + decorWidth(this);
    _gm.height = height + decorHeight(this);

    _gm.x = left ? (_click_x - _gm.width) : _click_x;
    _gm.y = top ? (_click_y - _gm.height) : _click_y;
}

void
Frame::moveToHead(int head_nr)
{
    int curr_head_nr = X11Util::getNearestHead(*this);
    if (curr_head_nr == head_nr || head_nr >= X11::getNumHeads()) {
        return;
    }

    Geometry old_gm, new_gm;
    pekwm::rootWo()->getHeadInfoWithEdge(curr_head_nr, old_gm);
    pekwm::rootWo()->getHeadInfoWithEdge(head_nr, new_gm);

    // Ensure the window fits in the new head.
    _gm.x = new_gm.x + (_gm.x - old_gm.x);
    _gm.y = new_gm.y + (_gm.y - old_gm.y);
    _gm.width = std::min(_gm.width, new_gm.width);
    _gm.height = std::min(_gm.height, new_gm.height);

    if ((_gm.x + _gm.width) > (new_gm.x + new_gm.width)) {
        _gm.x = new_gm.x + new_gm.width - _gm.width;
    }
    if ((_gm.y + _gm.height) > (new_gm.y + new_gm.height)) {
        _gm.y = new_gm.y + new_gm.height - _gm.height;
    }

    moveResize(_gm.x, _gm.y, _gm.width, _gm.height);
}

//! @brief Moves the Frame to the screen edge ori ( considering struts )
void
Frame::moveToEdge(OrientationType ori)
{
    uint head_nr;
    Geometry head, real_head;

    head_nr = X11Util::getNearestHead(*this);
    X11::getHeadInfo(head_nr, real_head);
    pekwm::rootWo()->getHeadInfoWithEdge(head_nr, head);

    switch (ori) {
    case TOP_LEFT:
        _gm.x = head.x;
        _gm.y = head.y;
        break;
    case TOP_EDGE:
        _gm.y = head.y;
        break;
    case TOP_CENTER_EDGE:
        _gm.x = real_head.x + ((real_head.width - _gm.width) / 2);
        _gm.y = head.y;
        break;
    case TOP_RIGHT:
        _gm.x = head.x + head.width - _gm.width;
        _gm.y = head.y;
        break;
    case BOTTOM_RIGHT:
        _gm.x = head.x + head.width - _gm.width;
        _gm.y = head.y + head.height - _gm.height;
        break;
    case BOTTOM_EDGE:
        _gm.y = head.y + head.height - _gm.height;
        break;
    case BOTTOM_CENTER_EDGE:
        _gm.x = real_head.x + ((real_head.width - _gm.width) / 2);
        _gm.y = head.y + head.height - _gm.height;
        break;
    case BOTTOM_LEFT:
        _gm.x = head.x;
        _gm.y = head.y + head.height - _gm.height;
        break;
    case LEFT_EDGE:
        _gm.x = head.x;
        break;
    case LEFT_CENTER_EDGE:
        _gm.x = head.x;
        _gm.y = real_head.y + ((real_head.height - _gm.height) / 2);
        break;
    case RIGHT_EDGE:
        _gm.x = head.x + head.width - _gm.width;
        break;
    case RIGHT_CENTER_EDGE:
        _gm.x = head.x + head.width - _gm.width;
        _gm.y = real_head.y + ((real_head.height - _gm.height) / 2);
        break;
    case CENTER:
        _gm.x = real_head.x + ((real_head.width - _gm.width) / 2);
        _gm.y = real_head.y + ((real_head.height - _gm.height) / 2);
    default:
        // DO NOTHING
        break;
    }

    move(_gm.x, _gm.y);
}

//! @brief Updates all inactive childrens geometry and state
void
Frame::updateInactiveChildInfo(void)
{
    for (auto it : _children) {
        if (it != _client) {
            applyState(static_cast<Client*>(it));
            it->resize(getChildWidth(), getChildHeight());
        }
    }
}

// STATE actions begin

//! @brief Toggles current clients max size
//! @param sa State to set
//! @param horz Include horizontal in (de)maximize
//! @param vert Include vertcical in (de)maximize
//! @param fill Limit size by other frame boundaries ( defaults to false )
void
Frame::setStateMaximized(StateAction sa, bool horz, bool vert, bool fill)
{
    setShaded(STATE_UNSET);
    setStateFullscreen(STATE_UNSET);

    // make sure the two states are in sync if toggling
    if ((horz == vert) && (sa == STATE_TOGGLE)) {
        if (_maximized_horz != _maximized_vert) {
            horz = ! _maximized_horz;
            vert = ! _maximized_vert;
        }
    }

    auto hints = _client->getActiveSizeHints();

    Geometry head;
    pekwm::rootWo()->getHeadInfoWithEdge(X11Util::getNearestHead(*this), head);

    int max_x, max_r, max_y, max_b;
    max_x = head.x;
    max_r = head.width + head.x;
    max_y = head.y;
    max_b = head.height + head.y;

    if (fill) {
        getMaxBounds(max_x, max_r, max_y, max_b);

        // make sure vert and horz gets set if fill is on
        sa = STATE_SET;
    }

    if (horz && (fill || _client->allowMaximizeHorz())) {
        // maximize
        if ((sa == STATE_SET) ||
                ((sa == STATE_TOGGLE) && ! _maximized_horz)) {
            uint h_decor = _gm.width - getChildWidth();

            if (! fill) {
                _old_gm.x = _gm.x;
                _old_gm.width = _gm.width;
            }

            _gm.x = max_x;
            _gm.width = max_r - max_x;

            if ((hints.flags&PMaxSize)
                && (_gm.width > (hints.max_width + h_decor))) {
                _gm.width = hints.max_width + h_decor;
            }
            // demaximize
        } else if ((sa == STATE_UNSET) ||
                   ((sa == STATE_TOGGLE) && _maximized_horz)) {
            _gm.x = _old_gm.x;
            _gm.width = _old_gm.width;
        }

        // we unset the maximized state if we use maxfill
        _maximized_horz = fill ? false : ! _maximized_horz;
        _client->setMaximizedHorz(_maximized_horz);
    }

    if (vert && (fill || _client->allowMaximizeVert())) {
        // maximize
        if ((sa == STATE_SET) ||
                ((sa == STATE_TOGGLE) && ! _maximized_vert)) {
            uint v_decor = _gm.height - getChildHeight();

            if (! fill) {
                _old_gm.y = _gm.y;
                _old_gm.height = _gm.height;
            }

            _gm.y = max_y;
            _gm.height = max_b - max_y;

            if ((hints.flags&PMaxSize) &&
                    (_gm.height > (hints.max_height + v_decor))) {
                _gm.height = hints.max_height + v_decor;
            }
            // demaximize
        } else if ((sa == STATE_UNSET) ||
                   ((sa == STATE_TOGGLE) && _maximized_vert)) {
            _gm.y = _old_gm.y;
            _gm.height = _old_gm.height;
        }

        // we unset the maximized state if we use maxfill
        _maximized_vert = fill ? false : ! _maximized_vert;
        _client->setMaximizedVert(_maximized_vert);
    }

    fixGeometry(); // harbour already considered
    downSize(_gm, true, true); // keep x and keep y ( make conform to inc size )

    moveResize(_gm.x, _gm.y, _gm.width, _gm.height);

    _client->updateEwmhStates();
}

//! @brief Set fullscreen state
void
Frame::setStateFullscreen(StateAction sa)
{
    // Check for DisallowedActions="Fullscreen".
    if (! _client->allowFullscreen()) {
        sa = STATE_UNSET;
    }

    if (! ActionUtil::needToggle(sa, _fullscreen)) {
        return;
    }

    bool lock = _client->setConfigureRequestLock(true);

    if (_fullscreen) {
        if ((_non_fullscreen_decor_state&DECOR_BORDER) != hasBorder()) {
            setBorder(STATE_TOGGLE);
        }
        if ((_non_fullscreen_decor_state&DECOR_TITLEBAR) != hasTitlebar()) {
            setTitlebar(STATE_TOGGLE);
        }
        _gm = _old_gm;

    } else {
        _old_gm = _gm;
        _non_fullscreen_decor_state = _client->getDecorState();
        _non_fullscreen_layer = _client->getLayer();

        setBorder(STATE_UNSET);
        setTitlebar(STATE_UNSET);

        Geometry head;
        uint nr = X11Util::getNearestHead(*this);
        X11::getHeadInfo(nr, head);

        _gm = head;
    }

    _fullscreen = !_fullscreen;
    _client->setFullscreen(_fullscreen);

    moveResize(_gm.x, _gm.y, _gm.width, _gm.height);

    // Re-stack window if fullscreen is above other windows.
    if (pekwm::config()->isFullscreenAbove() && _client->getLayer() != LAYER_DESKTOP) {
        setLayer(_fullscreen ? LAYER_ABOVE_DOCK : _non_fullscreen_layer);
        raise();
    }

    _client->setConfigureRequestLock(lock);
    _client->configureRequestSend();

    _client->updateEwmhStates();
}

void
Frame::setStateSticky(StateAction sa)
{
    // Check for DisallowedActions="Stick".
    if (! _client->allowStick()) {
        sa = STATE_UNSET;
    }

    if (ActionUtil::needToggle(sa, _sticky)) {
        stick();
    }
}

void
Frame::setStateAlwaysOnTop(StateAction sa)
{
    if (! ActionUtil::needToggle(sa, getLayer() >= LAYER_ONTOP)) {
        return;
    }

    _client->alwaysOnTop(getLayer() < LAYER_ONTOP);
    setLayer(_client->getLayer());

    raise();
}

void
Frame::setStateAlwaysBelow(StateAction sa)
{
    if (! ActionUtil::needToggle(sa, getLayer() == LAYER_BELOW)) {
        return;
    }

    _client->alwaysBelow(getLayer() > LAYER_BELOW);
    setLayer(_client->getLayer());

    lower();
}

//! @brief Hides/Shows the border depending on _client
//! @param sa State to set
void
Frame::setStateDecorBorder(StateAction sa)
{
    bool border = hasBorder();

    setBorder(sa);

    // state changed, update client and atom state
    if (border != hasBorder()) {
        _client->setBorder(hasBorder());

        // update the _PEKWM_FRAME_DECOR hint
        X11::setCardinal(_client->getWindow(), PEKWM_FRAME_DECOR,
                         _client->getDecorState());
    }
}

//! @brief Hides/Shows the titlebar depending on _client
//! @param sa State to set
void
Frame::setStateDecorTitlebar(StateAction sa)
{
    bool titlebar = hasTitlebar();

    setTitlebar(sa);

    // state changed, update client and atom state
    if (titlebar != hasTitlebar()) {
        _client->setTitlebar(hasTitlebar());

        X11::setCardinal(_client->getWindow(), PEKWM_FRAME_DECOR,
                         _client->getDecorState());
    }
}

void
Frame::setStateIconified(StateAction sa)
{
    // Check for DisallowedActions="Iconify".
    if (! _client->allowIconify()) {
        sa = STATE_UNSET;
    }

    if (! ActionUtil::needToggle(sa, _iconified)) {
        return;
    }

    if (_iconified) {
        mapWindow();
    } else {
        iconify();
    }
}

//! (Un)Sets the tagged Frame.
//!
//! @param sa Set/Unset or Toggle the state.
//! @param behind Should tagged actions be behind (non-focused).
void
Frame::setStateTagged(StateAction sa, bool behind)
{
    if (ActionUtil::needToggle(sa, (_tag_frame != 0))) {
        _tag_frame = (this == _tag_frame) ? 0 : this;
        _tag_behind = behind;
    }
}

void
Frame::setStateSkip(StateAction sa, uint skip)
{
    if (! ActionUtil::needToggle(sa, _skip&skip)) {
        return;
    }

    if (_skip&skip) {
        _skip &= ~skip;
    } else {
        _skip |= skip;
    }

    setSkip(_skip);
}

//! @brief Sets client title
void
Frame::setStateTitle(StateAction sa, Client *client, const std::wstring &title)
{
    if (sa == STATE_SET) {
        client->getTitle()->setUser(title);

    } else if (sa == STATE_UNSET) {
        client->getTitle()->setUser(L"");
        client->readName();
    } else {
        if (client->getTitle()->isUserSet()) {
            client->getTitle()->setUser(L"");
        } else {
            client->getTitle()->setUser(title);
        }
    }

    // Set PEKWM_TITLE atom to preserve title on client between sessions.
    X11::setString(client->getWindow(), PEKWM_TITLE,
                   Charset::to_mb_str(client->getTitle()->getUser()));

    renderTitle();
}

//! @brief Sets clients marked state.
//! @param sa
//! @param client
void
Frame::setStateMarked(StateAction sa, Client *client)
{
    if (! client || ! ActionUtil::needToggle(sa, client->isMarked())) {
        return;
    }

    // Set marked state and re-render title to update visual marker.
    client->setStateMarked(sa);
    renderTitle();
}

void
Frame::setStateOpaque(StateAction sa)
{
    if (! ActionUtil::needToggle(sa, _opaque)) {
        return;
    }
    _client->setOpaque(!_opaque);
    setOpaque(!_opaque);
}
// STATE actions end

void
Frame::getMaxBounds(int &max_x,int &max_r, int &max_y, int &max_b)
{
    int f_r, f_b;
    int x, y, r, b;

    f_r = getRX();
    f_b = getBY();

    for (auto it : _frames) {
        if (! it->isMapped()) {
            continue;
        }

        x = it->getX();
        y = it->getY();
        r = it->getRX();
        b = it->getBY();

        // update max borders when other frame border lies between
        // this border and prior max border (originally screen/head edge)
        if ((r >= max_x) && (r <= _gm.x) && ! ((y >= f_b) || (b <= _gm.y))) {
            max_x = r;
        }
        if ((x <= max_r) && (x >= f_r) && ! ((y >= f_b) || (b <= _gm.y))) {
            max_r = x;
        }
        if ((b >= max_y) && (b <= _gm.y) && ! ((x >= f_r) || (r <= _gm.x))) {
            max_y = b;
        }
        if ((y <= max_b) && (y >= f_b) && ! ((x >= f_r) || (r <= _gm.x))) {
            max_b = y;
        }
    }
}

void
Frame::setGeometry(const std::string geometry, int head, bool honour_strut)
{
    Geometry gm;
    int mask = X11::parseGeometry(geometry.c_str(), gm);
    if (! mask) {
        return;
    }

    auto screen_gm = X11::getScreenGeometry();
    if (head != -1) {
        if (head == -2) {
            head = X11Util::getNearestHead(*this);
        }

        if (honour_strut) {
            pekwm::rootWo()->getHeadInfoWithEdge(head, screen_gm);
        } else {
            screen_gm = X11::getHeadGeometry(head);
        }
    }

    Geometry applied_gm;
    applyGeometry(applied_gm, gm, mask, screen_gm);
    moveResize(applied_gm, mask);
}

void
Frame::growDirection(uint direction)
{
    Geometry head;
    pekwm::rootWo()->getHeadInfoWithEdge(X11Util::getNearestHead(*this), head);

    switch (direction) {
    case DIRECTION_UP:
        _gm.height = getBY() - head.y;
        _gm.y = head.y;
        break;
    case DIRECTION_DOWN:
        _gm.height = head.y + head.height - _gm.y;
        break;
    case DIRECTION_LEFT:
        _gm.width = getRX() - head.x;
        _gm.x = head.x;
        break;
    case DIRECTION_RIGHT:
        _gm.width = head.x + head.width - _gm.x;
        break;
    default:
        break;
    }

    downSize(_gm, (direction != DIRECTION_LEFT), (direction != DIRECTION_UP));

    moveResize(_gm.x, _gm.y, _gm.width, _gm.height);
}

//! @brief Closes the frame and all clients in it
void
Frame::close(void)
{
    for (auto it : _children) {
        static_cast<Client*>(it)->close();
    }
}

/**
 * Reads autoprops for the active client.
 *
 * @param type Defaults to APPLY_ON_RELOAD
 */
void
Frame::readAutoprops(ApplyOn type)
{
    if ((type != APPLY_ON_RELOAD) && (type != APPLY_ON_WORKSPACE)) {
        return;
    }

    _class_hint->title = _client->getTitle()->getReal();
    auto data =
        pekwm::autoProperties()->findAutoProperty(_class_hint,
                                                  _workspace, type);
    _class_hint->title = L"";

    if (! data) {
        return;
    }

    // Set the correct group of the window
    _class_hint->group = data->group_name;

    if (_class_hint == _client->getClassHint()
        && (_client->isTransient()
            && ! data->isApplyOn(APPLY_ON_TRANSIENT))) {
        return;
    }

    if (data->isMask(AP_STICKY) && _sticky != data->sticky) {
        stick();
    }
    if (data->isMask(AP_SHADED) && (isShaded() != data->shaded)) {
        setShaded(STATE_UNSET);
    }
    if (data->isMask(AP_MAXIMIZED_HORIZONTAL)
        && (_maximized_horz != data->maximized_horizontal)) {
        setStateMaximized(STATE_TOGGLE, true, false, false);
    }
    if (data->isMask(AP_MAXIMIZED_VERTICAL)
        && (_maximized_vert != data->maximized_vertical)) {
        setStateMaximized(STATE_TOGGLE, false, true, false);
    }
    if (data->isMask(AP_FULLSCREEN) && (_fullscreen != data->fullscreen)) {
        setStateFullscreen(STATE_TOGGLE);
    }

    if (data->isMask(AP_ICONIFIED) && (_iconified != data->iconified)) {
        if (_iconified) {
            mapWindow();
        } else {
            iconify();
        }
    }
    if (data->isMask(AP_WORKSPACE)) {
        // In order to avoid eternal recursion, the workspace is only set here
        // and then actually called PDecor::setWorkspace from Frame::setWorkspace
        if (type == APPLY_ON_WORKSPACE) {
            _workspace = data->workspace;
        }	else if (_workspace != data->workspace) {
            // Call PDecor directly to avoid recursion.
            PDecor::setWorkspace(data->workspace);
        }
    }

    if (data->isMask(AP_SHADED) && (isShaded() != data->shaded)) {
        setShaded(STATE_TOGGLE);
    }
    if (data->isMask(AP_LAYER) && (data->layer <= LAYER_MENU)) {
        _client->setLayer(data->layer);
        raise(); // restack the frame
    }

    if (data->isMask(AP_FRAME_GEOMETRY|AP_CLIENT_GEOMETRY)) {
        setupAPGeometry(_client, data);

        // Apply changes
        moveResize(_gm.x, _gm.y, _gm.width, _gm.height);
    }

    if (data->isMask(AP_BORDER) && (hasBorder() != data->border)) {
        setStateDecorBorder(STATE_TOGGLE);
    }
    if (data->isMask(AP_TITLEBAR) && (hasTitlebar() != data->titlebar)) {
        setStateDecorTitlebar(STATE_TOGGLE);
    }

    if (data->isMask(AP_SKIP)) {
        _client->setSkip(data->skip);
        setSkip(_client->getSkip());
    }

    if (data->isMask(AP_FOCUSABLE)) {
        _client->setFocusable(data->focusable);
    }
}

/**
 * Return how large the frame is in cells.
 */
void
Frame::calcSizeInCells(uint &width, uint &height, const Geometry& gm)
{
    auto hints = _client->getActiveSizeHints();

    if (hints.flags&PResizeInc) {
        int c_width = gm.width - bdLeft(this) - bdRight(this);
        if (c_width < 1) {
            c_width = 1;
        }
        int c_height = gm.height - decorHeight(this);
        if (c_height < 1) {
            c_height = 1;
        }
        width = (c_width - hints.base_width) / hints.width_inc;
        height = (c_height - hints.base_height) / hints.height_inc;
    } else {
        width = gm.width;
        height = gm.height;
    }
}

void
Frame::setGravityPosition(int gravity, int &x, int &y, int w, int h)
{
    switch (gravity) {
    case NorthWestGravity:
        break;
    case NorthGravity:
        x = x - w/2;
        break;
    case NorthEastGravity:
        x = x - w;
        break;
    case WestGravity:
        y = y - h/2;
        break;
    case CenterGravity:
        x = x - w/2;
        y = y - h/2;
        break;
    case EastGravity:
        x = x - w;
        y = y - h/2;
        break;
    case SouthWestGravity:
        y = y - h;
        break;
    case SouthGravity:
        x = x - w/2;
        y = y - h;
        break;
    case SouthEastGravity:
        x = x - w;
        y = y - h;
        break;
    case StaticGravity:
        // FIXME: What should we do here?
        break;
    default:
        break;
    }
}
/**
 * Makes gm conform to _clients width and height inc.
 */
void
Frame::downSize(Geometry &gm, bool keep_x, bool keep_y)
{
    auto hints = _client->getActiveSizeHints();

    // conform to width_inc
    if (hints.flags&PResizeInc) {
        int o_r = getRX();
        int b_x = (hints.flags&PBaseSize)
                  ? hints.base_width
                  : (hints.flags&PMinSize) ? hints.min_width : 0;

        gm.width -= (getChildWidth() - b_x) % hints.width_inc;
        if (! keep_x) {
            gm.x = o_r - gm.width;
        }
    }

    // conform to height_inc
    if (hints.flags&PResizeInc) {
        int o_b = getBY();
        int b_y = (hints.flags&PBaseSize)
                  ? hints.base_height
                  : (hints.flags&PMinSize) ? hints.min_height : 0;

        gm.height -= (getChildHeight() - b_y) % hints.height_inc;
        if (! keep_y) {
            gm.y = o_b - gm.height;
        }
    }
}

// Below this Client message handling is done

//! @brief Handle XConfgiureRequestEvents
void
Frame::handleConfigureRequest(XConfigureRequestEvent *ev, Client *client)
{
    if (client != _client) {
        _client->configureRequestSend();
        return; // only handle the active client's events
    }

    // Update the stacking (ev->value_mask&CWSibling should not happen)
    if (! client->isCfgDeny(CFG_DENY_STACKING) && ev->value_mask&CWStackMode) {
        if (ev->detail == Above) {
            raise();
        } else if (ev->detail == Below) {
            lower();
        } // ignore TopIf, BottomIf, Opposite - PekWM is a reparenting WM
    }

    // Update the geometry if requested
    bool chg_size = ! _client->isCfgDeny(CFG_DENY_SIZE) && (ev->value_mask&(CWWidth|CWHeight));
    bool chg_pos  = ! _client->isCfgDeny(CFG_DENY_POSITION) && (ev->value_mask&(CWX|CWY));
    if (! (chg_size || chg_pos)) {
        _client->configureRequestSend();
        return;
    }

    if (pekwm::config()->isFullscreenDetect()
          && (ev->value_mask&(CWX|CWY|CWWidth|CWHeight)) == (CWX|CWY|CWWidth|CWHeight)
          && isRequestGeometryFullscreen(ev)) {
        if (isFullscreen()) {
            _client->configureRequestSend();
        } else {
            setStateFullscreen(STATE_SET);
        }
        return;
    }

    Geometry gm = _gm;

    if (chg_size) {
        int diff_w = ev->width - gm.width + decorWidth(this);
        gm.width += diff_w;
        int diff_h = ev->height - gm.height + decorHeight(this);
        gm.height += diff_h;

        if (!chg_pos) {
            setGravityPosition(_client->getActiveSizeHints().win_gravity,
                               gm.x, gm.y, diff_w, diff_h);
        }
    }

    if (chg_pos) {
        gm.x = ev->x;
        gm.y = ev->y;
    }

    X11::keepVisible(gm);

    if (pekwm::config()->isFullscreenDetect() && isFullscreen()) {
        _old_gm = gm;
        setStateFullscreen(STATE_UNSET);
        return;
    }

    moveResize(gm.x, gm.y, gm.width, gm.height);
}

/**
 * Check if requested size if "fullscreen"
 */
bool
Frame::isRequestGeometryFullscreen(XConfigureRequestEvent *ev)
{
    if (_client->isCfgDeny(CFG_DENY_SIZE) || _client->isCfgDeny(CFG_DENY_POSITION)
          || ! _client->allowFullscreen()) {
        return false;
    }

    int nearest_head = X11::getNearestHead(ev->x, ev->y);
    Geometry gm_request(ev->x, ev->y, ev->width, ev->height);
    Geometry gm_screen(X11::getScreenGeometry());
    Geometry gm_head(X11::getHeadGeometry(nearest_head));

    bool is_fullscreen = false;
    if (gm_request == gm_screen || gm_request == gm_head) {
        is_fullscreen = true;
    } else {
        downSize(gm_screen, true, true);
        downSize(gm_head, true, true);
        if (gm_request == gm_screen || gm_request == gm_head) {
            is_fullscreen = true;
        }
    }
    return is_fullscreen;
}

/**
 * Handle client message.
 */
ActionEvent*
Frame::handleClientMessage(XClientMessageEvent *ev, Client *client)
{
    ActionEvent *ae = nullptr;

    if (ev->message_type == X11::getAtom(STATE)) {
        handleClientStateMessage(ev, client);
    } else if (ev->message_type == X11::getAtom(NET_ACTIVE_WINDOW)) {
        if (! client->isCfgDeny(CFG_DENY_ACTIVE_WINDOW)) {
            // Active child if it's not the active child
            if (client != _client) {
                activateChild(client);
            }

            // If we aren't mapped we check if we make sure we're on the right
            // workspace and then map the window.
            if (! _mapped) {
                if (_workspace != Workspaces::getActive() &&
                        !isSticky()) {
                    Workspaces::setWorkspace(_workspace, false);
                }
                mapWindow();
            }
            // Seems as if raising the window is implied in activating it
            raise();
            giveInputFocus();
        }
    } else if (ev->message_type == X11::getAtom(NET_CLOSE_WINDOW)) {
        client->close();
    } else if (ev->message_type == X11::getAtom(NET_WM_DESKTOP)) {
        if (client == _client) {
            setWorkspace(ev->data.l[0]);
        }
    } else if (ev->message_type == X11::getAtom(WM_CHANGE_STATE) &&
               (ev->format == 32) && (ev->data.l[0] == IconicState)) {
        if (client == _client) {
            iconify();
        }
    } else if (ev->message_type == X11::getAtom(NET_WM_MOVERESIZE)
               && ev->format == 32) {
        switch (ev->data.l[2]) {
        case NET_WM_MOVERESIZE_SIZE_TOPLEFT:
            doResize(true, true, true, true);
            break;
        case NET_WM_MOVERESIZE_SIZE_TOP:
            doResize(false, false, true, true);
            break;
        case NET_WM_MOVERESIZE_SIZE_TOPRIGHT:
            doResize(false, true, true, true);
            break;
        case NET_WM_MOVERESIZE_SIZE_RIGHT:
            doResize(false, true, false, false);
            break;
        case NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT:
            doResize(false, true, false, true);
            break;
        case NET_WM_MOVERESIZE_SIZE_BOTTOM:
            doResize(false, false, false, true);
            break;
        case NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT:
            doResize(true, true, false, true);
            break;
        case NET_WM_MOVERESIZE_SIZE_LEFT:
            doResize(true, true, false, false);
            break;
        case NET_WM_MOVERESIZE_MOVE:
            ae = &_ae_move;
            ae->action_list[0].setParamI(ev->data.l[0], ev->data.l[1]);
            break;
        case NET_WM_MOVERESIZE_SIZE_KEYBOARD:
        case NET_WM_MOVERESIZE_MOVE_KEYBOARD:
            ae = &_ae_move_resize;
            break;
        }
    }
    return ae;
}

/**
 * Handle _NET_WM_STATE atom.
 */
void
Frame::handleClientStateMessage(XClientMessageEvent *ev, Client *client)
{
    StateAction sa = getStateActionFromMessage(ev);
    handleStateAtom(sa, ev->data.l[1], client);
    if (ev->data.l[2] != 0) {
        handleStateAtom(sa, ev->data.l[2], client);
    }
    client->updateEwmhStates();
}

/**
 * Get StateAction from NET_WM atom.
 */
StateAction
Frame::getStateActionFromMessage(XClientMessageEvent *ev)
{
    StateAction sa = STATE_SET;
    if (ev->data.l[0]== NET_WM_STATE_REMOVE) {
        sa = STATE_UNSET;
    } else if (ev->data.l[0]== NET_WM_STATE_ADD) {
        sa = STATE_SET;
    } else if (ev->data.l[0]== NET_WM_STATE_TOGGLE) {
        sa = STATE_TOGGLE;
    }
    return sa;
}

/**
 * Handle state atom for client.
 */
void
Frame::handleStateAtom(StateAction sa, Atom atom, Client *client)
{
    if (client == _client) {
        handleCurrentClientStateAtom(sa, atom, client);
    }

    switch (atom) {
    case STATE_SKIP_TASKBAR:
        client->setStateSkip(sa, SKIP_TASKBAR);
        break;
    case STATE_SKIP_PAGER:
        client->setStateSkip(sa, SKIP_PAGER);
        break;
    case STATE_DEMANDS_ATTENTION:
        bool ostate = client->demandsAttention();
        bool nstate = (sa == STATE_SET || (sa == STATE_TOGGLE && ! ostate));
        client->setDemandsAttention(nstate);
        if (ostate != nstate) {
            if (ostate) {
                decrAttention();
            } else {
                incrAttention();
            }
        }
        break;
    }
}

/**
 * Handle state atom for actions that apply only on active client.
 */
void
Frame::handleCurrentClientStateAtom(StateAction sa, Atom atom, Client *client)
{
    if (atom == X11::getAtom(STATE_STICKY)) {
        setStateSticky(sa);
    }
    if (atom == X11::getAtom(STATE_MAXIMIZED_HORZ)
            && ! client->isCfgDeny(CFG_DENY_STATE_MAXIMIZED_HORZ)) {
        setStateMaximized(sa, true, false, false);
    }
    if (atom == X11::getAtom(STATE_MAXIMIZED_VERT)
            && ! client->isCfgDeny(CFG_DENY_STATE_MAXIMIZED_VERT)) {
        setStateMaximized(sa, false, true, false);
    }
    if (atom == X11::getAtom(STATE_SHADED)) {
        setShaded(sa);
    }
    if (atom == X11::getAtom(STATE_HIDDEN)
            && ! client->isCfgDeny(CFG_DENY_STATE_HIDDEN)) {
        setStateIconified(sa);
    }
    if (atom == X11::getAtom(STATE_FULLSCREEN)
            && ! client->isCfgDeny(CFG_DENY_STATE_FULLSCREEN)) {
        setStateFullscreen(sa);
    }
    if (atom == X11::getAtom(STATE_ABOVE)
            && ! client->isCfgDeny(CFG_DENY_STATE_ABOVE)) {
        setStateAlwaysOnTop(sa);
    }
    if (atom == X11::getAtom(STATE_BELOW)
            && ! client->isCfgDeny(CFG_DENY_STATE_BELOW)) {
        setStateAlwaysBelow(sa);
    }
}

void
Frame::handlePropertyChange(XPropertyEvent *ev, Client *client)
{
    if (ev->atom == X11::getAtom(NET_WM_DESKTOP)) {
        if (client == _client) {
            Cardinal workspace;
            if (X11::getCardinal(client->getWindow(),
                                 NET_WM_DESKTOP, workspace)) {
                if (workspace != signed(_workspace))
                    setWorkspace(workspace);
            }
        }
    } else if (ev->atom == X11::getAtom(NET_WM_STRUT)) {
        client->getStrutHint();
    } else if (ev->atom == X11::getAtom(NET_WM_NAME)
               || ev->atom == XA_WM_NAME) {
        handleTitleChange(client, true);
    } else if (ev->atom == X11::getAtom(MOTIF_WM_HINTS)) {
        client->readMwmHints();
        if (! isFullscreen() && _client == client) {
            setBorder(_client->hasBorder()?STATE_SET:STATE_UNSET);
            setTitlebar(_client->hasTitlebar()?STATE_SET:STATE_UNSET);
        }
    } else if (ev->atom == XA_WM_NORMAL_HINTS) {
        client->getWMNormalHints();
    } else if (ev->atom == XA_WM_TRANSIENT_FOR) {
        client->getTransientForHint();
    } else if (ev->atom == X11::getAtom(WM_HINTS)) {
        bool ostate = client->demandsAttention();
        client->getWMHints();
        bool nstate = client->demandsAttention();
        if (ostate != nstate) {
            if (ostate) {
                decrAttention();
            } else {
                incrAttention();
            }
        }
    } else if (ev->atom == X11::getAtom(WM_PROTOCOLS)) {
      client->getWMProtocols();
    }
}

/**
 * Handle title change, find decoration rules based on changed title
 * and update if changed.
 */
void
Frame::handleTitleChange(Client *client, bool read_name)
{
    // Update title
    if (read_name) {
        client->readName();
    }

    if (client != _client || ! updateDecor()) {
        // Render title as either the title changed was not the active
        // title or the name change did not cause the decor to change.
        renderTitle();
    }
}

/**
 * Insert Frame into Workspaces and ensure client list is updated.
 */
void
Frame::workspacesInsert()
{
    Workspaces::insert(this);
    Workspaces::updateClientList();
}

/**
 * Remove Frame from Workspaces and ensure client list is updated.
 */
void
Frame::workspacesRemove()
{
    Workspaces::remove(this);
    Workspaces::updateClientList();
}
