//
// AutoProperties.hh for pekwm
// Copyright (C) 2003-2021 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#pragma once

#include "config.h"

#include "pekwm.hh"
#include "CfgParser.hh"
#include "ImageHandler.hh"
#include "PImageIcon.hh"
#include "RegexString.hh"
#include "X11.hh"

#include <string>

/**
 * Bitmask with different auto property types, used to identify what
 * properties has been set in Property.
 */
enum PropertyType {
    AP_STICKY = (1L << 1),
    AP_SHADED = (1L << 2),
    AP_MAXIMIZED_VERTICAL = (1L << 3),
    AP_MAXIMIZED_HORIZONTAL = (1L << 4),
    AP_ICONIFIED = (1L << 5),
    AP_BORDER = (1L << 6),
    AP_TITLEBAR = (1L << 7),
    AP_FRAME_GEOMETRY = (1L << 8),
    AP_CLIENT_GEOMETRY = (1L << 9),
    AP_LAYER = (1L << 10),
    AP_WORKSPACE = (1L << 11),
    AP_SKIP = (1L << 12),
    AP_FULLSCREEN = (1L << 13),
    AP_PLACE_NEW = (1L << 14),
    AP_FOCUS_NEW = (1L << 15),
    AP_FOCUSABLE = (1L << 16),
    AP_CFG_DENY = (1L << 17),
    AP_ALLOWED_ACTIONS = (1L << 18),
    AP_DISALLOWED_ACTIONS = (1L << 19),
    AP_OPACITY = (1L << 20),
    AP_DECOR = (1L << 21),
    AP_ICON = (1L << 22),

    AP_GROUP_SIZE,
    AP_GROUP_BEHIND,
    AP_GROUP_FOCUSED_FIRST,
    AP_GROUP_GLOBAL,
    AP_GROUP_RAISE,

    AP_PROPERTY,
    AP_NO_PROPERTY
};

/**
 * ClassHint holds information from a window required to identify it.
 */
class ClassHint {
public:
    ClassHint(void);
    ClassHint(const std::wstring &n_h_name, const std::wstring &n_h_class,
              const std::wstring &n_h_role, const std::wstring &n_title,
              const std::wstring &n_group);
    ~ClassHint(void);

    ClassHint& operator=(const ClassHint& rhs);
    bool operator==(const ClassHint& rhs) const;

    friend std::ostream& operator<<(std::ostream& os, const ClassHint &ch);

public:
    std::wstring h_name; /**< name part of WM_CLASS hint. */
    std::wstring h_class; /**< class part of WM_CLASS hint. */
    std::wstring h_role; /**< WM_ROLE hint value. */
    std::wstring title; /**< Title of window. */
    std::wstring group; /**< Group window belongs to. */
};

/**
 * Base class for auto properties, includes base matching information.
 */
class Property {
public:
    Property(void);
    virtual ~Property(void);

    inline RegexString& getHintName(void) { return _hint_name; }
    inline RegexString& getHintClass(void) { return _hint_class; }
    inline RegexString& getRole(void) { return _role; }
    inline RegexString& getTitle(void) { return _title; }

    inline uint getApplyOn(void) const { return _apply_mask; }

    inline bool isApplyOn(uint mask) const { return (_apply_mask&mask); }
    inline void applyAdd(uint mask) { _apply_mask |= mask; }
    inline void applyRemove(uint mask) { _apply_mask &= ~mask; }

    inline void setWorkspaces(const std::vector<uint> &ws) { _workspaces=ws; }
    inline bool applyOnWs(uint ws) {
        auto it(find(_workspaces.begin(), _workspaces.end(), ws));
        return _workspaces.empty() || it != _workspaces.end();
    }

private:
    RegexString _hint_name;
    RegexString _hint_class;
    RegexString _role;
    RegexString _title;

    uint _apply_mask;
    std::vector<uint> _workspaces;
};

// AutoProperty for everything except title rewriting
class AutoProperty : public Property
{
public:
    AutoProperty(void);
    virtual ~AutoProperty(void);

    inline bool isMask(uint mask) { return (_prop_mask&mask); }
    inline void maskAdd(uint mask) { _prop_mask |= mask; }
    inline void maskRemove(uint mask) { _prop_mask &= ~mask; }

public:
    Geometry frame_gm;
    Geometry client_gm;
    int frame_gm_mask;
    int client_gm_mask;

    bool sticky;
    bool shaded;
    bool iconified;
    bool maximized_vertical;
    bool maximized_horizontal;
    bool fullscreen;
    bool border;
    bool titlebar;
    bool focusable;
    bool place_new;
    bool focus_new;
    uint workspace;
    uint skip;
    uint cfg_deny;
    Layer layer;
    uint focus_opacity;
    uint unfocus_opacity;
    uint allowed_actions;
    uint disallowed_actions;

    std::string frame_decor;
    PImageIcon *icon;

    // grouping variables
    int group_size;
    std::wstring group_name;
    bool group_behind;
    bool group_focused_first;
    bool group_global;
    bool group_raise;

private:
    uint _prop_mask;
};

// TitleProperty for title rewriting
class TitleProperty : public Property
{
public:
    TitleProperty(void) { }
    virtual ~TitleProperty(void) { }

    RegexString& getTitleRule(void) { return _title_rule; }

private:
    RegexString _title_rule;
};

// DecorProperty for multiple decor types
class DecorProperty : public Property
{
public:
    DecorProperty(void) { }
    virtual ~DecorProperty(void) { }

    inline const std::string &getName(void) const { return _decor_name; }
    inline void setName(const std::string &name) { _decor_name = name; }

private:
    std::string _decor_name;
};

// DockApp for Harbour sorting
class DockAppProperty : public Property
{
public:
    DockAppProperty(void) : _position(0) { }
    virtual ~DockAppProperty(void) { }

    inline int getPosition(void) const { return _position; }
    inline void setPosition(int position) { _position = position; }

private:
    int _position;
};

class AutoProperties {
public:
    AutoProperties(ImageHandler *image_handler);
    ~AutoProperties(void);

    AutoProperty* findAutoProperty(const ClassHint* class_hintbb,
                                   int ws = -1, ApplyOn type = APPLY_ON_ALWAYS);
    TitleProperty* findTitleProperty(const ClassHint* class_hint);
    DecorProperty* findDecorProperty(const ClassHint* class_hint);
    DockAppProperty* findDockAppProperty(const ClassHint *class_hint);
    inline bool isHarbourSort(void) const { return _harbour_sort; }

    AutoProperty *findWindowTypeProperty(AtomName atom);

    bool load(void);
    void unload(void);

    void removeApplyOnStart(void);

    static bool matchAutoClass(const ClassHint &hint, Property *prop);

private:
    Property* findProperty(const ClassHint* class_hint,
                           std::vector<Property*>* prop_list,
                           uint ws, ApplyOn type);

    void loadRequire(CfgParser &a_cfg, std::string &file);

    bool parsePropertyMatch(const std::string &str, Property *prop);
    void parsePropertyApplyOn(const std::string &apply_on, Property *prop);
    bool parseRegexpOrWarning(RegexString &regex, const std::string regex_str,
                              const std::string &name);
    bool parseProperty(CfgParser::Entry *section, Property *prop);
    void parseAutoProperty(CfgParser::Entry *section, std::vector<uint> *ws);
    void parseAutoGroup(CfgParser::Entry *section, AutoProperty* prop);
    void parseTitleProperty(CfgParser::Entry *section);
    void parseDecorProperty(CfgParser::Entry *section);

    void parseAutoPropertyValue(CfgParser::Entry *section, AutoProperty *prop,
                                std::vector<uint> *ws);

    void parseDockAppProperty(CfgParser::Entry *section);
    void parseTypeProperty(CfgParser::Entry *section);

    void setDefaultTypeProperties(void);

private:
    ImageHandler *_image_handler;

    TimeFiles _cfg_files;
    bool _extended; /**< Extended syntax enabled for autoproperties? */

    std::map<AtomName, AutoProperty*> _window_type_prop_map;
    std::vector<Property*> _prop_list;
    std::vector<Property*> _title_prop_list;
    std::vector<Property*> _decor_prop_list;
    std::vector<Property*> _dock_app_prop_list;
    bool _harbour_sort;
    bool _apply_on_start;
};

namespace pekwm
{
    AutoProperties* autoProperties();
}
