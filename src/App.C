#include "App.h"

#include "AppClass.h"
#include "System.h"
#include "Simulation.h"
#include "Task.h"

#include <stdlib.h>
#include <iostream>
#include <math.h>

std::ostream& operator<<(std::ostream& os, const App& app) {
    os << "App " << app.app_index << ":" << app.instance_index
       << " [" << app.nb_nodes << " nodes, "
       << app.remaining_work << " time left";
    if( app.working ) {
        os << " (at " << app.date_start_work << "),";
    } else {
        os << " (not currently working)";
    }
    return os << " has " << app.remaining_io <<" io left]";
}

static int next_app_index = 0;

App::App(AppClass *_ac, unsigned int *seed) :
    app_class(_ac),
    nodes(),
    start_date(UNDEFINED_DATE),
    end_date(UNDEFINED_DATE),
    last_succesfull_ckpt(UNDEFINED_DATE),
    date_start_work(UNDEFINED_DATE),
    current_iorate(1.0),
    working(false),
    app_index(next_app_index),
    instance_index(0),
    future_tasks(),
    completed(false)
{
    clear(seed);
    next_app_index++;
    set_random_color();
}

void App::clear(unsigned int *seed)
{
    nodes.clear();
    start_date = UNDEFINED_DATE;
    end_date = UNDEFINED_DATE;
    last_succesfull_ckpt = UNDEFINED_DATE;
    date_start_work = UNDEFINED_DATE;
    current_iorate = 1.0;
    working = false;
    instance_index = 0;
    future_tasks.clear();
    completed = false;

    int nbckpt;
    nb_nodes = app_class->_nb_nodes;
    remaining_work = 0.9 * app_class->_wall_time + app_class->_wall_time * 0.2 * (double)rand_r(seed) / (double)RAND_MAX;
    remaining_work = (remaining_work - app_class->input_time - app_class->output_time);
    if( remaining_work < 0.0 )
        throw std::runtime_error("Specified application of negative duration");
    nbckpt = remaining_work / ckpt_interval();
    wall_time = remaining_work + nbckpt * app_class->ckpt_time;
    wall_time = ceil(1.1 * wall_time);
    if( wall_time < 0.0 )
        throw std::runtime_error("Integer overflow? Walltime is negative...");
    remaining_io = app_class->input_time;
    Debug{} << "Remaining work ratio: " << (double)remaining_work / wall_time << std::endl;
    work_remaining_at_last_ckpt = remaining_work;
}

App::App(App *restarting_app) :
    app_class(restarting_app->app_class),
    nodes(),
    start_date(UNDEFINED_DATE),
    end_date(UNDEFINED_DATE),
    last_succesfull_ckpt(restarting_app->last_succesfull_ckpt),
    date_start_work(UNDEFINED_DATE),
    current_iorate(1.0),
    working(false),
    app_index(restarting_app->app_index),
    instance_index(restarting_app->instance_index+1),
    future_tasks(),
    completed(false)
{
    int nbckpt;
    nb_nodes = restarting_app->nb_nodes;
    remaining_work = restarting_app->remaining_work;
    if( remaining_work < 0.0 )
        throw std::runtime_error("Restarting an application with negative duration");
    nbckpt = remaining_work / ckpt_interval();
    remaining_io = (last_succesfull_ckpt == UNDEFINED_DATE) ? app_class->input_time : app_class->ckpt_time;
    wall_time = remaining_work + nbckpt * app_class->ckpt_time + app_class->output_time + remaining_io;
    work_remaining_at_last_ckpt = remaining_work;
    r = restarting_app->r;
    g = restarting_app->g;
    b = restarting_app->b;
}

simt_t App::ckpt_interval(void) {
    if( app_class->system->fixed_checkpoint_interval == UNDEFINED_DATE ) {
        simt_t mtbf = (double)app_class->system->mtbf_ind / nb_nodes;
        return sqrt(2.0 * mtbf * app_class->ckpt_time);
    } else {
        return app_class->system->fixed_checkpoint_interval;
    }
}
    
void App::start_working(simt_t now) {
    if(true == working) throw std::runtime_error("Started working while it was already doing so");
    Debug{} << *this << " Starts working at " << now << ", remaining work is " << remaining_work << std::endl;
    date_start_work = now;
    working = true;
}

void App::stop_working(simt_t now) {
    if( !working ) return;
    Debug{} << *this << " Stop working at " << now << " (accrued work: " << (now-date_start_work) << ")" << std::endl;
    if(remaining_work < (now-date_start_work)) throw std::runtime_error("Application extended its running time above its work duration");
    remaining_work = remaining_work - (now-date_start_work);
    date_start_work = UNDEFINED_DATE;
    working = false;
}

void App::removealltasks(simt_t date) {
    for(auto task : future_tasks) {
        auto search = app_class->system->sim->tasks.equal_range(task->date);
        for( auto e = search.first; e != search.second; e++ ) {
            if(e->second == task) {
                app_class->system->sim->tasks.erase(e);
                delete task;
                break;
            }
        }
    }
    app_class->system->sim->clear_app(this, date);
    future_tasks.clear();
}

void App::unschedule(simt_t date) {
    removealltasks(date);
    start_date = UNDEFINED_DATE;
    end_date = UNDEFINED_DATE;
}

void App::addtask(Task *task) {
    app_class->system->sim->tasks.insert( std::pair<simt_t, Task*>(task->date, task) );
    future_tasks.push_back(task);
}

void App::removetask(Task *task) {
    for(auto t = future_tasks.begin(); t != future_tasks.end(); t++) {
        if(*t == task) {
            future_tasks.erase(t);
            return;
        }
    }
}

void App::schedule(simt_t start, simt_t end) {
    if(start_date != start) {
        if( start_date != UNDEFINED_DATE ) throw std::runtime_error("Application is being scheduled with a start date that is not expected");
        start_date = start;
        auto start_task = new AppStartTask(app_class->system->sim, start_date, this);
        addtask(start_task);
    }
    if(end_date != end) {
        if( end_date != UNDEFINED_DATE ) throw std::runtime_error("Application is being scheduled with an end date that is not expected");
        end_date = end;
        auto end_task = new AppEndTask(app_class->system->sim, end_date, this);
        addtask(end_task);
    }
}

void App::checkpoint_success(simt_t date) {
    last_succesfull_ckpt = date;
    work_remaining_at_last_ckpt = remaining_work;
}
        
void App::set_random_color(void) {
    double gi = ((double)rand()) / (double)RAND_MAX;
    r = (png_byte)(floor(gi*app_class->r1 + (1.0-gi)*app_class->r2));
    g = (png_byte)(floor(gi*app_class->g1 + (1.0-gi)*app_class->g2));
    b = (png_byte)(floor(gi*app_class->b1 + (1.0-gi)*app_class->b2));
}

void App::color(png_bytep cell, float alpha) {
    cell[0] += (png_byte)(alpha * r);
    cell[1] += (png_byte)(alpha * g);
    cell[2] += (png_byte)(alpha * b);
}
