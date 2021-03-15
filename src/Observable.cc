//
// Observable. for pekwm
// Copyright (C) 2021 Claes Nästen <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#include "Observable.hh"
#include "Util.hh"

Observable::Observable(void)
{
}

Observable::~Observable(void)
{
}

/**
 * Notify all observers.
 */
void
Observable::notifyObservers(Observation *observation)
{
    for (auto it : _observers) {
        it->notify(this, observation);
    }
}

/**
 * Add observer.
 */
void
Observable::addObserver(Observer *observer)
{
    _observers.push_back(observer);
}

/**
 * Remove observer from list.
 */
void
Observable::removeObserver(Observer *observer)
{
    _observers.erase(std::remove(_observers.begin(),
                                 _observers.end(), observer),
                     _observers.end());
}
