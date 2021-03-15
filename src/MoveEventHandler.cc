//
// MoveEvenHandler.cc for pekwm
// Copyright (C) 2021 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "Debug.hh"
#include "ActionHandler.hh"
#include "MoveEventHandler.hh"

MoveEventHandler::MoveEventHandler(Config* cfg, PDecor* decor,
                                   int x_root, int y_root)
    : _cfg(cfg),
      _outline(!cfg->getOpaqueMove()),
      _show_status_window(cfg->isShowStatusWindow()),
      _center_on_root(cfg->isShowStatusWindowOnRoot()),
      _curr_edge(SCREEN_EDGE_NO),
      _init(false),
      _decor(decor)
{
    decor->getGeometry(_gm);
    decor->getGeometry(_last_gm);
    _x = x_root - _gm.x;
    _y = y_root - _gm.y;

    decor->addObserver(this);
}
MoveEventHandler::~MoveEventHandler(void)
{
    if (_decor) {
        _decor->removeObserver(this);
    }
    stopMove();
}

void
MoveEventHandler::notify(Observable *observable,
                         Observation *observation)
{
    if (observation == &PWinObj::pwin_obj_deleted
        && observable == _decor) {
        TRACE("decor " << _decor << " lost while moving");
        _decor = nullptr;
    }
}

bool
MoveEventHandler::initEventHandler(void)
{
    if (! _decor
        || ! _decor->allowMove()
        || ! X11::grabPointer(X11::getRoot(),
                              ButtonMotionMask|ButtonReleaseMask,
                              CURSOR_MOVE)) {
        return false;
    }

    // Grab server to avoid any events causing garbage on the
    // screen as the outline is drawed as an inverted rectangle.
    if (_outline) {
        X11::grabServer();
        drawOutline();
    }
    updateStatusWindow(true);

    _init = true;
    return true;
}

EventHandler::Result
MoveEventHandler::handleButtonPressEvent(XButtonEvent*)
{
    // mark as processed disabling wm processing of these events.
    return EventHandler::EVENT_PROCESSED;
}

EventHandler::Result
MoveEventHandler::handleButtonReleaseEvent(XButtonEvent*)
{
    stopMove();

    if (_decor) {
        _decor->move(_gm.x, _gm.y);
    }

    return EventHandler::EVENT_STOP_PROCESSED;
}

EventHandler::Result
MoveEventHandler::handleKeyEvent(XKeyEvent*)
{
    // mark as processed disabling wm processing of these events.
    return EventHandler::EVENT_PROCESSED;
}

EventHandler::Result
MoveEventHandler::handleMotionNotifyEvent(XMotionEvent *ev)
{
    if (! _decor) {
        return stopMove();
    }

    drawOutline(); //clear

    // Flush all pointer motion, no need to redraw and redraw.
    X11::removeMotionEvents();

    _gm.x = ev->x_root - _x;
    _gm.y = ev->y_root - _y;
    PDecor::checkSnap(_decor, _gm);

    if (! _outline && _gm != _last_gm) {
        _last_gm = _gm;
        X11::moveWindow(_decor->getWindow(), _gm.x, _gm.y);
    }

    auto edge = doMoveEdgeFind(ev->x_root, ev->y_root);
    if (edge != _curr_edge) {
        _curr_edge = edge;
        if (edge != SCREEN_EDGE_NO) {
            doMoveEdgeAction(ev, edge);
        }
    }

    drawOutline();
    updateStatusWindow(false);

    return EventHandler::EVENT_PROCESSED;
}

EventHandler::Result
MoveEventHandler::stopMove(void)
{
    if (_init) {
        if (_show_status_window) {
            pekwm::statusWindow()->unmapWindow();
        }
        if (_outline) {
            drawOutline(); // clear
            X11::ungrabServer(true);
        }
        X11::ungrabPointer();
        _init = false;
    }
    return EventHandler::EVENT_STOP_SKIP;
}

void
MoveEventHandler::drawOutline(void)
{
    if (_outline) {
        _decor->drawOutline(_gm);
    }
}

void
MoveEventHandler::updateStatusWindow(bool map)
{
    if (_show_status_window) {
        wchar_t buf[128];
        _decor->getDecorInfo(buf, 128, _gm);

        auto sw = pekwm::statusWindow();
        if (map) {
            // draw before map to avoid resize right after the
            // window is mapped.
            sw->draw(buf, true, _center_on_root ? 0 : &_gm);
            sw->mapWindowRaised();
        }
        sw->draw(buf, true, _center_on_root ? 0 : &_gm);
    }
}

EdgeType
MoveEventHandler::doMoveEdgeFind(int x, int y)
{
    auto edge = SCREEN_EDGE_NO;
    if (x <= signed(pekwm::config()->getScreenEdgeSize(SCREEN_EDGE_LEFT))) {
        edge = SCREEN_EDGE_LEFT;
    } else if (x >= signed(X11::getWidth() -
                           _cfg->getScreenEdgeSize(SCREEN_EDGE_RIGHT))) {
        edge = SCREEN_EDGE_RIGHT;
    } else if (y <= signed(_cfg->getScreenEdgeSize(SCREEN_EDGE_TOP))) {
        edge = SCREEN_EDGE_TOP;
    } else if (y >= signed(X11::getHeight() -
                           _cfg->getScreenEdgeSize(SCREEN_EDGE_BOTTOM))) {
        edge = SCREEN_EDGE_BOTTOM;
    }
    return edge;
}

void
MoveEventHandler::doMoveEdgeAction(XMotionEvent *ev, EdgeType edge)
{
    uint button = X11::getButtonFromState(ev->state);
    auto ae =
        ActionHandler::findMouseAction(button, ev->state,
                                       MOUSE_EVENT_ENTER_MOVING,
                                       _cfg->getEdgeListFromPosition(edge));
    if (ae) {
        ActionPerformed ap(_decor, *ae);
        ap.type = ev->type;
        ap.event.motion = ev;

        pekwm::actionHandler()->handleAction(ap);
    }
}
