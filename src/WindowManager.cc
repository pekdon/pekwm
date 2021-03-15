//
// WindowManager.cc for pekwm
// Copyright (C) 2002-2021 the pekwm development team
//
// windowmanager.cc for aewm++
// Copyright (C) 2000 Frank Hale <frankhale@yahoo.com>
// http://sapphire.sourceforge.net/
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "config.h"

#include "Debug.hh"
#include "PWinObj.hh"
#include "PDecor.hh"
#include "Frame.hh"
#include "Client.hh"
#include "WindowManager.hh"

#include "ActionHandler.hh"
#include "AutoProperties.hh"
#include "Config.hh"
#include "Theme.hh"
#include "PFont.hh"
#include "PTexture.hh"
#include "FontHandler.hh"
#include "TextureHandler.hh"
#include "Workspaces.hh"
#include "Util.hh"
#include "X11Util.hh"
#include "X11.hh"

#include "RegexString.hh"

#include "KeyGrabber.hh"
#include "MenuHandler.hh"
#include "Harbour.hh"
#include "DockApp.hh"
#include "CmdDialog.hh"
#include "SearchDialog.hh"
#include "StatusWindow.hh"
#include "WorkspaceIndicator.hh"

#include <cstring>
#include <iostream>
#include <functional>
#include <memory>
#include <cassert>

extern "C" {
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <X11/Xatom.h>
#include <X11/keysym.h>
#ifdef HAVE_XRANDR
#include <X11/extensions/Xrandr.h>
#endif // HAVE_XRANDR
}

// include after all includes to get ifndefs right
#include "Compat.hh"

extern "C" {

    static bool is_signal = false;
    static bool is_signal_hup = false;
    static bool is_signal_int_term = false;
    static bool is_signal_alrm = false;
    static bool is_signal_chld = false;

    /**
      * Signal handler setting signal flags.
      */
    static void
    sigHandler(int signal)
    {
        is_signal = true;

        switch (signal) {
        case SIGHUP:
            is_signal_hup = true;
            break;
        case SIGINT:
        case SIGTERM:
            is_signal_int_term = true;
            break;
        case SIGCHLD:
            is_signal_chld = true;
            break;
        case SIGALRM:
            // Do nothing, just used to break out of waiting
            is_signal_alrm = true;
            break;
        }
    }

} // extern "C"


// WindowManager

/**
 * Create window manager instance and run main routine.
 */
WindowManager*
WindowManager::start(const std::string &config_file,
                     bool replace, bool synchronous)
{
    auto dpy = XOpenDisplay(0);
    if (! dpy) {
        std::cerr << "Can not open display!" << std::endl
                  << "Your DISPLAY variable currently is set to: "
                  << Util::getEnv("DISPLAY") << std::endl;
        return nullptr;
    }

    // Setup window manager
    auto wm = new WindowManager();
    if (! pekwm::init(wm, wm, dpy, config_file, replace, synchronous)) {
        delete wm;
        wm = nullptr;

    } else {
        wm->setupDisplay(dpy);
        wm->scanWindows();
        Frame::resetFrameIDs();

        pekwm::rootWo()->setEwmhDesktopNames();
        pekwm::rootWo()->setEwmhDesktopLayout();
        Workspaces::updateClientList();

        // add all frames to the MRU list
        auto it = Frame::frame_begin();
        for (; it != Frame::frame_end(); ++it) {
            Workspaces::addToMRUBack(*it);
        }

        wm->startBackground(pekwm::theme()->getThemeDir(),
                            pekwm::theme()->getBackground());
        wm->execStartFile();
    }

    return wm;
}

//! @brief Constructor for WindowManager class
WindowManager::WindowManager()
    : _shutdown(false),
      _reload(false),
      _restart(false),
      _bg_pid(-1),
      _event_handler(nullptr),
      _skip_enter(false)
{
    pekwm::setIsStartup(false),

    _screen_edges[0] = 0;
    _screen_edges[1] = 0;
    _screen_edges[2] = 0;
    _screen_edges[3] = 0;

    struct sigaction act;

    // Set up the signal handlers.
    act.sa_handler = sigHandler;
    act.sa_mask = sigset_t();
    act.sa_flags = SA_NOCLDSTOP | SA_NODEFER;

    sigaction(SIGTERM, &act, 0);
    sigaction(SIGINT, &act, 0);
    sigaction(SIGHUP, &act, 0);
    sigaction(SIGCHLD, &act, 0);
    sigaction(SIGALRM, &act, 0);
}

//! @brief WindowManager destructor
WindowManager::~WindowManager(void)
{
    cleanup();

    MenuHandler::deleteMenus();
    Workspaces::cleanup();
}

//! @brief Checks if the start file is executable then execs it.
void
WindowManager::execStartFile(void)
{
    std::string start_file(pekwm::config()->getStartFile());
    if (start_file.empty()) {
        return;
    }

    bool exec = Util::isExecutable(start_file);
    if (! exec) {
        start_file = SYSCONFDIR "/start";
        exec = Util::isExecutable(start_file);
    }

    if (exec) {
        Util::forkExec(start_file);
    }
}

//! @brief Cleans up, Maps windows etc.
void
WindowManager::cleanup(void)
{
    stopBackground();

    // update all nonactive clients properties
    auto it_f(Frame::frame_begin());
    for (; it_f != Frame::frame_end(); ++it_f) {
        (*it_f)->updateInactiveChildInfo();
    }

    if (pekwm::harbour()) {
        pekwm::harbour()->removeAllDockApps();
    }

    // To preserve stacking order when destroying the frames, we go through
    // the PWinObj list from the Workspaces and put all Frames into our own
    // list, then we delete the frames in order.
    std::vector<Frame*> frame_list;
    Workspaces::iterator it_w(Workspaces::begin());
    for (; it_w != Workspaces::end(); ++it_w) {
        if ((*it_w)->getType() == PWinObj::WO_FRAME) {
            frame_list.push_back(static_cast<Frame*>(*it_w));
        }
    }

    // Delete all Frames. This reparents the Clients.
    for (it_f = frame_list.begin(); it_f != frame_list.end(); ++it_f) {
        delete *it_f;
    }

    // Delete all Clients.
    while (Client::client_begin() != Client::client_end()) {
        delete *Client::client_begin();
    }

    if (pekwm::keyGrabber()) {
        pekwm::keyGrabber()->ungrabKeys(X11::getRoot());
    }

    // destroy screen edge
    for (int i=0; i < 4; ++i) {
        Workspaces::remove(_screen_edges[i]);
        delete _screen_edges[i];
        _screen_edges[i]=0;
    }

    if (X11::getDpy()) {
        XInstallColormap(X11::getDpy(), X11::getColormap());
        X11::setInputFocus(PointerRoot);
    }
}

/**
 * Setup display and claim resources.
 */
void
WindowManager::setupDisplay(Display* dpy)
{
    pekwm::autoProperties()->load();

    Workspaces::init();
    Workspaces::setSize(pekwm::config()->getWorkspaces());
    Workspaces::setPerRow(pekwm::config()->getWorkspacesPerRow());

    MenuHandler::createMenus(pekwm::actionHandler());

    XDefineCursor(dpy, X11::getRoot(), X11::getCursor(CURSOR_ARROW));

#ifdef HAVE_XRANDR
    XRRSelectInput(dpy, X11::getRoot(), RRScreenChangeNotifyMask);
#endif // HAVE_XRANDR

    // Create screen edge windows
    screenEdgeCreate();
    screenEdgeMapUnmap();
}

//! @brief Goes through the window and creates Clients/DockApps.
void
WindowManager::scanWindows(void)
{
    // only done once when we start
    if (pekwm::isStartup()) {
        return;
    }

    uint num_wins;
    Window d_win1, d_win2, *wins;

    // Lets create a list of windows on the display
    XQueryTree(X11::getDpy(), X11::getRoot(),
               &d_win1, &d_win2, &wins, &num_wins);
    std::vector<Window> win_list(wins, wins + num_wins);
    X11::free(wins);

    auto it(win_list.begin());

    // We filter out all windows with the the IconWindowHint
    // set not pointing to themselves, making DockApps
    // work as they are supposed to.
    for (; it != win_list.end(); ++it) {
        if (*it == None) {
            continue;
        }

        auto wm_hints = XGetWMHints(X11::getDpy(), *it);
        if (wm_hints) {
            if ((wm_hints->flags&IconWindowHint) &&
                    (wm_hints->icon_window != *it)) {
                auto i_it(find(win_list.begin(), win_list.end(),
                               wm_hints->icon_window));
                if (i_it != win_list.end())
                    *i_it = None;
            }
            X11::free(wm_hints);
        }
    }

    for (it = win_list.begin(); it != win_list.end(); ++it) {
        if (*it != None) {
            createClient(*it, false);
        }
    }

    // Try to focus the ontop window, if no window we give root focus
    PWinObj *wo = Workspaces::getTopWO(PWinObj::WO_FRAME);
    if (wo && wo->isMapped()) {
        wo->giveInputFocus();
    } else {
        pekwm::rootWo()->giveInputFocus();
    }

    // Try to find transients for all clients, on restarts ordering might
    // not be correct.
    auto it_client = Client::client_begin();
    for (; it_client != Client::client_end(); ++it_client) {
        if ((*it_client)->isTransient() && ! (*it_client)->getTransientForClient()) {
            (*it_client)->findAndRaiseIfTransient();
        }
    }

    // We won't be needing these anymore until next restart
    pekwm::autoProperties()->removeApplyOnStart();

    pekwm::setIsStartup(true);
}

//! @brief Creates and places screen edge
void
WindowManager::screenEdgeCreate(void)
{
    bool indent = pekwm::config()->getScreenEdgeIndent();

    auto cfg = pekwm::config();
    auto root_wo = pekwm::rootWo();
    _screen_edges[0] =
        new EdgeWO(root_wo, SCREEN_EDGE_LEFT,
                   indent && (cfg->getScreenEdgeSize(SCREEN_EDGE_LEFT) > 0),
                   cfg);
    _screen_edges[1] =
        new EdgeWO(root_wo, SCREEN_EDGE_RIGHT,
                   indent && (cfg->getScreenEdgeSize(SCREEN_EDGE_RIGHT) > 0),
                   cfg);
    _screen_edges[2] =
        new EdgeWO(root_wo, SCREEN_EDGE_TOP,
                   indent && (cfg->getScreenEdgeSize(SCREEN_EDGE_TOP) > 0),
                   cfg);
    _screen_edges[3] =
        new EdgeWO(root_wo, SCREEN_EDGE_BOTTOM,
                   indent && (cfg->getScreenEdgeSize(SCREEN_EDGE_BOTTOM) > 0),
                   cfg);

    // make sure the edge stays ontop
    for (int i=0; i < 4; ++i) {
        Workspaces::insert(_screen_edges[i]);
    }

    screenEdgeResize();
}

void
WindowManager::screenEdgeResize(void)
{
    auto cfg = pekwm::config();
    uint l_size = std::max(cfg->getScreenEdgeSize(SCREEN_EDGE_LEFT), 1);
    uint r_size = std::max(cfg->getScreenEdgeSize(SCREEN_EDGE_RIGHT), 1);
    uint t_size = std::max(cfg->getScreenEdgeSize(SCREEN_EDGE_TOP), 1);
    uint b_size = std::max(cfg->getScreenEdgeSize(SCREEN_EDGE_BOTTOM), 1);

    // Left edge
    _screen_edges[0]->moveResize(0, 0,
                                 l_size, X11::getHeight());

    // Right edge
    _screen_edges[1]->moveResize(X11::getWidth() - r_size, 0,
                                 r_size, X11::getHeight());

    // Top edge
    _screen_edges[2]->moveResize(l_size, 0,
                                 X11::getWidth() - l_size - r_size, t_size);

    // Bottom edge
    _screen_edges[3]->moveResize(l_size, X11::getHeight() - b_size,
                                 X11::getWidth() - l_size - r_size, b_size);

    for (int i = 0; i < 4; ++i) {
        _screen_edges[i]->configureStrut(cfg->getScreenEdgeIndent()
                                         && cfg->getScreenEdgeSize(_screen_edges[i]->getEdge()) > 0);
    }

    pekwm::rootWo()->updateStrut();
}

void
WindowManager::screenEdgeMapUnmap(void)
{
    EdgeWO* edge;
    for (int i=0; i<4; ++i) {
        edge = _screen_edges[i];
        if (pekwm::config()->getScreenEdgeSize(edge->getEdge()) > 0
            && pekwm::config()->getEdgeListFromPosition(edge->getEdge())->size() > 0) {
            edge->mapWindow();
        } else {
            edge->unmapWindow();
        }
    }
}

//! @brief Reloads configuration and updates states.
void
WindowManager::doReload(void)
{
    doReloadConfig();
    doReloadTheme();
    doReloadMouse();
    doReloadKeygrabber();
    doReloadAutoproperties();

    MenuHandler::reloadMenus(pekwm::actionHandler());
    doReloadHarbour();

    pekwm::rootWo()->setEwmhDesktopNames();
    pekwm::rootWo()->setEwmhDesktopLayout();

    _reload = false;
}

/**
 * Reload main config file.
 */
void
WindowManager::doReloadConfig(void)
{
    // If any of these changes, re-fetch of all names is required
    bool old_client_unique_name = pekwm::config()->getClientUniqueName();
    auto old_client_unique_name_pre = pekwm::config()->getClientUniqueNamePre();
    auto old_client_unique_name_post = pekwm::config()->getClientUniqueNamePost();

    // Reload configuration
    if (! pekwm::config()->load(pekwm::config()->getConfigFile())) {
        return;
    }

    // Update what might have changed in the cfg touching the hints
    Workspaces::setSize(pekwm::config()->getWorkspaces());
    Workspaces::setPerRow(pekwm::config()->getWorkspacesPerRow());
    Workspaces::setNames();

    // Update the ClientUniqueNames if needed
    if ((old_client_unique_name != pekwm::config()->getClientUniqueName()) ||
        (old_client_unique_name_pre != pekwm::config()->getClientUniqueNamePre()) ||
        (old_client_unique_name_post != pekwm::config()->getClientUniqueNamePost())) {
        for_each(Client::client_begin(), Client::client_end(),
                 std::mem_fn(&Client::readName));
    }

    // Resize the screen edge
    screenEdgeResize();
    screenEdgeMapUnmap();

    pekwm::rootWo()->updateStrut();
}

/**
 * Reload theme file and update decorations.
 */
void
WindowManager::doReloadTheme(void)
{
    // Reload the theme
    if (! pekwm::theme()->load(pekwm::config()->getThemeFile(),
                               pekwm::config()->getThemeVariant())) {
        return;
    }

    startBackground(pekwm::theme()->getThemeDir(),
                    pekwm::theme()->getBackground());

    // Reload the themes on all decors
    for_each(PDecor::pdecor_begin(), PDecor::pdecor_end(),
             std::mem_fn(&PDecor::loadDecor));
}

void
WindowManager::startBackground(const std::string& theme_dir,
                               const std::string& texture)
{
    stopBackground();
    if (pekwm::config()->getThemeBackground() && ! texture.empty()) {
        std::vector<std::string> args =
            {BINDIR "/pekwm_bg", "--load-dir", theme_dir + "/backgrounds",
             texture};
        _bg_pid = Util::forkExec(args);
    }
}

void
WindowManager::stopBackground(void)
{
    if (_bg_pid != -1) {
        // SIGCHILD will take care of waiting for the child
        kill(_bg_pid, SIGKILL);
    }
    _bg_pid = -1;
}


/**
 * Reload mouse configuration and re-grab buttons on all windows.
 */
void
WindowManager::doReloadMouse(void)
{
    if (! pekwm::config()->loadMouseConfig(pekwm::config()->getMouseConfigFile())) {
        return;
    }

    for_each(Client::client_begin(), Client::client_end(),
             std::mem_fn(&Client::grabButtons));
}

/**
 * Reload keygrabber configuration and re-grab keys on all windows.
 */
void
WindowManager::doReloadKeygrabber(bool force)
{
    // Reload the keygrabber
    if (! pekwm::keyGrabber()->load(pekwm::config()->getKeyFile(), force)) {
        return;
    }

    pekwm::keyGrabber()->ungrabKeys(X11::getRoot());
    pekwm::keyGrabber()->grabKeys(X11::getRoot());

    // Regrab keys and buttons
    auto c_it(Client::client_begin());
    for (; c_it != Client::client_end(); ++c_it) {
        (*c_it)->grabButtons();
        pekwm::keyGrabber()->ungrabKeys((*c_it)->getWindow());
        pekwm::keyGrabber()->grabKeys((*c_it)->getWindow());
    }
}

/**
 * Reload autoproperties.
 */
void
WindowManager::doReloadAutoproperties(void)
{
    if (! pekwm::autoProperties()->load()) {
        return;
    }

    // NOTE: we need to load autoproperties after decor have been updated
    // as otherwise old theme data pointer will be used and sig 11 pekwm.
    auto it_c(Client::client_begin());
    for (; it_c != Client::client_end(); ++it_c) {
        (*it_c)->readAutoprops(APPLY_ON_RELOAD);
    }

    auto it_f(Frame::frame_begin());
    for (; it_f != Frame::frame_end(); ++it_f) {
        (*it_f)->readAutoprops(APPLY_ON_RELOAD);
    }
}

/**
 * Reload harbour configuration.
 */
void
WindowManager::doReloadHarbour(void)
{
    pekwm::harbour()->loadTheme();
    pekwm::harbour()->rearrange();
    pekwm::harbour()->restack();
    pekwm::harbour()->updateHarbourSize();
}

/**
 * Exit pekwm and restart with the command command
 */
void
WindowManager::restart(std::string command)
{
    if (! command.empty()) {
        _restart_command = command;
    }
    _restart = true;
    _shutdown = true;
}

// Event handling routins beneath this =====================================

void
WindowManager::handleSignals(void)
{
    // SIGALRM used to timeout workspace indicator
    if (is_signal_alrm) {
        TRACE("handle SIGALRM");
        is_signal_alrm = false;
        Workspaces::hideWorkspaceIndicator();
    }

    // SIGHUP
    if (is_signal_hup || _reload) {
        TRACE("handle SIGHUP or reload");
        is_signal_hup = false;
        doReload();
    }

    // Wait for children if a SIGCHLD was received
    if (is_signal_chld) {
        TRACE("handle SIGHUP");
        pid_t pid;
        do {
            pid = waitpid(WAIT_ANY, nullptr, WNOHANG);
            if (pid == -1) {
                if (errno == EINTR) {
                    TRACE("waitpid interrupted, retrying");
                }
            } else if (pid == 0) {
                TRACE("no more finished child processes");
            } else {
                TRACE("child process " << pid << " finished");
            }
        } while (pid > 0 || (pid == -1 && errno == EINTR));

        is_signal_chld = false;
    }
}

void
WindowManager::doEventLoop(void)
{
    XEvent ev;

    while (! _shutdown && ! is_signal_int_term) {
        if (is_signal) {
            handleSignals();
            is_signal = false;
        }
        if (_reload) {
            doReload();
        }

        // Get next event, drop event handling if none was given
        if (X11::getNextEvent(ev)) {
            if (! _event_handler || ! handleEventHandlerEvent(ev)) {
                handleEvent(ev);
            }
        }
    }
}

bool
WindowManager::handleEventHandlerEvent(XEvent &ev)
{
    EventHandler::Result res;
    switch (ev.type) {
    case ButtonPress:
        res = _event_handler->handleButtonPressEvent(&ev.xbutton);
        break;
    case ButtonRelease:
        res = _event_handler->handleButtonReleaseEvent(&ev.xbutton);
        break;
    case KeyPress:
    case KeyRelease:
        res = _event_handler->handleKeyEvent(&ev.xkey);
        break;
    case MotionNotify:
        res = _event_handler->handleMotionNotifyEvent(&ev.xmotion);
        break;
    default:
        res = EventHandler::EVENT_SKIP;
    }

    switch (res) {
    case EventHandler::EVENT_STOP_PROCESSED:
    case EventHandler::EVENT_STOP_SKIP:
        TRACE("removing event handler " << _event_handler);
        setEventHandler(nullptr);
        return res == EventHandler::EVENT_STOP_PROCESSED;
    default:
        return res == EventHandler::EVENT_PROCESSED;
    }
}

void
WindowManager::handleEvent(XEvent &ev)
{
    switch (ev.type) {
    case MapRequest:
        handleMapRequestEvent(&ev.xmaprequest);
        break;
    case UnmapNotify:
        handleUnmapEvent(&ev.xunmap);
        break;
    case DestroyNotify:
        handleDestroyWindowEvent(&ev.xdestroywindow);
        break;

    case ConfigureRequest:
        handleConfigureRequestEvent(&ev.xconfigurerequest);
        break;
    case ClientMessage:
        handleClientMessageEvent(&ev.xclient);
        break;
    case ColormapNotify:
        handleColormapEvent(&ev.xcolormap);
        break;
    case PropertyNotify:
        X11::setLastEventTime(ev.xproperty.time);
        handlePropertyEvent(&ev.xproperty);
        break;
    case MappingNotify:
        handleMappingEvent(&ev.xmapping);
        break;
    case Expose:
        handleExposeEvent(&ev.xexpose);
        break;

    case KeyPress:
    case KeyRelease:
        X11::setLastEventTime(ev.xkey.time);
        handleKeyEvent(&ev.xkey);
        break;

    case ButtonPress:
        X11::setLastEventTime(ev.xbutton.time);
        handleButtonPressEvent(&ev.xbutton);
        break;
    case ButtonRelease:
        X11::setLastEventTime(ev.xbutton.time);
        handleButtonReleaseEvent(&ev.xbutton);
        break;

    case MotionNotify:
        X11::setLastEventTime(ev.xmotion.time);
        handleMotionEvent(&ev.xmotion);
        break;

    case EnterNotify:
        X11::setLastEventTime(ev.xcrossing.time);
        handleEnterNotify(&ev.xcrossing);
        break;
    case LeaveNotify:
        X11::setLastEventTime(ev.xcrossing.time);
        handleLeaveNotify(&ev.xcrossing);
        break;
    case FocusIn:
        handleFocusInEvent(&ev.xfocus);
        break;
    case FocusOut:
        break;

    case SelectionClear:
        // Another window
        LOG("being replaced by another WM");
        _shutdown = true;
        break;

    default:
#ifdef HAVE_SHAPE
        if (X11::hasExtensionShape()
            && ev.type == X11::getEventShape()) {
            auto sev = reinterpret_cast<XShapeEvent*>(&ev);
            X11::setLastEventTime(sev->time);
            auto client = Client::findClient(sev->window);
            if (client) {
                client->handleShapeEvent(sev);
            }
        }
#endif // HAVE_SHAPE
#ifdef HAVE_XRANDR
        if (X11::hasExtensionXRandr()) {
            if (ev.type == X11::getEventXRandr() + RRScreenChangeNotify) {
                XRRScreenChangeNotifyEvent *scr_ev =
                    reinterpret_cast<XRRScreenChangeNotifyEvent*>(&ev);

                if  (scr_ev->rotation == RR_Rotate_90
                     || scr_ev->rotation == RR_Rotate_270) {
                    pekwm::rootWo()->updateGeometry(scr_ev->height,
                                                    scr_ev->width);
                } else {
                    pekwm::rootWo()->updateGeometry(scr_ev->width,
                                                    scr_ev->height);
                }

                pekwm::harbour()->updateGeometry();
                screenEdgeResize();

                // Make sure windows are visible after resize
                auto it(PDecor::pdecor_begin());
                for (; it != PDecor::pdecor_end(); ++it) {
                    Workspaces::placeWoInsideScreen(*it);
                }
            }
        }
#endif // HAVE_XRANDR
        break;
    }
}

//! @brief Handle XKeyEvents
void
WindowManager::handleKeyEvent(XKeyEvent *ev)
{
    bool matched = false;
    ActionEvent *ae = 0;
    PWinObj *wo, *wo_orig;
    wo = wo_orig = PWinObj::getFocusedPWinObj();
    PWinObj::Type type = (wo) ? wo->getType() : PWinObj::WO_SCREEN_ROOT;

    if (wo && wo->getWindow() == ev->window) {
        wo->setLastActivity(ev->time);
    }

    switch (type) {
    case PWinObj::WO_CLIENT:
    case PWinObj::WO_FRAME:
    case PWinObj::WO_SCREEN_ROOT:
    case PWinObj::WO_MENU:
        if (ev->type == KeyPress) {
            ae = pekwm::keyGrabber()->findAction(ev, type, matched);
        }
        break;
    case PWinObj::WO_CMD_DIALOG:
    case PWinObj::WO_SEARCH_DIALOG:
        if (ev->type == KeyPress) {
            ae = wo->handleKeyPress(ev);
        } else {
            ae = wo->handleKeyRelease(ev);
        }
        wo = static_cast<InputDialog*>(wo)->getWORef();
        break;
    default:
        if (wo) {
            if (ev->type == KeyPress) {
                ae = wo->handleKeyPress(ev);
            } else {
                ae = wo->handleKeyRelease(ev);
            }
        }
        break;
    }

    handleKeyEventAction(ev, ae, wo, wo_orig);

    // Flush Enter events caused by keygrabbing
    if (matched) {
        XEvent e;
        while (X11::checkTypedEvent(EnterNotify, &e)) {
            if (! e.xcrossing.send_event) {
                X11::setLastEventTime(e.xcrossing.time);
            }
        }
    }
}

void
WindowManager::handleKeyEventAction(XKeyEvent *ev, ActionEvent *ae, PWinObj *wo, PWinObj *wo_orig)
{
    if (!ae) {
        return;
    }

    // HACK: Always close CmdDialog and SearchDialog before actions
    if (wo_orig
        && (wo_orig->getType() == PWinObj::WO_CMD_DIALOG
            || wo_orig->getType() == PWinObj::WO_SEARCH_DIALOG)) {
        ::Action close_action;
        ActionEvent close_ae;

        close_ae.action_list.push_back(close_action);
        close_ae.action_list.back().setAction(ACTION_CLOSE);

        ActionPerformed close_ap(wo_orig, close_ae);
        pekwm::actionHandler()->handleAction(close_ap);
    }

    ActionPerformed ap(wo, *ae);
    ap.type = ev->type;
    ap.event.key = ev;
    pekwm::actionHandler()->handleAction(ap);
}

void
WindowManager::handleButtonPressEvent(XButtonEvent *ev)
{
    // Clear event queue
    while (X11::checkTypedEvent(ButtonPress, (XEvent *) ev)) {
        if (! ev->send_event) {
            X11::setLastEventTime(ev->time);
        }
    }

    ActionEvent *ae = 0;
    PWinObj *wo = 0;

    wo = PWinObj::findPWinObj(ev->window);
    if (wo) {
        // Update all objects (and again if child found)
        wo->setLastActivity(ev->time);

        ae = wo->handleButtonPress(ev);

        if (wo->getType() == PWinObj::WO_FRAME) {
            // this is done so that clicking the titlebar executes action on
            // the client clicked on, doesn't apply when subwindow is set (meaning
            // a titlebar button beeing pressed)
            if ((ev->subwindow == None)
                && (ev->window == static_cast<Frame*>(wo)->getTitleWindow())) {
                wo = static_cast<Frame*>(wo)->getChildFromPos(ev->x);
            } else {
                wo = static_cast<Frame*>(wo)->getActiveChild();
            }

            if (wo != 0) {
                wo->setLastActivity(ev->time);
            }
        }
    }

    if (ae) {
        ActionPerformed ap(wo, *ae);
        ap.type = ev->type;
        ap.event.button = ev;

        pekwm::actionHandler()->handleAction(ap);
    } else {
        auto da = pekwm::harbour()->findDockAppFromFrame(ev->window);
        if (da) {
            pekwm::harbour()->handleButtonEvent(ev, da);
        }
    }
}

void
WindowManager::handleButtonReleaseEvent(XButtonEvent *ev)
{
    // Flush ButtonReleases
    while (X11::checkTypedEvent(ButtonRelease, (XEvent *) ev)) {
        if (! ev->send_event) {
            X11::setLastEventTime(ev->time);
        }
    }

    ActionEvent *ae = 0;
    PWinObj *wo = PWinObj::findPWinObj(ev->window);
    if (wo == pekwm::rootWo() && ev->subwindow != None) {
        wo = PWinObj::findPWinObj(ev->subwindow);
    }

    if (wo) {
        // Kludge for the case that wo is freed by handleButtonRelease(.).
        PWinObj::Type wotype = wo->getType();
        ae = wo->handleButtonRelease(ev);

        if (wotype == PWinObj::WO_FRAME) {
            // this is done so that clicking the titlebar executes action on
            // the client clicked on, doesn't apply when subwindow is set (meaning
            // a titlebar button beeing pressed)
            if ((ev->subwindow == None)
                && (ev->window == static_cast<Frame*>(wo)->getTitleWindow())) {
                wo = static_cast<Frame*>(wo)->getChildFromPos(ev->x);
            } else {
                wo = static_cast<Frame*>(wo)->getActiveChild();
            }
        }

        if (ae) {
            ActionPerformed ap(wo, *ae);
            ap.type = ev->type;
            ap.event.button = ev;

            pekwm::actionHandler()->handleAction(ap);
        }
    } else {
        auto da = pekwm::harbour()->findDockAppFromFrame(ev->window);
        if (da) {
            pekwm::harbour()->handleButtonEvent(ev, da);
        }
    }
}

void
WindowManager::handleConfigureRequestEvent(XConfigureRequestEvent *ev)
{
    Client *client = Client::findClientFromWindow(ev->window);

    if (client) {
        ((Frame*) client->getParent())->handleConfigureRequest(ev, client);

    } else {
        auto da = pekwm::harbour()->findDockApp(ev->window);
        if (da) {
            pekwm::harbour()->handleConfigureRequestEvent(ev, da);
        } else {
            // Since this window isn't yet a client lets delegate
            // the configure request back to the window so it can use it.

            XWindowChanges wc;

            wc.x = ev->x;
            wc.y = ev->y;
            wc.width = ev->width;
            wc.height = ev->height;
            wc.sibling = ev->above;
            wc.stack_mode = ev->detail;

            XConfigureWindow(X11::getDpy(), ev->window, ev->value_mask, &wc);
        }
    }
}

/**
 * Handle motion event, match on event window expect when event window
 * is root and subwindow is set then also match on menus.
 */
void
WindowManager::handleMotionEvent(XMotionEvent *ev)
{
    ActionEvent *ae = 0;
    PWinObj *wo = PWinObj::findPWinObj(ev->window);
    if (wo == pekwm::rootWo() && ev->subwindow != None) {
        wo = PWinObj::findPWinObj(ev->subwindow);
    }

    if (wo) {
        if (wo->getType() == PWinObj::WO_CLIENT) {
            ae = wo->getParent()->handleMotionEvent(ev);

        } else if (wo->getType() == PWinObj::WO_FRAME) {
            ae = wo->handleMotionEvent(ev);

            // this is done so that clicking the titlebar executes action on
            // the client clicked on
            if (ev->subwindow != None) {
                wo = static_cast<Frame*>(wo)->getActiveChild();
            } else {
                wo = static_cast<Frame*>(wo)->getChildFromPos(ev->x);
            }

        } else {
            ae = wo->handleMotionEvent(ev);
        }

        if (ae) {
            ActionPerformed ap(wo, *ae);
            ap.type = ev->type;
            ap.event.motion = ev;

            pekwm::actionHandler()->handleAction(ap);
        }
    } else {
        auto da = pekwm::harbour()->findDockAppFromFrame(ev->window);
        if (da) {
            pekwm::harbour()->handleMotionNotifyEvent(ev, da);
        }
    }
}

void
WindowManager::handleMapRequestEvent(XMapRequestEvent *ev)
{
    PWinObj *wo = PWinObj::findPWinObj(ev->window);

    if (wo) {
        wo->handleMapRequest(ev);
    } else {
        createClient(ev->window, true);
    }
}

void
WindowManager::handleUnmapEvent(XUnmapEvent *ev)
{
    PWinObj *wo = PWinObj::findPWinObj(ev->window);
    PWinObj *wo_search = 0;

    PWinObj::Type wo_type = PWinObj::WO_NO_TYPE;

    if (wo) {
        wo_type = wo->getType();
        if (wo_type == PWinObj::WO_CLIENT) {
            wo_search = wo->getParent();
        }

        wo->handleUnmapEvent(ev);

        if (wo == PWinObj::getFocusedPWinObj()) {
            PWinObj::setFocusedPWinObj(0);
        }
    } else {
        auto da = pekwm::harbour()->findDockApp(ev->window);
        if (da) {
            if (ev->window == ev->event) {
                pekwm::harbour()->removeDockApp(da);
            }
        }
    }

    if (wo_type != PWinObj::WO_MENU
        && wo_type != PWinObj::WO_CMD_DIALOG
        && wo_type != PWinObj::WO_SEARCH_DIALOG
        && ! PWinObj::getFocusedPWinObj()) {
        Workspaces::findWOAndFocus(wo_search);
    }
}

void
WindowManager::handleDestroyWindowEvent(XDestroyWindowEvent *ev)
{
    Client *client = Client::findClientFromWindow(ev->window);

    if (client) {
        PWinObj *wo_search = client->getParent();
        client->handleDestroyEvent(ev);

        if (! PWinObj::getFocusedPWinObj()) {
            Workspaces::findWOAndFocus(wo_search);
        }
    } else {
        auto da = pekwm::harbour()->findDockApp(ev->window);
        if (da) {
            da->setAlive(false);
            pekwm::harbour()->removeDockApp(da);
        }
    }
}

void
WindowManager::handleEnterNotify(XCrossingEvent *ev)
{
    // Window being entered is marked to cause the upcoming enter
    // event to have no effect, multiple consequitive enter events on
    // this window just keeps on making the next enter event being
    // marked to be skipped.
    if (PWinObj::isSkipEnterAfter(ev->window)) {
        _skip_enter = true;
        return;
    }
    PWinObj::setSkipEnterAfter(nullptr);
    if (_skip_enter) {
        _skip_enter = false;
        return;
    }

    // Clear event queue
    while (X11::checkTypedEvent(EnterNotify, (XEvent *) ev)) {
        if (! ev->send_event) {
            X11::setLastEventTime(ev->time);
        }
    }

    if ((ev->mode == NotifyGrab) || (ev->mode == NotifyUngrab)) {

        return;
    }

    PWinObj *wo = PWinObj::findPWinObj(ev->window);

    if (wo) {

        if (wo->getType() == PWinObj::WO_CLIENT) {
            wo = wo->getParent();
        }

        ActionEvent *ae = wo->handleEnterEvent(ev);

        if (ae) {
            ActionPerformed ap(wo, *ae);
            ap.type = ev->type;
            ap.event.crossing = ev;

            pekwm::actionHandler()->handleAction(ap);
        }
    }
}

void
WindowManager::handleLeaveNotify(XCrossingEvent *ev)
{
    // Clear event queue
    while (X11::checkTypedEvent(LeaveNotify, (XEvent *) ev)) {
        if (! ev->send_event) {
            X11::setLastEventTime(ev->time);
        }
    }

    if ((ev->mode == NotifyGrab) || (ev->mode == NotifyUngrab)) {
        return;
    }

    auto wo = PWinObj::findPWinObj(ev->window);
    if (wo) {
        auto ae = wo->handleLeaveEvent(ev);
        if (ae) {
            ActionPerformed ap(wo, *ae);
            ap.type = ev->type;
            ap.event.crossing = ev;
            pekwm::actionHandler()->handleAction(ap);
        }
    }
}

//! @brief Handles FocusIn Events.
void
WindowManager::handleFocusInEvent(XFocusChangeEvent *ev)
{
    if (ev->mode == NotifyGrab
        || ev->mode == NotifyUngrab
        || ev->detail == NotifyVirtual) {
        return;
    }

    PWinObj *wo = PWinObj::findPWinObj(ev->window);
    if (wo) {
        // To simplify logic, changing client in frame wouldn't update the
        // focused window because wo != focused_wo woule be true.
        if (wo->getType() == PWinObj::WO_FRAME) {
            wo = static_cast<Frame*>(wo)->getActiveChild();
        }

        if (! wo->isFocusable() || ! wo->isMapped()) {
            Workspaces::findWOAndFocus(nullptr);

        } else if (wo != PWinObj::getFocusedPWinObj()) {
            // If it's a valid FocusIn event with accepatable target lets flush
            // all EnterNotify and LeaveNotify as they can interfere with
            // focusing if Sloppy or Follow like focus model is used.
            XEvent e_flush;
            while (X11::checkTypedEvent(EnterNotify, &e_flush)) {
                if (! e_flush.xcrossing.send_event) {
                    X11::setLastEventTime(e_flush.xcrossing.time);
                }
            }
            while (X11::checkTypedEvent(LeaveNotify, &e_flush)) {
                if (! e_flush.xcrossing.send_event) {
                    X11::setLastEventTime(e_flush.xcrossing.time);
                }
            }

            PWinObj *focused_wo = PWinObj::getFocusedPWinObj(); // convenience

            // unfocus last window
            if (focused_wo) {
                if (focused_wo->getType() == PWinObj::WO_CLIENT) {
                    focused_wo->getParent()->setFocused(false);
                } else {
                    focused_wo->setFocused(false);
                }
                Workspaces::setLastFocused(Workspaces::getActive(), focused_wo);
            }

            PWinObj::setFocusedPWinObj(wo);

            if (wo->getType() == PWinObj::WO_CLIENT) {
                wo->getParent()->setFocused(true);
                pekwm::rootWo()->setEwmhActiveWindow(wo->getWindow());

                // update the MRU list (except for skip focus windows, see #297)
                if (! static_cast<Client*>(wo)->isSkip(SKIP_FOCUS_TOGGLE)) {
                    auto frame = static_cast<Frame*>(wo->getParent());
                    Workspaces::addToMRUFront(frame);
                }
            } else {
                wo->setFocused(true);
                pekwm::rootWo()->setEwmhActiveWindow(None);
            }
        }
    }
}

/**
 * Handle XClientMessageEvent events.
 *
 * @param ev Event to handle.
 */
void
WindowManager::handleClientMessageEvent(XClientMessageEvent *ev)
{
    if (ev->message_type == X11::getAtom(PEKWM_CMD)) {
        // _PEKWM_CMD is handled independent of client
        handlePekwmCmd(ev);
    } if (ev->window == X11::getRoot()) {
        if (ev->format == 32) {
            if (ev->message_type == X11::getAtom(NET_CURRENT_DESKTOP)) {
                Workspaces::setWorkspace(ev->data.l[0], true);
            } else if (ev->message_type ==
                       X11::getAtom(NET_NUMBER_OF_DESKTOPS)) {
                if (ev->data.l[0] > 0) {
                    Workspaces::setSize(ev->data.l[0]);
                }
            }
        }
    } else if (ev->message_type == X11::getAtom(NET_REQUEST_FRAME_EXTENTS)) {
        handleNetRequestFrameExtents(ev->window);
    } else {
        auto client = Client::findClientFromWindow(ev->window);
        if (client) {
            auto *frame = static_cast<Frame*>(client->getParent());
            auto ae = frame->handleClientMessage(ev, client);
            if (ae) {
                ActionPerformed ap(frame, *ae);
                ap.type = ev->type;
                ap.event.client = ev;
                pekwm::actionHandler()->handleAction(ap);
            }
        }
    }
}

void
WindowManager::handleNetRequestFrameExtents(Window win)
{
    ThemeState *state;
    MwmThemeState mwm_state;
    auto client = Client::findClientFromWindow(win);
    if (client == nullptr) {
        MwmHints hints = {0};
        X11Util::readMwmHints(win, hints);
        mwm_state.setHints(hints);
        state = &mwm_state;
    } else {
        state = static_cast<Frame*>(client->getParent());
    }

    // _NET_REQUEST_FRAME_EXTENTS is not a required to be correct,
    // autoproperties and such are ignored and also the decor type.
    auto data = pekwm::theme()->getPDecorData("DEFAULT");
    if (data) {
        ThemeGm theme_gm(data);

        Cardinal extents[4];
        extents[0] = theme_gm.bdLeft(state);
        extents[1] = theme_gm.bdRight(state);
        extents[2] = theme_gm.bdTop(state) + theme_gm.titleHeight(state);
        extents[3] = theme_gm.bdBottom(state);
        TRACE("setting _NET_FRAME_EXTENTS (" << extents[0] << " "
              << extents[1] << " " << extents[2] << " " << extents[3]
              << ") on " << win);
        X11::setCardinals(win, NET_FRAME_EXTENTS, extents, 4);
    }
}

void
WindowManager::handleColormapEvent(XColormapEvent *ev)
{
    Client *client = Client::findClient(ev->window);
    if (client) {
        client =
            static_cast<Client*>(((Frame*) client->getParent())->getActiveChild());
        client->handleColormapChange(ev);
    }
}

//! @brief Handles XPropertyEvents
void
WindowManager::handlePropertyEvent(XPropertyEvent *ev)
{
    if (ev->window == X11::getRoot()) {
        return pekwm::rootWo()->handlePropertyChange(ev);
    }

    Client *client = Client::findClientFromWindow(ev->window);
    if (client) {
        ((Frame*) client->getParent())->handlePropertyChange(ev, client);
    }
}

//! @brief Handles XMappingEvent
void
WindowManager::handleMappingEvent(XMappingEvent *ev)
{
    if (ev->request == MappingKeyboard || ev->request == MappingModifier) {
        XRefreshKeyboardMapping(ev);
        X11::setLockKeys();
        InputDialog::reloadKeysymMap();
        doReloadKeygrabber(true);
    }
}

void
WindowManager::handleExposeEvent(XExposeEvent *ev)
{
    ActionEvent *ae = 0;

    PWinObj *wo = PWinObj::findPWinObj(ev->window);
    if (wo) {
        ae = wo->handleExposeEvent(ev);
    }

    if (ae) {
        ActionPerformed ap(wo, *ae);
        ap.type = ev->type;
        ap.event.expose = ev;

        pekwm::actionHandler()->handleAction(ap);
    }
}

// Event handling routines stop ============================================

Client*
WindowManager::createClient(Window window, bool is_new)
{
    Client *client = 0;
    ClientInitConfig initConfig;

    XWindowAttributes attr;
    XGetWindowAttributes(X11::getDpy(), window, &attr);
    if (! attr.override_redirect && (is_new || attr.map_state != IsUnmapped)) {
        // We need to figure out whether or not this is a dockapp.
        XWMHints *wm_hints = XGetWMHints(X11::getDpy(), window);
        if (wm_hints) {
            if ((wm_hints->flags&StateHint)
                && (wm_hints->initial_state == WithdrawnState)) {
                pekwm::harbour()->addDockApp(new DockApp(window));
            } else {
                client = new Client(window, initConfig, is_new);
            }
            X11::free(wm_hints);
        } else {
            client = new Client(window, initConfig, is_new);
        }
    }

    if (client) {
        if (client->isAlive()) {
            if (initConfig.parent_is_new) {
                Workspaces::insert(client->getParent(), true);
            }
            Workspaces::updateClientList();

            // Make sure the window is mapped, this is done after it has been
            // added to the decor/frame as otherwise IsViewable state won't
            // be correct and we don't know whether or not to place the window
            if (initConfig.map) {
                client->mapWindow();
            }

            // Focus was requested by the configuration, look out for
            // focus stealing.
            if (initConfig.focus) {
                auto wo = PWinObj::getFocusedPWinObj();
                Time time_protect =
                    static_cast<Time>(pekwm::config()->getFocusStealProtect());

                if (wo != nullptr
                    && wo->isMapped()
                    && wo->isKeyboardInput()
                    && time_protect
                    && time_protect >= (X11::getLastEventTime()
                                        - wo->getLastActivity())) {
                    // WO exists, is mapped, time protect is active
                    // and within it's limits.
                } else {
                    client->getParent()->giveInputFocus();
                }
            }
        } else{
            delete client;
            client = 0;
        }
    }

    return client;
}

void
WindowManager::handlePekwmCmd(XClientMessageEvent *ev)
{
    if (! recvPekwmCmd(ev)) {
        return;
    }

    Action action;
    if (ActionConfig::parseAction(_pekwm_cmd_buf, action, CMD_OK)) {
        ActionEvent ae;
        ae.action_list.push_back(action);

        PWinObj *wo = nullptr;
        if (ev->window != X11::getRoot()) {
            wo = Client::findClient(ev->window);
        }

        ActionPerformed ap(wo, ae);
        pekwm::actionHandler()->handleAction(ap);
    }

    _pekwm_cmd_buf.clear();
}

/**
 * Receive data from XClientMessage building up the _pekwm_cmd_buf,
 * command can be split up in multiple messages due to size
 * restrictions.
 *
 * @return true on complete message, false on error and incomplete message.
 */
bool
WindowManager::recvPekwmCmd(XClientMessageEvent *ev)
{
    size_t last = sizeof(ev->data.b) - 1;
    enum PekwmCmdBuf op = static_cast<enum PekwmCmdBuf>(ev->data.b[last]);
    switch (op) {
    case PEKWM_CMD_SINGLE:
        _pekwm_cmd_buf = ev->data.b;
        return true;
    case PEKWM_CMD_MULTI_FIRST:
        ev->data.b[last] = 0;
        _pekwm_cmd_buf = ev->data.b;
        return false;
    case PEKWM_CMD_MULTI_CONT:
    case PEKWM_CMD_MULTI_END:
        if (_pekwm_cmd_buf.empty()) {
            DBG("invalid _PEKWM_CMD, continuation on empty buffer");
            _pekwm_cmd_buf.clear();
            return false;
        }

        // multi-message command, continuation.
        _pekwm_cmd_buf.append(ev->data.b,
                              std::min(std::strlen(ev->data.b), last));
        if (_pekwm_cmd_buf.size() > 1024) {
            DBG("maximum _PEKWM_CMD message size reached, drop");
            _pekwm_cmd_buf.clear();
            return false;
        }
        return op == PEKWM_CMD_MULTI_END;
    default:
        // invalid data
        DBG("invalid _PEKMW_CMD, last byte " << op << " not in range 0-3");
        _pekwm_cmd_buf.clear();
        return false;
    }
}
