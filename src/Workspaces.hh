//
// Workspaces.hh for pekwm
// Copyright (C) 2002-2020 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#pragma once

#include "config.h"

#include <string>

#include "pekwm.hh"
#include "WinLayouter.hh"
#include "WorkspaceIndicator.hh"

class PWinObj;
class Frame;

class Workspace {
public:
    Workspace() : _name(), _layouter(0), _last_focused(0) { }
    Workspace(const Workspace &w) : _name(w._name), _layouter(w._layouter),
          _last_focused(w._last_focused)
    {
        w._layouter = 0;
    }
    ~Workspace(void);
    Workspace &operator=(const Workspace &w);

    inline const std::wstring &getName(void) const { return _name; }
    inline void setName(const std::wstring &name) { _name = name; }

    inline WinLayouter *getLayouter(void) const {
        return _layouter?_layouter:_default_layouter;
    }
    static void setDefaultLayouter(const std::string &);

    inline PWinObj* getLastFocused(void) const { return _last_focused; }
    inline void setLastFocused(PWinObj* wo) { _last_focused = wo; }

private:
    std::wstring _name;
    mutable WinLayouter *_layouter; // evil hack, will change with C++11
    PWinObj *_last_focused;

    static WinLayouter *_default_layouter;
};

class Workspaces {
public:
    typedef std::vector<PWinObj*>::iterator iterator;
    typedef std::vector<PWinObj*>::const_iterator const_iterator;
    typedef std::vector<PWinObj*>::reverse_iterator reverse_iterator;
    typedef std::vector<PWinObj*>::const_reverse_iterator
        const_reverse_iterator;

    static void init(void);
    static void cleanup(void);

    static inline iterator begin(void) { return _wobjs.begin(); }
    static inline iterator end(void) { return _wobjs.end(); }
    static inline reverse_iterator rbegin(void) { return _wobjs.rbegin(); }
    static inline reverse_iterator rend(void) { return _wobjs.rend(); }

    static inline uint size(void) { return _workspaces.size(); }
    static inline uint getActive(void) { return _active; }
    static inline uint getPrevious(void) { return _previous; }
    static uint getRow(int active = -1) {
        if (active < 0) {
            active = _active;
        }
        return _per_row ? (active / _per_row) : 0;
    }
    static uint getRowMin(void) { return _per_row ? (getRow() * _per_row) : 0; }
    static uint getRowMax(void) {
        return _per_row ? (getRowMin() + _per_row - 1) : size() - 1;
    }
    static uint getRows(void) {
        return _per_row ? (size() / _per_row + (size() % _per_row ? 1 : 0)) : 1;
    }
    static uint getPerRow(void) { return _per_row ? _per_row : size(); }

    static void setSize(uint number);
    static void setPerRow(uint per_row) { _per_row = per_row; }
    static void setNames(void);

    static void setWorkspace(uint num, bool focus);
    static bool gotoWorkspace(uint direction, bool warp);

    static Workspace &getActWorkspace(void) {
        return _workspaces[_active];
    }

    static void layout(Frame *f=0, Window parent=None) {
        _workspaces[_active].getLayouter()->layout(f, parent);
    }

    static void insert(PWinObj* wo, bool raise = true);
    static void remove(PWinObj* wo);

    static void hideAll(uint workspace);
    static void unhideAll(uint workspace, bool focus);

    static PWinObj* getLastFocused(uint workspace);
    static void setLastFocused(uint workspace, PWinObj* wo);

    static void raise(PWinObj* wo);
    static void lower(PWinObj* wo);

    static PWinObj* getTopWO(uint type_mask);
    static void updateClientList(void);
    static void updateClientStackingList(void);
    static void placeWoInsideScreen(PWinObj *wo);

    static void findWOAndFocus(PWinObj *search);
    static PWinObj *findDirectional(PWinObj *wo,
                                    DirectionType dir, uint skip = 0);
    static Frame* getNextFrame(Frame* frame, bool mapped, uint mask = 0);
    static Frame* getPrevFrame(Frame* frame, bool mapped, uint mask = 0);

    static void fixStacking(PWinObj *);

    static void showWorkspaceIndicator(void);
    static void hideWorkspaceIndicator(void);

    // list iterators
    static std::vector<Frame*>::iterator mru_begin(void) {
        return _mru.begin();
    }
    static std::vector<Frame*>::iterator mru_end(void) {
        return _mru.end();
    }

    // adds
    static void addToMRUFront(Frame *frame) {
        if (frame) {
            removeFromMRU(frame);
            _mru.insert(_mru.begin(), frame);
        }
    }

    static void addToMRUBack(Frame *frame) {
        if (frame) {
            removeFromMRU(frame);
            _mru.push_back(frame);
        }
    }

    static void removeFromMRU(Frame *frame) {
        _mru.erase(std::remove(_mru.begin(), _mru.end(), frame),
                   _mru.end());
    }

private:
    static Window *buildClientList(unsigned int &num_windows);
    static bool warpToWorkspace(uint num, int dir);

    static std::wstring getWorkspaceName(uint num);

    static uint _active; /**< Current active workspace. */
    static uint _previous; /**< Previous workspace. */
    static uint _per_row; /**< Workspaces per row in layout. */

    /** Window popping up when switching workspace */
    static WorkspaceIndicator *_workspace_indicator;

    static std::vector<PWinObj*> _wobjs;
    /** The most recently used frame is kept at the front. */
    static std::vector<Frame*> _mru;
    static std::vector<Workspace> _workspaces;
};
