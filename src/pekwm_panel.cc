//
// pekwm_panel.cc for pekwm
// Copyright (C) 2021 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "pekwm.hh"
#include "Debug.hh"
#include "FontHandler.hh"
#include "ImageHandler.hh"
#include "Observable.hh"
#include "Observer.hh"
#include "TextureHandler.hh"
#include "Util.hh"
#include "X11App.hh"
#include "X11Util.hh"
#include "x11.hh"

#include <functional>

extern "C" {
#include <assert.h>
#include <getopt.h>
#include <time.h>
#include <unistd.h>
}

#include "Compat.hh"

#define DEFAULT_FONT "-misc-fixed-medium-r-*-*-13-*-*-*-*-*-iso10646-*"
#define DEFAULT_FONT_FOC "-misc-fixed-bold-r-*-*-13-*-*-*-*-*-iso10646-*"
#define DEFAULT_FONT_ICO "-misc-fixed-medium-o-*-*-13-*-*-*-*-*-iso10646-*"

/**
 * Client state, used for selecting correct theme data for the client
 * list.
 */
enum ClientState {
    CLIENT_STATE_FOCUSED,
    CLIENT_STATE_UNFOCUSED,
    CLIENT_STATE_ICONIFIED,
    CLIENT_STATE_NO
};

/**
 * Widget size unit.
 */
enum WidgetUnit {
    WIDGET_UNIT_PIXELS,
    WIDGET_UNIT_PERCENT,
    WIDGET_UNIT_REQUIRED,
    WIDGET_UNIT_REST,
    WIDGET_UNIT_TEXT_WIDTH
};

/** empty string, used as default return value. */
static std::string _empty_string;
/** empty string, used as default return value. */
static std::wstring _empty_wstring;

/** static pekwm resources, accessed via the pekwm namespace. */
static FontHandler* _font_handler = nullptr;
static ImageHandler* _image_handler = nullptr;
static TextureHandler* _texture_handler = nullptr;

namespace pekwm
{
    FontHandler* fontHandler()
    {
        return _font_handler;
    }

    ImageHandler* imageHandler()
    {
        return _image_handler;
    }

    TextureHandler* textureHandler()
    {
        return _texture_handler;
    }
}

/**
 * Configuration for panel, read from ~/.pekwm/panel by default.
 */
class PanelConfig {
public:
    /**
     * Configuration for commands to be run at given intervals to
     * collect data.
     */
    class CommandConfig {
    public:
        CommandConfig(const std::string& command,
                      uint interval_s)
            : _command(command),
              _interval_s(interval_s)
        {
        }

        const std::string& getCommand(void) const { return _command; }
        uint getIntervalS(void) const { return _interval_s; }

    private:
        /** Command to run (using the shell) */
        std::string _command;
        /** Interval between runs, not including run time. */
        uint _interval_s;
    };

    typedef std::vector<CommandConfig> command_config_vector;
    typedef command_config_vector::const_iterator command_config_it;

    /**
     * Size request for widget.
     */
    class SizeReq {
    public:
        SizeReq(const std::wstring &text)
            : _unit(WIDGET_UNIT_TEXT_WIDTH),
              _size(0),
              _text(text)
        {
        }

        SizeReq(WidgetUnit unit, uint size)
            : _unit(unit),
              _size(size)
        {
        }

        enum WidgetUnit getUnit(void) const { return _unit; }
        uint getSize(void) const { return _size; }
        const std::wstring& getText(void) const { return _text; }

    private:
        WidgetUnit _unit;
        uint _size;
        std::wstring _text;
    };

    /**
     * Widgets to display.
     */
    class WidgetConfig {
    public:
        WidgetConfig(const std::string& name, std::vector<std::string> args,
                     const SizeReq& size_req, uint interval_s = UINT_MAX)
            : _name(name),
              _args(args),
              _size_req(size_req),
              _interval_s(interval_s)
        {
        }

        const std::string& getName(void) const { return _name; }
        const std::string& getArg(uint arg) const
        {
            return arg < _args.size() ? _args[arg] : _empty_string;
        }
        const SizeReq& getSizeReq(void) const { return _size_req; }
        uint getIntervalS(void) const { return _interval_s; }

    private:
        /** Widget type name. */
        std::string _name;
        /** Widget arguments (if any). */
        std::vector<std::string> _args;
        /** Requested size of widget. */
        SizeReq _size_req;
        /** Refresh interval of widgets, set to UINT_MAX for non time
            based widgets. */
        uint _interval_s;
    };

    typedef std::vector<WidgetConfig> widget_config_vector;
    typedef widget_config_vector::const_iterator widget_config_it;

    PanelConfig(void)
    {
    }

    bool load(const std::string &panel_file)
    {
        CfgParser cfg;
        if (! cfg.parse(panel_file, CfgParserSource::SOURCE_FILE, true)) {
            return false;
        }

        auto root = cfg.getEntryRoot();
        loadCommands(root->findSection("COMMANDS"));
        loadWidgets(root->findSection("WIDGETS"));
        _refresh_interval_s = calculateRefreshIntervalS();
        return true;
    }

    uint getRefreshIntervalS(void) const { return _refresh_interval_s; }

    command_config_it commandsBegin(void) const { return _commands.begin(); }
    command_config_it commandsEnd(void) const { return _commands.end(); }

    widget_config_it widgetsBegin(void) const { return _widgets.begin(); }
    widget_config_it widgetsEnd(void) const { return _widgets.end(); }

private:
    void loadCommands(CfgParser::Entry *section)
    {
        _commands.clear();
        if (section == nullptr) {
            return;
        }

        auto it = section->begin();
        for (; it != section->end(); ++it) {
            uint interval = UINT_MAX;

            if ((*it)->getSection()) {
                std::vector<CfgParserKey*> keys;
                keys.push_back(new CfgParserKeyNumeric<uint>("INTERVAL",
                                                             interval,
                                                             UINT_MAX));
                (*it)->getSection()->parseKeyValues(keys.begin(), keys.end());
                std::for_each(keys.begin(), keys.end(),
                              Util::Free<CfgParserKey*>());
            }
            _commands.push_back(CommandConfig((*it)->getValue(), interval));
        }
    }

    void loadWidgets(CfgParser::Entry *section)
    {
        _widgets.clear();
        if (section == nullptr) {
            return;
        }

        auto it = section->begin();
        for (; it != section->end(); ++it) {
            uint interval = UINT_MAX;
            std::string size = "REQUIRED";

            if ((*it)->getSection()) {
                std::vector<CfgParserKey*> keys;
                keys.push_back(new CfgParserKeyNumeric<uint>("INTERVAL",
                                                             interval,
                                                             UINT_MAX));
                keys.push_back(new CfgParserKeyString("SIZE",
                                                      size,
                                                      "REQUIRED"));
                (*it)->getSection()->parseKeyValues(keys.begin(), keys.end());
                std::for_each(keys.begin(), keys.end(),
                              Util::Free<CfgParserKey*>());
            }

            auto size_req = parseSize(size);
            std::vector<std::string> args;
            if (! (*it)->getValue().empty()) {
                args.push_back((*it)->getValue());
            }

            _widgets.push_back(WidgetConfig((*it)->getName(), args,
                                            size_req, interval));
        }
    }

    SizeReq parseSize(const std::string& size)
    {
        std::vector<std::string> toks;
        Util::splitString(size, toks, " \t", 2);
        if (toks.size() == 1) {
            if (strcasecmp("REQUIRED", toks[0].c_str()) == 0) {
                return SizeReq(WIDGET_UNIT_REQUIRED, 0);
            } else if (toks[0] == "*") {
                return SizeReq(WIDGET_UNIT_REST, 0);
            }
        } else if (toks.size() == 2) {
            if (strcasecmp("PIXELS", toks[0].c_str()) == 0) {
                return SizeReq(WIDGET_UNIT_PIXELS, atoi(toks[1].c_str()));
            } else if (strcasecmp("PERCENT", toks[0].c_str()) == 0) {
                return SizeReq(WIDGET_UNIT_PERCENT, atoi(toks[1].c_str()));
            } else if (strcasecmp("TEXTWIDTH", toks[0].c_str()) == 0) {
                return SizeReq(Util::to_wide_str(toks[1]));
            }
        }

        USER_WARN("failed to parse size: " << size);
        return SizeReq(WIDGET_UNIT_REQUIRED, 0);
    }

    uint calculateRefreshIntervalS(void) const
    {
        uint min = UINT_MAX;
        for (auto it : _commands) {
            if (it.getIntervalS() < min) {
                min = it.getIntervalS();
            }
        }
        for (auto it : _widgets) {
            if (it.getIntervalS() < min) {
                min = it.getIntervalS();
            }
        }
        return min;
    }

private:
    /** List of commands to run. */
    command_config_vector _commands;
    /** List of widgets to instantiate. */
    std::vector<WidgetConfig> _widgets;
    /** At what given interval is refresh required at a minimum. */
    uint _refresh_interval_s;
};

/**
 * Collection of data from external commands.
 *
 * Commands output is collected in the below format at the specified
 * interval.
 *
 * key data
 *
 */
class ExternalCommandData : public Observable
{
public:
    class FieldObservation : public Observation
    {
    public:
        FieldObservation(const std::string field)
            : _field(field)
        {
        }
        virtual ~FieldObservation(void) { }

        const std::string& getField(void) const { return _field; }

    private:
        std::string _field;
    };

    class CommandProcess
    {
    public:
        CommandProcess(const std::string& command, uint interval_s)
            : _command(command),
              _interval_s(interval_s),
              _pid(-1),
              _fd(-1)
        {
            // set next interval to last second to ensure immediate
            // execution
            int ret = clock_gettime(CLOCK_MONOTONIC, &_next_interval);
            assert(ret == 0);
            _next_interval.tv_sec--;
        }

        ~CommandProcess(void)
        {
            if (_fd != -1) {
                close(_fd);
            }
        }

        int getFd(void) const { return _fd; }
        pid_t getPid(void) const { return _pid; }
        const std::string& getBuf(void) const { return _buf; }

        void append(char *data, ssize_t size)
        {
            _buf.append(data, data + size);
        }

        bool start(void)
        {
            int fd[2];
            int ret = pipe(fd);
            if (ret == -1) {
                ERR("pipe failed due to: " << strerror(errno));
                return false;
            }

            _pid = fork();
            if (_pid == -1) {
                close(fd[0]);
                close(fd[1]);
                ERR("fork failed due to: " << strerror(errno));
                return false;
            } else if (_pid == 0) {
                // child, dup write end of file descriptor to stdout
                dup2(fd[1], STDOUT_FILENO);

                close(fd[0]);
                close(fd[1]);

                execlp("/bin/sh", "sh", "-c", _command.c_str(), nullptr);

                ERR("failed to execute: " << _command);

                close(STDOUT_FILENO);
                exit(1);
            }

            // parent, close write end just going to read
            _fd = fd[0];
            close(fd[1]);
            Util::setNonBlock(_fd);
            TRACE("pid " << _pid << " started with fd " << _fd
                  << " for command " << _command);
            return true;
        }

        bool checkInterval(struct timespec *now)
        {
            return now->tv_sec >= _next_interval.tv_sec;
        }

        void reset(void)
        {
            _pid = -1;
            _fd = -1;
            _buf.clear();

            int ret = clock_gettime(CLOCK_MONOTONIC, &_next_interval);
            assert(ret == 0);
            _next_interval.tv_sec += _interval_s;
        }

    private:
        std::string _command;
        uint _interval_s;
        struct timespec _next_interval;

        pid_t _pid;
        int _fd;
        std::string _buf;
    };

    ExternalCommandData(const PanelConfig& cfg)
        : _cfg(cfg)
    {
        auto it = _cfg.commandsBegin();
        for (; it != _cfg.commandsEnd(); ++it) {
            _command_processes.push_back(CommandProcess(it->getCommand(),
                                                        it->getIntervalS()));
        }
    }

    const std::wstring& get(const std::string& field) const
    {
        auto it = _fields.find(field);
        return it == _fields.end() ? _empty_wstring : it->second;
    }

    void refresh(std::function<void(int)> addFd)
    {
        struct timespec now;
        int ret = clock_gettime(CLOCK_MONOTONIC, &now);
        assert(ret == 0);

        auto it = _command_processes.begin();
        for (; it != _command_processes.end(); ++it) {
            if (it->getPid() == -1
                && it->checkInterval(&now)
                && it->start()) {
                addFd(it->getFd());
            }
        }
    }

    bool input(int fd)
    {
        char buf[1024];
        ssize_t nread = read(fd, buf, sizeof(buf));
        if (nread == -1) {
            TRACE("failed to read from " << fd << ": " << strerror(errno));
            return false;
        }

        auto it = _command_processes.begin();
        for (; it != _command_processes.end(); ++it) {
            if (it->getFd() == fd) {
                it->append(buf, nread);
                break;
            }
        }
        return nread > 0;
    }

    void done(pid_t pid, std::function<void(int)> removeFd)
    {
        auto it = _command_processes.begin();
        for (; it != _command_processes.end(); ++it) {
            if (it->getPid() == pid) {
                while (input(it->getFd())) {
                    // read data left in pipe if any
                }
                parseOutput(it->getBuf());
                removeFd(it->getFd());

                // clean up state, resetting timer and pid/fd
                it->reset();
                break;
            }
        }
    }

private:
    void parseOutput(const std::string& buf)
    {
        std::vector<std::string> lines;
        std::vector<std::string> field_value;
        Util::splitString(buf, lines, "\n");
        for (auto line : lines) {
            field_value.clear();
            if (Util::splitString(line, field_value, " \t", 2) == 2) {
                _fields[field_value[0]] = Util::to_wide_str(field_value[1]);
                FieldObservation field_obs(field_value[0]);
                notifyObservers(&field_obs);
            }
        }
    }

private:
    const PanelConfig& _cfg;

    std::map<std::string, std::wstring> _fields;
    std::vector<CommandProcess> _command_processes;
};

/**
 * Current window manager state.
 */
class WmState : public Observable
{
public:
    class ClientInfo : public NetWMStates{
    public:
        ClientInfo(Window window)
            : _window(window)
        {
            _name = readName();
            _gm = readGeometry();
            _workspace = readWorkspace();
            X11Util::readEwmhStates(_window, *this);
        }

        Window getWindow(void) const { return _window; }
        const std::wstring& getName(void) const { return _name; }
        const Geometry& getGeometry(void) const { return _gm; }

        bool displayOn(uint workspace) const
        {
            return sticky || this->_workspace == workspace;
        }

    private:
        std::wstring readName(void)
        {
            std::string name;
            if (X11::getUtf8String(_window, NET_WM_NAME, name)) {
                return Util::from_utf8_str(name);
            }
            if (X11::getTextProperty(_window, XA_WM_NAME, name)) {
                return Util::to_wide_str(name);
            }
            return L"";
        }

        Geometry readGeometry(void)
        {
            return Geometry();
        }

        uint readWorkspace(void)
        {
            long workspace;
            if (! X11::getLong(_window, NET_WM_DESKTOP, workspace)) {
                TRACE("failed to read _NET_WM_DESKTOP on " << _window
                      << " using 0");
                return 0;
            }
            return workspace;
        }

     private:
        Window _window;
        std::wstring _name;
        Geometry _gm;
        uint _workspace;
    };

    typedef std::vector<ClientInfo> client_info_vector;
    typedef client_info_vector::const_iterator client_info_it;

    WmState(void)
        : _active_window(None),
          _workspace(0)
    {
    }
    virtual ~WmState(void) { }

    void read(void)
    {
        readActiveWorkspace();
        readActiveWindow();
        readClientListStacking();
    }

    uint getActiveWorkspace(void) const { return _workspace; }
    Window getActiveWindow(void) const { return _active_window; }

    uint numClients(void) const { return _clients.size(); }
    client_info_it clientsBegin(void) const { return _clients.begin(); }
    client_info_it clientsEnd(void) const { return _clients.end(); }

    bool handlePropertyNotify(XPropertyEvent *ev)
    {
        bool updated = false;

        if (ev->window == X11::getRoot()) {
            if (ev->atom == X11::getAtom(NET_CURRENT_DESKTOP)) {
                updated = readActiveWorkspace();
            } else if (ev->atom == X11::getAtom(NET_ACTIVE_WINDOW)) {
                updated = readActiveWindow();
            } else if (ev->atom == X11::getAtom(NET_CLIENT_LIST)) {
                updated = readClientListStacking();
            }
        }

        if (updated) {
            notifyObservers(nullptr);
        }

        return updated;
    }

private:
    bool readActiveWorkspace(void)
    {
        long workspace;
        if (! X11::getLong(X11::getRoot(), NET_CURRENT_DESKTOP, workspace)) {
            TRACE("failed to read _NET_CURRENT_DESKTOP, setting to 0");
            _workspace = 0;
            return false;
        }
        _workspace = workspace;
        return true;
    }

    bool readActiveWindow(void)
    {
        if (! X11::getWindow(X11::getRoot(),
                             NET_ACTIVE_WINDOW, _active_window)) {
            TRACE("failed to read _NET_ACTIVE_WINDOW, setting to None");
            _active_window = None;
            return false;
        }
        return true;
    }

    bool readClientListStacking(void)
    {
        ulong actual;
        Window *windows;
        if (! X11::getProperty(X11::getRoot(),
                               X11::getAtom(NET_CLIENT_LIST),
                               XA_WINDOW, 0,
                               reinterpret_cast<uchar**>(&windows), &actual)) {
            TRACE("failed to read _NET_CLIENT_LIST");
            return false;
        }

        // FIXME: only add new clients to avoid re-reading geometry and name.

        _clients.clear();
        for (uint i = 0; i < actual; i++) {
            _clients.push_back(ClientInfo(windows[i]));
        }

        X11::free(windows);

        TRACE("read _NET_CLIENT_LIST, " << actual << " windows");
        return true;
    }

private:
    Window _active_window;
    uint _workspace;
    std::vector<WmState::ClientInfo> _clients;
};

/**
 * Theme for the panel and its widgets.
 */
class PanelTheme {
public:
    PanelTheme(void)
    {
        auto fh = pekwm::fontHandler();
        _fonts[CLIENT_STATE_UNFOCUSED] = fh->getFont(DEFAULT_FONT "#Center");
        _fonts[CLIENT_STATE_FOCUSED] =
            fh->getFont(DEFAULT_FONT_FOC "#Center");
        _fonts[CLIENT_STATE_ICONIFIED] =
            fh->getFont(DEFAULT_FONT_ICO "#Center");
        _color = pekwm::fontHandler()->getColor("#000000");

        auto th = pekwm::textureHandler();
        _background = th->getTexture("Solid #ffffff ");
        _sep = th->getTexture("Solid #aaaaaa 1x24");

        for (int i = 0; i < CLIENT_STATE_NO; i++) {
            _fonts[i]->setColor(_color);
        }
    }
    ~PanelTheme(void)
    {
        auto th = pekwm::textureHandler();
        th->returnTexture(_sep);

        auto fh = pekwm::fontHandler();
        fh->returnColor(_color);
        for (int i = 0; i < CLIENT_STATE_NO; i++) {
            fh->returnFont(_fonts[i]);
        }
    }

    PFont *getFont(ClientState state) const { return _fonts[state]; }
    PTexture *getBackground(void) const { return _background; }
    PTexture *getSep(void) const { return _sep; }

private:
    PFont* _fonts[CLIENT_STATE_NO];
    PFont::Color* _color;
    PTexture *_background;
    PTexture *_sep;
};

/**
 * Base class for all widgets displayed on the panel.
 */
class PanelWidget {
public:

    PanelWidget(const PanelTheme &theme, const PanelConfig::SizeReq& size_req)
        : _theme(theme),
          _dirty(true),
          _x(0),
          _width(0),
          _size_req(size_req)
    {
    }

    virtual ~PanelWidget(void) { }

    bool isDirty(void) const { return _dirty; }
    int getX(void) const { return _x; }
    void move(int x) { _x = x; }

    uint getWidth(void) const { return _width; }
    void setWidth(uint width) { _width = width; }

    const PanelConfig::SizeReq& getSizeReq(void) const { return _size_req; }
    virtual uint getRequiredSize(void) const { return 0; }

    virtual void render(Drawable draw)
    {
        X11::clearArea(draw, _x, 0, _width - 1, 24);
        _dirty = false;
    }

protected:
    const PanelTheme& _theme;
    bool _dirty;

private:
    int _x;
    uint _width;
    PanelConfig::SizeReq _size_req;
};

/**
 * Current Date/Time formatted using strftime.
 */
class DateTimeWidget : public PanelWidget {
public:
    DateTimeWidget(const PanelTheme &theme,
                   const PanelConfig::SizeReq& size_req,
                   const std::string &format)
        : PanelWidget(theme, size_req),
          _format(format)
    {
        if (_format.empty()) {
            _format = "%Y-%m-%d %H:%M";
        }
    }

    virtual uint getRequiredSize(void) const override
    {
        std::wstring wtime;
        formatNow(wtime);
        auto font = _theme.getFont(CLIENT_STATE_UNFOCUSED);
        return font->getWidth(L" " + wtime + L" ");
    }

    virtual void render(Drawable draw)
    {
        PanelWidget::render(draw);

        std::wstring wtime;
        formatNow(wtime);
        auto font = _theme.getFont(CLIENT_STATE_UNFOCUSED);
        font->draw(draw, getX(), 1, wtime, 0, getWidth());

        // always treat date time as dirty, requires redraw up to
        // every second.
        _dirty = true;
    }

private:
    void formatNow(std::wstring &res) const
    {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);

        char buf[64];
        strftime(buf, sizeof(buf), _format.c_str(), &tm);
        res = Util::to_wide_str(buf);
    }

private:
    std::string _format;
};

/**
 * List of Frames/Clients on the current workspace.
 */
class ClientListWidget : public PanelWidget,
                         public Observer{
public:
    ClientListWidget(const PanelTheme& theme,
                     const PanelConfig::SizeReq& size_req,
                     WmState& wm_state)
        : PanelWidget(theme, size_req),
          _wm_state(wm_state)
    {
        _wm_state.addObserver(this);
    }

    virtual ~ClientListWidget(void)
    {
        _wm_state.removeObserver(this);
    }

    virtual void notify(Observable *observable,
                        Observation *observation) override
    {
        _dirty = true;
    }

    virtual void render(Drawable draw)
    {
        PanelWidget::render(draw);

        uint workspace = _wm_state.getActiveWorkspace();

        // divide width equally between all clients
        uint num_clients = 0;
        auto it = _wm_state.clientsBegin();
        for (; it != _wm_state.clientsEnd(); ++it) {
            if (it->displayOn(workspace)) {
                num_clients++;
            }
        }

        // no clients on active workspace, skip rendering and avoid
        // division by zero.
        if (! num_clients) {
            return;
        }
        uint client_width = getWidth() / num_clients;;

        int x = getX();
        it = _wm_state.clientsBegin();
        for (; it != _wm_state.clientsEnd(); ++it) {
            if (! it->displayOn(workspace)) {
                continue;
            }
            PFont *font;
            if (it->getWindow() == _wm_state.getActiveWindow()) {
                font = _theme.getFont(CLIENT_STATE_FOCUSED);
            } else if (it->hidden) {
                font = _theme.getFont(CLIENT_STATE_ICONIFIED);
            } else {
                font = _theme.getFont(CLIENT_STATE_UNFOCUSED);
            }
            font->draw(draw, x, 1, it->getName(), 0, client_width);
            x += client_width;
        }
    }

private:
    WmState& _wm_state;
};

/**
 * Widget displaying a data field from the output of the defined
 * commands in this panel.
 */
class ExternalDataWidget : public PanelWidget,
                           public Observer{
public:
    ExternalDataWidget(const PanelTheme& theme,
                       const PanelConfig::SizeReq& size_req,
                       ExternalCommandData& ext_data,
                       const std::string& field)
        : PanelWidget(theme, size_req),
          _ext_data(ext_data),
          _field(field)
    {
        _ext_data.addObserver(this);
    }

    virtual ~ExternalDataWidget(void)
    {
        _ext_data.removeObserver(this);
    }

    virtual void notify(Observable *observable,
                        Observation *observation) override
    {
        auto efo =
            dynamic_cast<ExternalCommandData::FieldObservation*>(observation);
        if (efo != nullptr && efo->getField() == _field) {
            _dirty = true;
        }
    }

    virtual void render(Drawable draw)
    {
        PanelWidget::render(draw);

        auto data = _ext_data.get(_field);
        auto font = _theme.getFont(CLIENT_STATE_UNFOCUSED);
        font->draw(draw, getX(), 1, data, 0, getWidth());
    }

private:
    ExternalCommandData& _ext_data;
    std::string _field;
};

/**
 * Simple widget displaying the active workspace number.
 */
class WorkspaceNumberWidget : public PanelWidget,
                              public Observer {
public:
    WorkspaceNumberWidget(const PanelTheme& theme,
                          const PanelConfig::SizeReq& size_req,
                          WmState& wm_state)
        : PanelWidget(theme, size_req),
          _wm_state(wm_state)
    {
        _wm_state.addObserver(this);
    }

    virtual ~WorkspaceNumberWidget(void)
    {
        _wm_state.removeObserver(this);
    }

    virtual void notify(Observable *observable,
                        Observation *observation) override
    {
        _dirty = true;
    }

    virtual uint getRequiredSize(void) const override
    {
        auto font = _theme.getFont(CLIENT_STATE_UNFOCUSED);
        return font->getWidth(L" 00 ");
    }

    virtual void render(Drawable draw)
    {
        PanelWidget::render(draw);

        auto ws = std::to_string(_wm_state.getActiveWorkspace() + 1);
        auto font = _theme.getFont(CLIENT_STATE_UNFOCUSED);
        font->draw(draw, getX(), 1, Util::to_wide_str(ws), 0, getWidth());
    }

private:
    WmState& _wm_state;
};

/**
 * Widget construction.
 */
class WidgetFactory {
public:
    WidgetFactory(const PanelTheme& theme,
                  ExternalCommandData& ext_data,
                  WmState& wm_state)
        : _theme(theme),
          _ext_data(ext_data),
          _wm_state(wm_state)
    {
    }

    PanelWidget* construct(const PanelConfig::WidgetConfig& cfg)
    {
        std::string name = cfg.getName();
        Util::to_upper(name);

        if (name == "DATETIME") {
            auto format = cfg.getArg(0);
            return new DateTimeWidget(_theme, cfg.getSizeReq(), format);
        } else if (name == "CLIENTLIST") {
            return new ClientListWidget(_theme, cfg.getSizeReq(), _wm_state);
        } else if (name == "EXTERNALDATA") {
            auto field = cfg.getArg(0);
            if (field.empty()) {
                USER_WARN("missing required argument to ExternalData widget");
            } else {
                return new ExternalDataWidget(_theme, cfg.getSizeReq(),
                                              _ext_data, field);
            }
        } else if (name == "WORKSPACENUMBER") {
            return new WorkspaceNumberWidget(_theme, cfg.getSizeReq(),
                                             _wm_state);
        } else {
            USER_WARN("unknown widget " << cfg.getName());
        }

        return nullptr;
    }

private:
    const PanelTheme& _theme;
    ExternalCommandData& _ext_data;
    WmState& _wm_state;
};

/**
 * Widgets in the panel are given a size when configured, can be given
 * in:
 *
 *   * pixels, number of pixels
 *   * percent, percent of the screen width the panel is on.
 *   * required, minimum required size.
 *   * *, all space not occupied by the other widgets. All * share the remaining
 *     space.
 *
 *       required            300px                         *
 * ----------------------------------------------------------------------------
 * | [WorkspaceNumber] | [ExternalData] | [ClientList]                        |
 * ----------------------------------------------------------------------------
 *
 */
class PekwmPanel : public X11App {
public:
    PekwmPanel(const PanelConfig &cfg, const PanelTheme &theme, XSizeHints *sh)
        : X11App({sh->x, sh->y,
                  static_cast<uint>(sh->width), static_cast<uint>(sh->height)},
                 L"", "panel", "pekwm_panel",
                 WINDOW_TYPE_DOCK, sh),
          _cfg(cfg),
          _theme(theme),
          _ext_data(cfg),
          _pixmap(X11::createPixmap(sh->width, sh->height))
    {
        renderBackground();
        X11::setWindowBackgroundPixmap(_window, _pixmap);

        Atom state[] = {
            X11::getAtom(STATE_STICKY),
            X11::getAtom(STATE_SKIP_TASKBAR),
            X11::getAtom(STATE_SKIP_PAGER),
            X11::getAtom(STATE_ABOVE)
        };
        X11::setAtoms(_window, STATE, state, sizeof(state)/sizeof(state[0]));

        long strut[] = {0, 0, sh->height, 0};
        X11::setLongs(_window, NET_WM_STRUT, strut, 4);

        // select root window for atom changes _before_ reading state
        // ensuring state is up-to-date at all times.
        X11::selectInput(X11::getRoot(), PropertyChangeMask);

        _wm_state.read();
    }

    ~PekwmPanel(void)
    {
        for (auto it : _widgets) {
            delete it;
        }
        X11::freePixmap(_pixmap);
    }

    const PanelConfig& getCfg(void) const { return _cfg; }

    void configure(void)
    {
        addWidgets();
        resizeWidgets();
    }

    void render(void)
    {
        auto sep = _theme.getSep();

        PanelWidget *last_widget = nullptr;
        for (auto it : _widgets) {
            if (last_widget) {
                sep->render(_window,
                            it->getX() - sep->getWidth(), 0,
                            sep->getWidth(), sep->getHeight());
            }
            if (it->isDirty()) {
                it->render(_window);
            }
        }
    }

    void renderBackground(void)
    {
        _theme.getBackground()->render(_pixmap, 0, 0, _gm.width, _gm.height);
    }

    virtual void refresh(void) override
    {
        _ext_data.refresh([this](int fd) { this->addFd(fd); });
        render();
    }

    virtual void handleEvent(XEvent *ev) override
    {
        switch (ev->type) {
        case ButtonPress:
            TRACE("ButtonPress");
            break;
        case ButtonRelease:
            TRACE("ButtonRelease");
            break;
        case ConfigureNotify:
            TRACE("ConfigureNotify");
            break;
        case EnterNotify:
            TRACE("EnterNotify");
            break;
        case LeaveNotify:
            TRACE("LeaveNotify");
            break;
        case MapNotify:
            TRACE("MapNotify");
            break;
        case ReparentNotify:
            TRACE("ReparentNotify");
            break;
        case UnmapNotify:
            TRACE("UnmapNotify");
            break;
        case PropertyNotify:
            handlePropertyNotify(&ev->xproperty);
            break;
        default:
            DBG("UNKNOWN EVENT " << ev->type);
            break;
        }
    }

    void handlePropertyNotify(XPropertyEvent *ev)
    {
        if (_wm_state.handlePropertyNotify(ev)) {
            render();
        }
    }

    virtual void handleFd(int fd) override
    {
        _ext_data.input(fd);
    }

    virtual void handleChildDone(pid_t pid, int code)
    {
        _ext_data.done(pid, [this](int fd) { this->removeFd(fd); });
    }

private:
    void addWidgets(void)
    {
        WidgetFactory factory(_theme, _ext_data, _wm_state);

        auto it = _cfg.widgetsBegin();
        for (; it != _cfg.widgetsEnd(); ++it) {
            auto widget = factory.construct(*it);
            if (widget == nullptr) {
                USER_WARN("");
            } else {
                _widgets.push_back(widget);
            }
        }
    }

    void resizeWidgets(void)
    {
        uint num_rest = 0;
        uint width_left = _gm.width;
        for (auto it : _widgets) {
            switch (it->getSizeReq().getUnit()) {
            case WIDGET_UNIT_PIXELS:
                width_left -= it->getSizeReq().getSize();
                it->setWidth(it->getSizeReq().getSize());
                break;
            case WIDGET_UNIT_PERCENT: {
                uint width =
                    _gm.width
                    * (static_cast<float>(it->getSizeReq().getSize()) / 100);
                width_left -= width;
                it->setWidth(width);
                break;
            }
            case WIDGET_UNIT_REQUIRED:
                width_left -= it->getRequiredSize();
                it->setWidth(it->getRequiredSize());
                break;
            case WIDGET_UNIT_REST:
                num_rest++;
                break;
            case WIDGET_UNIT_TEXT_WIDTH: {
                auto font = _theme.getFont(CLIENT_STATE_UNFOCUSED);
                uint width = font->getWidth(it->getSizeReq().getText());
                width_left -= width;
                it->setWidth(width);
                break;
            }
            }
        }

        uint x = 0;
        uint rest = width_left / static_cast<float>(num_rest);
        for (auto it : _widgets) {
            if (it->getSizeReq().getUnit() == WIDGET_UNIT_REST) {
                it->setWidth(rest);
            }
            it->move(x);
            x += it->getWidth();
        }
    }

private:
    const PanelConfig& _cfg;
    const PanelTheme& _theme;
    WmState _wm_state;
    std::vector<PanelWidget*> _widgets;
    ExternalCommandData _ext_data;
    Pixmap _pixmap;
};

static bool loadConfig(PanelConfig& cfg, const std::string& file)
{
    if (file.size() && cfg.load(file)) {
        return true;
    }

    std::string panel_config = Util::getEnv("HOME") + "/.pekwm/panel";
    if (cfg.load(panel_config)) {
        return true;
    }

    return cfg.load(SYSCONFDIR "/panel");
}

static void init(Display* dpy)
{
    _font_handler = new FontHandler();
    _image_handler = new ImageHandler();
    _texture_handler = new TextureHandler();
}

static void cleanup()
{
    delete _texture_handler;
    delete _image_handler;
    delete _font_handler;
}

static void usage(const char* name, int ret)
{
    std::cout << "usage: " << name << " [-dh]" << std::endl
              << " -c --config path    Configuration file" << std::endl
              << " -d --display dpy    Display" << std::endl
              << " -h --help           Display this information" << std::endl
              << " -f --log-file       Set log file." << std::endl
              << " -l --log-level      Set log level." << std::endl;
    exit(ret);
}

int main(int argc, char *argv[])
{
    std::string config;
    const char* display = NULL;

    static struct option opts[] = {
        {"config", required_argument, NULL, 'c'},
        {"display", required_argument, NULL, 'd'},
        {"help", no_argument, NULL, 'h'},
        {"log-level", required_argument, NULL, 'l'},
        {"log-file", required_argument, NULL, 'f'},
        {NULL, 0, NULL, 0}
    };

    try {
        std::locale::global(std::locale(""));
    } catch (const std::runtime_error &e) {
        setlocale(LC_ALL, "");
    }

    Util::iconv_init();

    char ch;
    while ((ch = getopt_long(argc, argv, "c:d:f:hl:", opts, NULL)) != -1) {
        switch (ch) {
        case 'c':
            config = optarg;
            break;
        case 'd':
            display = optarg;
            break;
        case 'h':
            usage(argv[0], 0);
            break;
        case 'f':
            if (Debug::setLogFile(optarg)) {
                Debug::enable_logfile = true;
            } else {
                std::cerr << "Failed to open log file " << optarg << std::endl;
            }
            break;
        case 'l':
            Debug::level = Debug::getLevel(optarg);
            break;
        default:
            usage(argv[0], 1);
            break;
        }
    }

    auto dpy = XOpenDisplay(display);
    if (! dpy) {
        auto actual_display = display ? display : Util::getEnv("DISPLAY");
        std::cerr << "Can not open display!" << std::endl
                  << "Your DISPLAY variable currently is set to: "
                  << actual_display << std::endl;
        return 1;
    }

    X11::init(dpy, true);
    init(dpy);

    {
        // run in separate scope to get resources cleaned up before
        // X11 and iconv cleanup
        PanelConfig cfg;
        PanelTheme theme; // FIXME: read panel theme from active theme
        if (loadConfig(cfg, config)) {
            XSizeHints normal_hints = {0};
            normal_hints.flags = PPosition|PSize;
            normal_hints.x = 0;
            normal_hints.y = 0;
            normal_hints.width = X11::getWidth();
            normal_hints.height = 24;

            PekwmPanel panel(cfg, theme, &normal_hints);
            panel.configure();
            panel.mapWindow();
            panel.render();
            panel.main(cfg.getRefreshIntervalS());
        } else {
            std::cerr << "failed to read panel configuration" << std::endl;
        }
    }

    cleanup();
    X11::destruct();
    Util::iconv_deinit();

    return 0;
}
