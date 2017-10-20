#ifndef SchedEvent_h
#define SchedEvent_h

#include <set>
#include <vector>

#include "Simulation.h"

class App;

class SchedEvent {
public:
    std::set<App*>    apps;
    std::vector<bool> occ;

    SchedEvent() :
        apps(),
        occ() {}

    SchedEvent(int nb_nodes) :
        apps(),
        occ(nb_nodes, false) {}

    SchedEvent(SchedEvent *ev) :
        apps(ev->apps),
        occ(ev->occ) {}

};

#endif
