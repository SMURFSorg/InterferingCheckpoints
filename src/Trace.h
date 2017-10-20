#ifndef _trace_h
#define _trace_h

#include <deque>
#include <tuple>

#include "Simulation.h"
class Task;

class Trace
{
public:
    Trace() = default;
    ~Trace() = default;
    virtual const Trace &operator<<(const Task *task) {
        std::cout << *task << std::endl;
        return *this;
    }
};

class EmptyTrace : public Trace
{
 public:
   EmptyTrace() : Trace() { }
   ~EmptyTrace() { }
   EmptyTrace &operator <<(const Task *task) { return *this; }
};

class PNGTrace : public Trace
{
    const char *filename;
    typedef struct app_id_s {
        int app_index;
        int instance_index;
        bool operator<(const struct app_id_s &b) const {
            return app_index < b.app_index ||
                ((app_index == b.app_index) && (instance_index < b.instance_index));
        }
    } app_id_t;
    typedef struct {
        simt_t date;
        app_id_t app_id;
        int type;
    } event_t;
    std::vector<event_t>all_events;
    typedef struct {
        std::vector<int> nodes;
        png_byte r, g, b;
    } app_t;
    std::map<app_id_t, app_t>pmap;
    int nb_nodes;
    simt_t max_date;
public:
    PNGTrace(const char *filename, int nb_nodes);
    ~PNGTrace();
    void output(const std::string filename, simt_t at_date);
    PNGTrace &operator <<(const Task *task);
};

class StatTrace : public Trace
{
    typedef enum {LIMBO, WORK, CKPT, IO, WASTING} app_action_t ;

    typedef struct {
        int nb_nodes;
        app_action_t current_action;
        simt_t start_action_date;
    } app_status_t;
    std::map<int, app_status_t>app_status;

    typedef struct {
        simt_t event_date;
        app_action_t event_type;
        simt_t event_duration;
        int    app_id;
    } stat_event_t;
    std::deque<stat_event_t>stat_event;
    double ignore_start;
    double ignore_end;
    simt_t last_event;
    int nb_nodes;
 public:
    StatTrace(int nb_nodes, double is = 0.1, double ie = 0.9) :
        Trace(),
        app_status(),
        stat_event(),
        ignore_start(is),
        ignore_end(ie),
        last_event(UNDEFINED_DATE),
        nb_nodes(nb_nodes)
    { }
    
    ~StatTrace() {}

    std::tuple<simt_t, simt_t, simt_t, simt_t, simt_t>getStat(simt_t intv_length, unsigned int seed);

    void interrupt_action(const AppTask *t, app_action_t new_act);
    
    StatTrace &operator <<(const Task *task);
};


#endif
