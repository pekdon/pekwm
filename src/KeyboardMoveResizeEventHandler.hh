//
// KeyboardMoveResizeEventHandler.hh for pekwm
// Copyright (C) 2021 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#pragma once

#include "Action.hh"
#include "Config.hh"
#include "EventHandler.hh"
#include "Observable.hh"
#include "KeyGrabber.hh"
#include "StatusWindow.hh"

class KeyboardMoveResizeEventHandler : public EventHandler,
                                       public Observer
{
public:
    KeyboardMoveResizeEventHandler(Config* cfg, KeyGrabber *key_grabber,
                                   PDecor* decor);
    virtual ~KeyboardMoveResizeEventHandler(void);

    virtual void notify(Observable *observable,
                        Observation *observation) override;

    virtual bool initEventHandler(void) override;

    virtual EventHandler::Result
    handleButtonPressEvent(XButtonEvent*) override;

    virtual EventHandler::Result
    handleButtonReleaseEvent(XButtonEvent*) override;

    virtual EventHandler::Result
    handleMotionNotifyEvent(XMotionEvent*) override;

    virtual EventHandler::Result
    handleKeyEvent(XKeyEvent *ev) override;

    EventHandler::Result
    runMoveResizeAction(const Action& action);

    void drawOutline(void);

    void updateStatusWindow(bool map);

    EventHandler::Result stopMoveResize(void);

private:
    KeyGrabber *_key_grabber;
    bool _outline;
    bool _show_status_window;
    bool _center_on_root;

    Geometry _gm;
    Geometry _old_gm;

    bool _init;
    PDecor *_decor;
};
