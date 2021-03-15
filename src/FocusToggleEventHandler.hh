//
// FocusToggleEventHandler.hh for pekwm
// Copyright (C) 2021 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#pragma once

#include "Action.hh"
#include "Config.hh"
#include "EventHandler.hh"
#include "Frame.hh"
#include "Observable.hh"
#include "PMenu.hh"

class FocusToggleEventHandler : public EventHandler,
                                public Observer
{
public:
    FocusToggleEventHandler(Config* cfg, uint button, uint raise, int off,
                            bool show_iconified, bool mru);
    virtual ~FocusToggleEventHandler(void);

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

private:

    EventHandler::Result stop(void);
    void setFocusedWo(PWinObj *fo_wo);
    PMenu* createNextPrevMenu(void);
    bool createMenuInclude(Frame *frame, bool show_iconified);

private:
    Config *_cfg;
    uint _button;
    uint _raise;
    int _off;
    bool _show_iconified;
    bool _mru;

    PMenu *_menu;

    PWinObj *_fo_wo;
    bool _was_iconified;
};
