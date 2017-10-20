#ifndef System_h
#define System_h

#include <vector>
#include "Simulation.h"

class AppClass;
class App;
class Simulation;

class System {
public:
    const char *name;
    int nb_nodes;
    int cores_per_node;
    double bandwidth;
    double mem_per_node;
    std::vector <AppClass *>classes;
    std::vector <App*>apps;
    simt_t mtbf_ind;
    Simulation *sim;
    bool finalized;
    int  next_appclass_id;
    simt_t fixed_checkpoint_interval;
    simt_t min_duration;
    
    System(const char *name, int _nodes, int _cores, double _band, double _mem, simt_t _mtbf_sys, simt_t min_duration);
    ~System();
    void clear();
    void add_app_class(int nb_cores, double input, double output, simt_t wall, double io, double ckpt, double target);
    void finalize(Simulation *_sim, unsigned int *seed);
    std::pair<int, App*> pick_class(std::vector<AppClass *>&goals, unsigned int *seed);
    void set_fixed_checkpoint_interval(simt_t intvl);
    void set_daly_checkpoint_interval();
    
    friend std::ostream& operator<< (std::ostream& stream, const System& sys);
};

#endif
