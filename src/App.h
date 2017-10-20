#ifndef App_h
#define App_h

extern "C" {
#include <png.h>
}

#include <vector>
#include "Simulation.h"

class Task;
class AppClass;
class Simulation;

class App {
public:
    AppClass        *app_class;
    std::vector<int> nodes;
    int              nb_nodes;
    simt_t           start_date;
    simt_t           end_date;
    simt_t           remaining_work;
    simt_t           wall_time;
    simt_t           last_succesfull_ckpt;
    simt_t           work_remaining_at_last_ckpt;
    simt_t           date_start_work;
    simt_t           remaining_io;
    double           current_iorate;
    bool             working;
    png_byte         r, g, b;
    int              app_index;
    int              instance_index;
    std::vector<Task*> future_tasks;
    bool             completed;

    App(AppClass *_ac, unsigned int *seed);
    App(App* restarting_app);
    void clear(unsigned int *seed);

    simt_t ckpt_interval(void);
    
    void start_working(simt_t now);

    void stop_working(simt_t now);

    void unschedule(simt_t date);
    void schedule(simt_t start, simt_t end);

    void addtask(Task *task);
    void removetask(Task *task);
    void removealltasks(simt_t date);
    
    void checkpoint_success(simt_t date);
        
    void set_random_color(void);

    void color(png_bytep cell, float alpha);
    friend std::ostream& operator<< (std::ostream& stream, const App& app);
        
};

#endif
