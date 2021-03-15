//
// KeyboardMoveResizeEventHandler.hh for pekwm
// Copyright (C) 2021 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "Debug.hh"
#include "KeyboardMoveResizeEventHandler.hh"
#include "PDecor.hh"

KeyboardMoveResizeEventHandler::KeyboardMoveResizeEventHandler(Config* cfg,
                                                               KeyGrabber *kg,
                                                               PDecor* decor)
    : _key_grabber(kg),
      _outline(! cfg->getOpaqueMove() || ! cfg->getOpaqueResize()),
      _show_status_window(cfg->isShowStatusWindow()),
      _center_on_root(cfg->isShowStatusWindowOnRoot()),
      _init(false),
      _decor(decor)
{
    decor->getGeometry(_gm);
    decor->getGeometry(_old_gm);
    decor->addObserver(this);
}

KeyboardMoveResizeEventHandler::~KeyboardMoveResizeEventHandler(void)
{
    if (_decor) {
        _decor->removeObserver(this);
    }
    stopMoveResize();
}

void
KeyboardMoveResizeEventHandler::notify(Observable *observable,
                                       Observation *observation)
{
    if (observation == &PWinObj::pwin_obj_deleted
        && observable == _decor) {
        TRACE("decor" << _decor << " lost while keyboard move/resize");
        _decor = nullptr;
    }
}

bool
KeyboardMoveResizeEventHandler::initEventHandler(void)
{
    if (! X11::grabPointer(X11::getRoot(), NoEventMask, CURSOR_MOVE)) {
        return false;
    }
    if (! X11::grabKeyboard(X11::getRoot())) {
        X11::ungrabPointer();
        return false;
    }

    if (_outline) {
        X11::grabServer();
        drawOutline(); // draw for initial clear
    }
    updateStatusWindow(true);

    _init = true;
    return true;
}

EventHandler::Result
KeyboardMoveResizeEventHandler::handleButtonPressEvent(XButtonEvent*)
{
    // mark as processed disabling wm processing of these events.
    return EventHandler::EVENT_PROCESSED;
}

EventHandler::Result
KeyboardMoveResizeEventHandler::handleButtonReleaseEvent(XButtonEvent*)
{
    // mark as processed disabling wm processing of these events.
    return EventHandler::EVENT_PROCESSED;
}

EventHandler::Result
KeyboardMoveResizeEventHandler::handleMotionNotifyEvent(XMotionEvent*)
{
    // mark as processed disabling wm processing of these events.
    return EventHandler::EVENT_PROCESSED;
}

EventHandler::Result
KeyboardMoveResizeEventHandler::handleKeyEvent(XKeyEvent *ev)
{
    if (! _decor) {
        return stopMoveResize();
    } else if (ev->type == KeyRelease) {
        return EventHandler::EVENT_PROCESSED;
    }

    auto res = EventHandler::EVENT_PROCESSED;
    auto ae = _key_grabber->findMoveResizeAction(ev);
    if (ae == nullptr) {
        return res;
    }

    drawOutline(); // clear previous draw

    for (auto it : ae->action_list) {
        res = runMoveResizeAction(it);
        if (res == EventHandler::EVENT_STOP_PROCESSED) {
            break;
        }
    }

    drawOutline();
    updateStatusWindow(false);

    if (res == EventHandler::EVENT_STOP_PROCESSED
        || res == EventHandler::EVENT_STOP_SKIP) {
        stopMoveResize();
    }

    return res;
}

EventHandler::Result
KeyboardMoveResizeEventHandler::runMoveResizeAction(const Action& action)
{
    int gm_mask = 0;

    switch (action.getAction()) {
    case MOVE_HORIZONTAL:
        if (action.getParamI(1) == UNIT_PERCENT) {
            Geometry head;
            X11::getHeadInfo(X11Util::getNearestHead(*_decor), head);
            _gm.x += (action.getParamI(0)
                      * static_cast<int>(head.width)) / 100;
        } else {
            _gm.x += action.getParamI(0);
        }
        gm_mask = X_VALUE;
        break;
    case MOVE_VERTICAL:
        if (action.getParamI(1) == UNIT_PERCENT) {
            Geometry head;
            X11::getHeadInfo(X11Util::getNearestHead(*_decor), head);
            _gm.y += (action.getParamI(0)
                      * static_cast<int>(head.height)) / 100;
        } else {
            _gm.y +=  action.getParamI(0);
        }
        gm_mask = Y_VALUE;
        break;
    case RESIZE_HORIZONTAL:
        if (action.getParamI(1) == UNIT_PERCENT) {
            Geometry head;
            X11::getHeadInfo(X11Util::getNearestHead(*_decor), head);
            _gm.width += (action.getParamI(0)
                          * static_cast<int>(head.width)) / 100;
        } else {
            _gm.width +=  action.getParamI(0);
        }
        gm_mask = WIDTH_VALUE;
        break;
    case RESIZE_VERTICAL:
        if (action.getParamI(1) == UNIT_PERCENT) {
            Geometry head;
            X11::getHeadInfo(X11Util::getNearestHead(*_decor), head);
            _gm.height += (action.getParamI(0)
                           * static_cast<int>(head.height)) / 100;
        } else {
            _gm.height +=  action.getParamI(0);
        }
        gm_mask = HEIGHT_VALUE;
        break;
    case MOVE_SNAP:
        PDecor::checkSnap(_decor, _gm);
        gm_mask = X_VALUE | Y_VALUE;
        break;
    case MOVE_CANCEL:
        gm_mask = _old_gm.diffMask(_gm);
        _gm = _old_gm; // restore position

        if (! _outline) {
            _decor->moveResize(_old_gm, gm_mask);
        }

        return EventHandler::EVENT_STOP_PROCESSED;
    case MOVE_END:
        if (_outline) {
            gm_mask = _gm.diffMask(_old_gm);
            _decor->moveResize(_gm, gm_mask);
        }
        return EventHandler::EVENT_STOP_PROCESSED;
    default:
        break;
    }

    if (! _outline) {
        _decor->moveResize(_gm, gm_mask);
    }
    return EventHandler::EVENT_PROCESSED;
}

void
KeyboardMoveResizeEventHandler::drawOutline(void)
{
    if (_outline) {
        _decor->drawOutline(_gm);
    }
}

void
KeyboardMoveResizeEventHandler::updateStatusWindow(bool map)
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

EventHandler::Result
KeyboardMoveResizeEventHandler::stopMoveResize(void)
{
    if (_init) {
        if (_show_status_window) {
            pekwm::statusWindow()->unmapWindow();
        }
        if (_outline) {
            drawOutline();
            X11::ungrabServer(true);
        }
        X11::ungrabPointer();
        X11::ungrabKeyboard();
        _init = false;
    }
    return EventHandler::EVENT_STOP_SKIP;
}
