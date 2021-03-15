//
// FocusToggleEventHandler.cc for pekwm
// Copyright (C) 2021 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "Debug.hh"
#include "FocusToggleEventHandler.hh"
#include "Workspaces.hh"

FocusToggleEventHandler::FocusToggleEventHandler(Config* cfg, uint button,
                                                 uint raise, int off,
                                                 bool show_iconified, bool mru)
    : _cfg(cfg),
      _button(button),
      _raise(raise),
      _off(off),
      _show_iconified(show_iconified),
      _mru(mru),
      _menu(nullptr),
      _fo_wo(nullptr),
      _was_iconified(false)
{
}

FocusToggleEventHandler::~FocusToggleEventHandler(void)
{
    setFocusedWo(nullptr);
    delete _menu;
}

void
FocusToggleEventHandler::notify(Observable *observable,
                        Observation *observation)
    {
        if (observation == &PWinObj::pwin_obj_deleted
            && observable == _fo_wo) {
            TRACE("decor " << _fo_wo << " lost while moving");
            _fo_wo = nullptr;
        }
    }

bool
FocusToggleEventHandler::initEventHandler(void)
{
    _menu = createNextPrevMenu();

    // no clients in the list
    if (_menu->size() == 0) {
        return false;
    }

    // unable to grab keyboard
    if (! X11::grabKeyboard(X11::getRoot())) {
        return false;
    }

    // find the focused window object
    if (PWinObj::isFocusedPWinObj(PWinObj::WO_CLIENT)) {
        auto fo_wo = PWinObj::getFocusedPWinObj()->getParent();

        auto it(_menu->m_begin());
        for (; it != _menu->m_end(); ++it) {
            if ((*it)->getWORef() == fo_wo) {
                _menu->selectItem(it);
                break;
            }
        }
        fo_wo->setFocused(false);
    }

    if (_cfg->getShowFrameList()) {
        _menu->buildMenu();

        Geometry head;
        auto chs = pekwm::config()->getCurrHeadSelector();
        X11::getHeadInfo(X11Util::getCurrHead(chs), head);
        _menu->move(head.x + ((head.width - _menu->getWidth()) / 2),
                    head.y + ((head.height - _menu->getHeight()) / 2));
        _menu->setFocused(true);
        _menu->mapWindowRaised();
        PWinObj::setSkipEnterAfter(_menu);
    }

    _menu->selectItemRel(_off);
    setFocusedWo(_menu->getItemCurr()->getWORef());

    return true;
}

EventHandler::Result
FocusToggleEventHandler::handleButtonPressEvent(XButtonEvent*)
{
    // mark as processed disabling wm processing of these events.
    return EventHandler::EVENT_PROCESSED;
}

EventHandler::Result
FocusToggleEventHandler::handleButtonReleaseEvent(XButtonEvent*)
{
    // mark as processed disabling wm processing of these events.
    return EventHandler::EVENT_PROCESSED;
}

EventHandler::Result
FocusToggleEventHandler::handleMotionNotifyEvent(XMotionEvent*)
{
    // mark as processed disabling wm processing of these events.
    return EventHandler::EVENT_PROCESSED;
}

EventHandler::Result
FocusToggleEventHandler::handleKeyEvent(XKeyEvent *ev)
{
    if (ev->type == KeyRelease) {
        if (IsModifierKey(X11::getKeysymFromKeycode(ev->keycode))) {
            return stop();
        }
        return EventHandler::EVENT_PROCESSED;
    }

    if (ev->keycode == _button) {
        if (_fo_wo) {
            if (_raise == TEMP_RAISE) {
                Workspaces::fixStacking(_fo_wo);
            }
            // Restore iconified state
            if (_was_iconified) {
                _was_iconified = false;
                _fo_wo->iconify();
            }
            _fo_wo->setFocused(false);
        }

        _menu->selectItemRel(_off);
        setFocusedWo(_menu->getItemCurr()->getWORef());

        return EventHandler::EVENT_PROCESSED;
    }

    return stop();
}

EventHandler::Result
FocusToggleEventHandler::stop(void)
{
    X11::ungrabKeyboard();

    // Got something to focus
    if (_fo_wo) {
        if (_raise == TEMP_RAISE) {
            _fo_wo->raise();
            _fo_wo->setFocused(true);
        }

        // De-iconify if iconified, user probably wants this
        if (_fo_wo->isIconified()) {
            // If the window was iconfied, and sticky
            _fo_wo->setWorkspace(Workspaces::getActive());
            _fo_wo->mapWindow();
            _fo_wo->raise();
        } else if (_raise == END_RAISE) {
            _fo_wo->raise();
        }

        // Give focus
        _fo_wo->giveInputFocus();
    }

    return EventHandler::EVENT_STOP_PROCESSED;
}

void
FocusToggleEventHandler::setFocusedWo(PWinObj *fo_wo)
{
    if (_fo_wo) {
        _fo_wo->removeObserver(this);
    }
    _fo_wo = fo_wo;
    if (_fo_wo) {
        _fo_wo->addObserver(this);

        _fo_wo->setFocused(true);
        if (_raise == ALWAYS_RAISE) {
            // Make sure it's not iconified if raise is on.
            if (_fo_wo->isIconified()) {
                _was_iconified = true;
                _fo_wo->mapWindow();
            }
            _fo_wo->raise();
        } else if (_raise == TEMP_RAISE) {
            X11::raiseWindow(_fo_wo->getWindow());
            X11::raiseWindow(_menu->getWindow());
        }
    }
}

/**
 * Creates a menu containing a list of Frames currently visible
 * @param show_iconified Flag to show/hide iconified windows
 * @param mru Whether MRU order should be used or not.
 */
PMenu*
FocusToggleEventHandler::createNextPrevMenu(void)
{
    auto menu = new PMenu(_mru ? L"MRU Windows" : L"Windows", "" /* name*/);

    auto it = _mru ? Workspaces::mru_begin() : Frame::frame_begin();
    auto end = _mru ? Workspaces::mru_end() : Frame::frame_end();
    for (; it != end; ++it) {
        auto frame = *it;
        if (createMenuInclude(frame, _show_iconified)) {
            auto client = static_cast<Client*>(frame->getActiveChild());
            menu->insert(client->getTitle()->getVisible(), ActionEvent(),
                         frame, client->getIcon());
        }
    }

    return menu;
}

/**
 * Helper to decide wheter or not to include Frame in menu
 *
 * @param frame Frame to check
 * @param show_iconified Wheter or not to include iconified windows
 * @return true if it should be included, else false
 */
bool
FocusToggleEventHandler::createMenuInclude(Frame *frame, bool show_iconified)
{
    // focw == frame on current workspace
    bool focw = frame->isSticky()
        || frame->getWorkspace() == Workspaces::getActive();
    // ibs == iconified but should be shown
    bool ibs = (!frame->isIconified() || show_iconified) && focw;
    return ! frame->isSkip(SKIP_FOCUS_TOGGLE)
        && frame->isFocusable()
        && ibs;
}
