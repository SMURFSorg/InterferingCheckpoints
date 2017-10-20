#ifndef Schedule_h
#define Schedule_h

#include <map>
#include <vector>

#include "Simulation.h"

class SchedEvent;
class System;
class App;

class Schedule {
public:
    System *s;
    std::map<simt_t, SchedEvent* > scheduling;

    Schedule(System *sys);
    ~Schedule();
    void clear();
    bool app_fits(App *app, simt_t at_date, std::vector<int> *candidates);
    bool node_remains_free(int n, simt_t from_date, simt_t to_date);
    std::vector<int> *app_fits(App *app, simt_t at_date);
    void remove_events_at_date(simt_t at_date);
    void reschedule_apps(simt_t at_date);
    bool all_nodes_busy_between(simt_t start, simt_t end, const std::vector<int> *nodes);
    void update_sched_event(App *app, simt_t new_end_date);
    int print(const std::string filename, simt_t at_date);
    void print(std::ostream &o);
};

#endif
