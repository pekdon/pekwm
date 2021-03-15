//
// Observable.hh for pekwm
// Copyright (C) 2009-2021 Claes Nästen <pekdon@gmail.com>
//
// This program is licensed under the GNU GPL.
// See the LICENSE file for more information.
//

#pragma once

#include <vector>

/**
 * Message sent to observer.
 */
class Observation {
public:
    virtual ~Observation(void) { };
};

class Observable;

class Observer {
public:
    Observer(void) { }
    virtual ~Observer(void) { }

    virtual void notify(Observable*, Observation*) { };
};

class Observable {
public:
    Observable(void);
    virtual ~Observable(void);

    void notifyObservers(Observation *observation);
    void addObserver(Observer *observer);
    void removeObserver(Observer *observer);

private:
    std::vector<Observer*> _observers; /**< List of observers. */
};
