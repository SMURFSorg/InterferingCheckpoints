
#include "Simulation.h"
#include "AppClass.h"
#include "SchedEvent.h"
#include "Task.h"

#include <math.h>

std::ostream& operator<<(std::ostream& os, const Task& task) {
    task.print(os);
    return os;
}

void Task::print(std::ostream &o) const {
    o << "At " << date << ": " << str_type();
}

void NodeFaultTask::print(std::ostream &o) const {
    Task::print(o);
    o << "(" << node_id << ")";
}

void AppTask::print(std::ostream &o) const {
    Task::print(o);
    o  << " " << *app;
}

bool NodeFaultTask::step(void) {
    Debug{} << "*** Node " << node_id << " Dies at " << date << std::endl;
    auto ev = sim->schedule->scheduling.lower_bound(date);
    if( ev == sim->schedule->scheduling.end() ||
        std::next(ev, 1) == sim->schedule->scheduling.end()) {
        Debug{} << "*** This happens after the last scheduling event" << std::endl;
        return false;
    }
    sim->inject_next_fault(date);
    if( ev != sim->schedule->scheduling.begin() &&
        ev->first > date ) {
        ev--;
    }
    Debug{} << "*** The Scheduling Event that represents this period starts at " << ev->first << " and ends at " << std::next(ev, 1)->first << std::endl;
    App *impacted_app = nullptr;
    for(auto app = ev->second->apps.begin();
        app != ev->second->apps.end() && impacted_app == nullptr;
        app++) {
        for(auto n: (*app)->nodes) {
            if(n == node_id) {
                impacted_app = *app;
                break;
            }
        }
    }
    if( nullptr == impacted_app ) {
        Debug{} << "*** This failure did not impact any application" << std::endl;
        return false;
    }

    AppFailureTask *fault = new AppFailureTask(sim, date, impacted_app);
    impacted_app->addtask(fault);
    
    return false;
}
    
bool AppFailureTask::vstep(void) {
    Debug{} << "*** " << *app
            << " is impacted at " << date <<"; its last checkpoint was " << app->last_succesfull_ckpt
            << ", and its work remaining at last checkpoint is " << app->work_remaining_at_last_ckpt
            << std::endl;

    waste = date - app->last_succesfull_ckpt;

    App *restarting_app = new App(app);
    app->remaining_work = 0;
    app->removealltasks(date);
    sim->schedule->s->apps.insert(sim->schedule->s->apps.begin(), restarting_app);
    sim->schedule->update_sched_event(app, date);

    return true;
}

simt_t AppFailureTask::wasted_time(void) const {
    return waste;
}

bool AppTask::step(void) {
    app->removetask(this);
    Debug{} << "At " << date << ", " << *app << " does " << str_type() << std::endl;    
    return vstep();
}

bool AppStartTask::vstep(void) {
    Task *t = new IOStartTask(sim, date, app);
    app->addtask(t);
    return true;
}

bool AppEndTask::vstep(void) {
    if( app->remaining_work == 0 && app->remaining_io == 0) {
        if( !app->completed ) {
            auto search = sim->tasks.equal_range(app->end_date);
            for(auto i = search.first; i != search.second;) {
                Task *t = i->second;
                if( static_cast<AppEndTask*>(t)->app == app ) {
                    if( !(t->type == Task::APP_END ||
                          t->type == Task::IO_END) ) {
                        throw std::runtime_error("Task should either be AppEnd or IOEnd");
                    }
                    i = sim->tasks.erase(i);
                    delete t;
                } else
                    i++;
            }
            if( app->end_date < date )
                throw std::runtime_error("Application has an end date event located before its registered end date");
            sim->schedule->update_sched_event(app, date);
            app->completed = true;
            return true;
        }
        return false;
    } else {
        int nbckpt = ceil(app->remaining_work / app->ckpt_interval());
        if( app->working ) {
            app->stop_working(date);
            app->start_working(date);
        }
        simt_t new_end = date + ceil(1.2 * (app->remaining_io/app->current_iorate + app->remaining_work +
                                            nbckpt * app->app_class->ckpt_time));
        Debug{} << "****** App " << app->app_index << " end " << app->end_date << " -> " << new_end << std::endl;
        sim->schedule->update_sched_event(app, new_end);
        return false;
    }
}

bool CkptStartTask::vstep(void) {
    if( sim->start_ckpt(date, app) ) {
        app->stop_working(date);
        return true;
    }
    return false;
}

bool CkptEndTask::vstep(void) {
    Task *t = NULL;
    app->start_working(date);
    if(sim->end_ckpt(date, app)) {
        Debug{} << "At " << date << ", " << *app << " succeeds checkpoint" << std::endl;
        app->checkpoint_success(date);
    }
    if( app->remaining_work <= 0 )
        throw std::runtime_error("Application is ending its checkpoint but no work remains");
    simt_t ckpt = app->ckpt_interval();
    if(ckpt < app->remaining_work) {
        t = new CkptStartTask(sim, date + ckpt, app);
    } else {
        app->remaining_io = app->app_class->output_time;
        Debug{} << "Final task: setting the remaining_io to output for app " << *app << std::endl;
        t = new IOStartTask(sim, date + app->remaining_work, app);
    }
    app->addtask(t);
    return !app->completed;
}

bool CkptIOStartTask::vstep(void) {
    sim->start_ckpt_io(date, app);
    return true;
}

bool CkptIOEndTask::vstep(void) {
    sim->end_ckpt_io(date, app);
    return true;
}

bool IOStartTask::vstep(void) {
    if( app->remaining_work == 0 ) {
        app->remaining_io = app->app_class->output_time;
    } else {
        // app->remaining_io set by App constructor
        // to app_class->ckpt_time in case of restart
        // or app_class->input_time in case of initial run
    }
    sim->start_io(date, app);
    app->stop_working(date);

    return true;
}

bool IOEndTask::vstep(void) {
    Task *t = NULL;
    sim->end_io(date, app);
    if(app->remaining_work == 0) {
        if( app->end_date < date ) {
            std::stringstream error;
            error << "Application "<< *app
                  <<"is ending a final I/O at " << date
                  << ", but the end date " << app->end_date
                  << " was registered to happen before";
            throw std::runtime_error(error.str());
        }
        t = new AppEndTask(sim, date, app);
    } else {
        app->start_working(date);
        simt_t ckpt = app->ckpt_interval();
        if(ckpt < app->remaining_work) {
            t = new CkptStartTask(sim, date + ckpt, app);
        } else {
            t = new IOStartTask(sim, date + app->remaining_work, app);
        }
    }
    app->addtask(t);
    return !app->completed;
}
