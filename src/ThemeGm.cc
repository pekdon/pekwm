//
// ThemeGm.cc for pekwm
// Copyright (C) 2021 Claes Nästén <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "ThemeGm.hh"

uint
ThemeGm::decorWidth(const ThemeState *state) const
{
    return bdLeft(state) + bdRight(state);
}

uint
ThemeGm::decorHeight(const ThemeState *state) const
{
    return bdTop(state) + bdBottom(state) + titleHeight(state);
}

PFont*
ThemeGm::getFont(FocusedState state) const
{
    return _data->getFont(state);
}

/**
 * Calculate title width (for given title)
 */
uint
ThemeGm::titleWidth(const ThemeState *state, const std::wstring& str) const {
    return getFont(state->getFocusedState(false))->getWidth(str)
        + _data->getPad(PAD_LEFT) + _data->getPad(PAD_RIGHT)
        + titleLeftOffset(state) + titleRightOffset(state);
}

/** Calculate title height, 0 if titlebar is disabled. */
uint
ThemeGm::titleHeight(const ThemeState *state) const {
    if (! state->hasTitlebar()) {
        return 0;
    }

    if (_data->isTitleHeightAdapt()) {
        return getFont(state->getFocusedState(false))->getHeight()
            + _data->getPad(PAD_UP) + _data->getPad(PAD_DOWN);
    } else {
        return _data->getTitleHeight();
    }
}

/**
 * Offset for top border, if non full width the border is placed
 * below the titlebar.
 */
uint
ThemeGm::bdTopOffset(const ThemeState *state) const
{
    return _data->getTitleWidthMin()
        ? titleHeight(state) : 0;
}

uint
ThemeGm::bdTop(const ThemeState *state) const
{
    return getBorder(state, BORDER_TOP)->getHeight();
}

uint
ThemeGm::bdTopLeft(const ThemeState *state) const
{
    return getBorder(state, BORDER_TOP_LEFT)->getWidth();
}

uint
ThemeGm::bdTopLeftHeight(const ThemeState *state) const
{
    return getBorder(state, BORDER_TOP_LEFT)->getHeight();
}

uint
ThemeGm::bdTopRight(const ThemeState *state) const
{
    return getBorder(state, BORDER_TOP_RIGHT)->getWidth();
}

uint
ThemeGm::bdTopRightHeight(const ThemeState *state) const
{
    return getBorder(state, BORDER_TOP_RIGHT)->getHeight();
}

uint
ThemeGm::bdBottom(const ThemeState *state) const
{
    return getBorder(state, BORDER_BOTTOM)->getHeight();
}

uint
ThemeGm::bdBottomLeft(const ThemeState *state) const
{
    return getBorder(state, BORDER_BOTTOM_LEFT)->getWidth();
}

uint
ThemeGm::bdBottomLeftHeight(const ThemeState *state) const
{
    return getBorder(state, BORDER_BOTTOM_LEFT)->getHeight();
}

uint
ThemeGm::bdBottomRight(const ThemeState *state) const
{
    return getBorder(state, BORDER_BOTTOM_RIGHT)->getWidth();
}

uint
ThemeGm::bdBottomRightHeight(const ThemeState *state) const
{
    return getBorder(state, BORDER_BOTTOM_RIGHT)->getHeight();
}

uint
ThemeGm::bdLeft(const ThemeState *state) const
{
    return getBorder(state, BORDER_LEFT)->getWidth();
}

uint
ThemeGm::bdRight(const ThemeState *state) const
{
    return getBorder(state, BORDER_RIGHT)->getWidth();
}

const PTexture*
ThemeGm::getBorder(const ThemeState *state, BorderPosition pos) const
{
    if (! _data || ! state->hasBorder()) {
        return &_empty;
    }
    auto focused_state = state->getFocusedState(false);
    return _data->getBorderTexture(focused_state, pos);
}
