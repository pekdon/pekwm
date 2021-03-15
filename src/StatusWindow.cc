//
// StatusWindow.cc for pekwm
// Copyright (C) 2004-2016 the pekwm development team
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "config.h"

#include "Debug.hh"
#include "PWinObj.hh"
#include "StatusWindow.hh"
#include "X11.hh"
#include "PTexture.hh"
#include "Theme.hh"
#include "Workspaces.hh"
#include "X11Util.hh"

#include <algorithm>

//! @brief StatusWindow constructor
StatusWindow::StatusWindow(Theme* theme)
    : PDecor("STATUSWINDOW"),
      _theme(theme),
      _status_wo(new PWinObj(false))
{
    // PWinObj attributes
    _type = PWinObj::WO_STATUS;
    setLayer(LAYER_NONE); // hack, goes over LAYER_MENU
    _hidden = true; // don't care about it when changing worskpace etc

    XSetWindowAttributes attr;
    attr.event_mask = None;

    _status_wo->setWindow(X11::createWindow(_window, 0, 0, 1, 1, 0,
                                            CopyFromParent, CopyFromParent,
                                            CopyFromParent,
                                            CWEventMask, &attr));

    addChild(_status_wo);
    activateChild(_status_wo);
    _status_wo->mapWindow();

    setTitlebar(STATE_UNSET);
    setFocused(true); // always draw as focused

    Workspaces::insert(this);
}

//! @brief StatusWindow destructor
StatusWindow::~StatusWindow(void)
{
    Workspaces::remove(this);

    // remove ourself from the decor manually, no need to reparent and stuff
    _children.erase(std::remove(_children.begin(), _children.end(),
                                _status_wo),
                    _children.end());

    // free resources
    X11::destroyWindow(_status_wo->getWindow());
    delete _status_wo;

    unloadTheme();
}

//! @brief Resizes window to fit the text and renders text
//! @param text Text to draw in StatusWindow
//! @param do_center Center the StatusWindow on screen. Defaults to false.
void
StatusWindow::draw(const std::wstring &text, bool do_center, Geometry *gm)
{
    uint width, height;
    auto sd = _theme->getStatusData();
    PFont *font = sd->getFont();

    width = font->getWidth(text.c_str()) + 10;
    width = width - (width % 10);
    height = font->getHeight()
             + sd->getPad(PAD_UP)
             + sd->getPad(PAD_DOWN);

    if ((width != getChildWidth()) || (height != getChildHeight())) {
        resizeChild(width, height);
        render();
    }

    if (do_center) {
        Geometry head;
        if (! gm) {
            auto chs = pekwm::config()->getCurrHeadSelector();
            X11::getHeadInfo(X11Util::getCurrHead(chs), head);
            gm = &head;
        }

        move(gm->x + (gm->width - _gm.width) / 2,
             gm->y + (gm->height - _gm.height) / 2);
    }

    font->setColor(sd->getColor());
    X11::clearWindow(_status_wo->getWindow());
    font->draw(_status_wo->getWindow(),
               (width - font->getWidth(text.c_str())) / 2,
               sd->getPad(PAD_UP),
               text.c_str());
}

//! @brief Renders and sets background
void
StatusWindow::loadTheme(void)
{
    render();
}

//! @brief Frees theme resources
void
StatusWindow::unloadTheme(void)
{
}

//! @brief Renders and sets background
void
StatusWindow::render(void)
{
    auto tex = _theme->getStatusData()->getTexture();
    tex->setBackground(_status_wo->getWindow(),
                       0, 0, _status_wo->getWidth(), _status_wo->getHeight());
    X11::clearWindow(_status_wo->getWindow());
}
