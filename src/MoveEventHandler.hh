//
// MoveEvenHandler.hh for pekwm
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
#include "StatusWindow.hh"

class MoveEventHandler : public EventHandler,
                         public Observer
{
public:
    MoveEventHandler(Config* cfg, PDecor* decor, int x_root, int y_root);
    virtual ~MoveEventHandler(void);

    virtual void notify(Observable *observable,
                        Observation *observation) override;
    virtual bool initEventHandler(void) override;

    virtual EventHandler::Result
    handleButtonPressEvent(XButtonEvent*) override;
    virtual EventHandler::Result
    handleButtonReleaseEvent(XButtonEvent*) override;
    virtual EventHandler::Result
    handleKeyEvent(XKeyEvent*) override;
    virtual EventHandler::Result
    handleMotionNotifyEvent(XMotionEvent *ev) override;

private:
    EventHandler::Result stopMove(void);
    void drawOutline(void);
    void updateStatusWindow(bool map);
    EdgeType doMoveEdgeFind(int x, int y);
    void doMoveEdgeAction(XMotionEvent *ev, EdgeType edge);

private:
    Config *_cfg;

    bool _outline;
    bool _show_status_window;
    bool _center_on_root;
    Geometry _gm;
    Geometry _last_gm;
    EdgeType _curr_edge;

    int _x;
    int _y;

    bool _init;
    PDecor *_decor;
};
