#include "Simulation.h"

#include <iostream>
#include <math.h>

#include "System.h"
#include "Schedule.h"
#include "Task.h"
#include "App.h"
#include "AppClass.h"

#define DOUBLE_CHECKS 0

std::mutex Debug::_mutexDebug{};
bool Debug::debug = false;
std::ostream *Debug::stream = &std::cerr;

Simulation::Simulation(Schedule *_sched, Trace &t, unsigned int seed, bool inject_failures) :
    tasks(),
    schedule(_sched),
    io_tasks(),
    trace(t),
    seed_fault(seed),
    seed_app_order(seed)
{
    schedule->s->finalize(this, &seed_app_order);
    if( inject_failures )
        inject_next_fault(0);
}

double Simulation::cur_date(void)
{
    return curdate / TIME_UNIT;
}

simt_t Simulation::cur_simt(void)
{
    return curdate;
}

void Simulation::inject_next_fault(simt_t from_date)
{
    double mu = schedule->s->mtbf_ind / schedule->s->nb_nodes;
    double lambda = 1.0/mu;
    double r = (double)rand_r(&seed_fault) / (double)RAND_MAX;
    simt_t date = ceil(- log(-r + 1.0) / lambda) + from_date;
    int node = (int) ( schedule->s->nb_nodes * (double)rand_r(&seed_fault) / (double)RAND_MAX);
    Debug{} << "*** Injecting fault at " << date << " on " << node << std::endl;
    NodeFaultTask *fault = new NodeFaultTask(this, date, node);
    tasks.insert(std::pair<simt_t, Task*>(date, fault));
}

void Simulation::clear_app(App *app, simt_t date)
{
    (void)date;
    for(auto it = io_tasks.begin(); it != io_tasks.end(); ) {
        if( (*it)->app == app ) {
            it = io_tasks.erase(it);
        } else {
            it++;
        }
    }
}

bool Simulation::step(void) {
    if( tasks.empty() ) {
        return false;
    }
    
    auto first = tasks.begin();
    Task *task  = first->second;

    tasks.erase(first);
    
    //std::cout << "Handling of Task ";
    //task->print(std::cout);
    //std::cout << std::endl;
    curdate = task->date;

    if( task->step() ) {
        trace << task;
    }
    
    delete task;
    
    return true;
}

/** SimSimpleInterference
 *    Two interfering I/O are slowed down proportionnaly to the
 *    number of nodes doing I/O.
 **/

SimSimpleInterference::~SimSimpleInterference()
{
    tasks.clear();
    io_tasks.clear();
}

void SimSimpleInterference::reschedule_end_ios(int nb_nodes_doing_io, simt_t date)
{
    for(auto t2 = io_tasks.begin(); t2 != io_tasks.end(); ) {
        AppTaskIO *t = *t2;
        if( t->app->remaining_io > 0 ) {
            if( t->app->current_iorate != (double)t->app->nb_nodes / (double)nb_nodes_doing_io ) {
                Debug{} << "At " << date << ", " << *t->app << " Changes its io rate from " << t->app->current_iorate;
                t->app->current_iorate = (double)t->app->nb_nodes / (double)nb_nodes_doing_io;
                Debug{} << " To " << t->app->current_iorate << std::endl;
                auto search = tasks.equal_range(t->date);
                for(auto t3 = search.first; t3 != search.second; t3++) {
                    if( t3->second == t) {
                        tasks.erase(t3);
                        break;
                    }
                }
                Debug{} << *t->app << " was completing its io at " << t->date;
                t->date = date + floor(t->app->remaining_io / t->app->current_iorate);
                Debug{} << ". It now completes its at " << t->date << std::endl;
                tasks.insert( std::pair<simt_t, Task*>(t->date, t));
            }
            t2++;
        } else {
            Debug{} << "Removing App Task for App " << t->app->app_index << " because its remaining io is 0" << std::endl;
            t2 = io_tasks.erase(t2);
        }
    }
}

int SimSimpleInterference::update_remaining_ios(simt_t date)
{
    int nb_nodes_doing_io = 0;
    
    if( !(date_of_last_iorate_change != UNDEFINED_DATE ||
          io_tasks.empty()) ) {
        throw std::runtime_error("Tasks are registered in I/O tasks, but no date of last rate change is defined");
    }
    if( !(date_of_last_iorate_change == UNDEFINED_DATE ||
          date_of_last_iorate_change <= date) ) {
        std::stringstream msg;
        msg << "Date of last rate change (" << date_of_last_iorate_change << ") is inconsistent with current date " << date;
        throw std::runtime_error(msg.str());
    }
    for(auto t : io_tasks) {
        simt_t delta = date - date_of_last_iorate_change;
        Debug{}  << "App " << t->app->app_index << ": remaining io was " << t->app->remaining_io
                 << ", delta is " << delta << ", and current iorate is " << t->app->current_iorate
                 << " Thus new remaining io is ";
        if(ceil(delta * t->app->current_iorate) > t->app->remaining_io + TIME_UNIT) {
            std::stringstream msg;
            msg << ceil(delta * t->app->current_iorate)
                << " I/O would be transferred at this rate ("<< t->app->current_iorate <<"), which is more than the remaining I/O (" << t->app->remaining_io << ")";
            throw std::runtime_error(msg.str());
        }
        if( ceil(delta * t->app->current_iorate) > t->app->remaining_io )
            t->app->remaining_io = 0;
        else
            t->app->remaining_io = t->app->remaining_io - ceil(delta * t->app->current_iorate);
        Debug{} << t->app->remaining_io << std::endl;
        
        if( t->app->remaining_io > 0 ) {
            nb_nodes_doing_io += t->app->nb_nodes;
        }
    }
    date_of_last_iorate_change = date;
    return nb_nodes_doing_io;
}

void SimSimpleInterference::start_remaining_io(simt_t date, App *app, AppTaskIO *task)
{
    app->addtask(task);
    if(app->remaining_io > 0) {
        int nb_nodes_doing_io = app->nb_nodes;
        nb_nodes_doing_io += update_remaining_ios(date);

        app->current_iorate = 1.0;
        io_tasks.push_back(task);

        reschedule_end_ios(nb_nodes_doing_io, date);
    } 
}

void SimSimpleInterference::start_io(simt_t date, App *app)
{
    AppTaskIO *t = nullptr;

    // Value of remaining_io is decided up, because it depends
    // if it is a restarting application or an initial run
    t = new IOEndTask(this, date + app->remaining_io, app);
    start_remaining_io(date, app, t);
}

void SimSimpleInterference::end_io(simt_t date, App *app)
{
    bool found_task = false;
    int nb_nodes_doing_io = update_remaining_ios(date);
    for(auto i = io_tasks.begin(); i != io_tasks.end();) {
        AppTask *ai = static_cast<AppTask*>(*i);
        if( ai->app == app ) {
            i = io_tasks.erase(i);
            if( found_task == true ) throw std::runtime_error("Task appears multiple times in the I/O tasks list");
            found_task = true;
        } else {
            i++;
        }
    }
    if(found_task) {
        reschedule_end_ios(nb_nodes_doing_io, date);
    }
}

bool SimSimpleInterference::start_ckpt(simt_t date, App *app)
{
    AppTaskIO *t = nullptr;

    app->remaining_io = app->app_class->ckpt_time;
    t = new CkptEndTask(this, date + app->remaining_io, app);
    start_remaining_io(date, app, t);
    // In this mode, checkpoints always start now
    return true;
}

bool SimSimpleInterference::end_ckpt(simt_t date, App *app)
{
    end_io(date, app);
    return true;
}


/** SimSimpleInterferenceWithBurstBuffers
 *    Two interfering I/O are slowed down proportionnaly to the
 *    number of nodes doing I/O.
 **/

SimSimpleInterferenceWithBurstBuffers::~SimSimpleInterferenceWithBurstBuffers()
{
    tasks.clear();
    io_tasks.clear();
}

void SimSimpleInterferenceWithBurstBuffers::reschedule_end_ios(int nb_nodes_doing_io, simt_t date)
{
    for(auto t2 = io_tasks.begin(); t2 != io_tasks.end(); ) {
        AppTaskIO *t = *t2;
        if( t->app->remaining_io > 0 ) {
            if( t->app->current_iorate != (double)t->app->nb_nodes / (double)nb_nodes_doing_io ) {
                Debug{} << "At " << date << ", " << *t->app << " Changes its io rate from " << t->app->current_iorate;
                t->app->current_iorate = (double)t->app->nb_nodes / (double)nb_nodes_doing_io;
                Debug{} << " To " << t->app->current_iorate << std::endl;
                auto search = tasks.equal_range(t->date);
                for(auto t3 = search.first; t3 != search.second; t3++) {
                    if( t3->second == t) {
                        tasks.erase(t3);
                        break;
                    }
                }
                Debug{} << *t->app << " was completing its io at " << t->date;
                t->date = date + floor(t->app->remaining_io / t->app->current_iorate);
                Debug{} << ". It now completes its at " << t->date << std::endl;
                tasks.insert( std::pair<simt_t, Task*>(t->date, t));
            }
            t2++;
        } else {
            Debug{} << "Removing App Task for App " << t->app->app_index << ":" << t->app->instance_index << " because its remaining io is 0" << std::endl;
            t2 = io_tasks.erase(t2);
        }
    }
}

int SimSimpleInterferenceWithBurstBuffers::update_remaining_ios(simt_t date)
{
    int nb_nodes_doing_io = 0;
    
    if( !(date_of_last_iorate_change != UNDEFINED_DATE ||
          io_tasks.empty()) ) {
        throw std::runtime_error("Tasks are registered in I/O tasks, but no date of last rate change is defined");
    }
    if( !(date_of_last_iorate_change == UNDEFINED_DATE ||
          date_of_last_iorate_change <= date) ) {
        std::stringstream msg;
        msg << "Date of last rate change (" << date_of_last_iorate_change << ") is inconsistent with current date " << date;
        throw std::runtime_error(msg.str());
    }
    for(auto t2 = io_tasks.begin(); t2 != io_tasks.end(); t2++) {
        auto t = *t2;
        simt_t delta = date - date_of_last_iorate_change;
        Debug{}  << "App " << t->app->app_index << ":" << t->app->instance_index << " -- remaining io was " << t->app->remaining_io
                 << ", delta is " << delta << ", and current iorate is " << t->app->current_iorate
                 << " Thus new remaining io is ";
        if(ceil(delta * t->app->current_iorate) > t->app->remaining_io + TIME_UNIT) {
            std::stringstream msg;
            msg << ceil(delta * t->app->current_iorate)
                << " I/O would be transferred at this rate ("<< t->app->current_iorate <<"), which is more than the remaining I/O (" << t->app->remaining_io << ")";
            throw std::runtime_error(msg.str());
        }
        if( ceil(delta * t->app->current_iorate) > t->app->remaining_io )
            t->app->remaining_io = 0;
        else
            t->app->remaining_io = t->app->remaining_io - ceil(delta * t->app->current_iorate);
        Debug{} << t->app->remaining_io << std::endl;
        
        if( t->app->remaining_io > 0 ) {
            nb_nodes_doing_io += t->app->nb_nodes;
        }
    }
    date_of_last_iorate_change = date;
    return nb_nodes_doing_io;
}

void SimSimpleInterferenceWithBurstBuffers::start_remaining_io(simt_t date, App *app, AppTaskIO *task)
{
    app->addtask(task);
    if(app->remaining_io > 0) {
        int nb_nodes_doing_io = app->nb_nodes;
        nb_nodes_doing_io += update_remaining_ios(date);

        app->current_iorate = 1.0;
        for(auto t: io_tasks) {
            if( t == task )
                throw std::runtime_error("Task already in the io_tasks vector");
        }
        io_tasks.push_back(task);

        reschedule_end_ios(nb_nodes_doing_io, date);
    } 
}

void SimSimpleInterferenceWithBurstBuffers::cancel_concurrent_io(App *app)
{
    for(auto t2 = io_tasks.begin(); t2 != io_tasks.end(); ) {
        auto t = *t2;
        if( t->app == app ) {
            if( t->type != Task::CKPT_IO_END )
                throw std::runtime_error("There is another I/O task than a CKPT IO END");
            t2 = io_tasks.erase(t2);
            app->removetask(t);
            for(auto t3 = tasks.begin(); t3 != tasks.end(); ) {
                auto t4 = t3->second;
                if( t4 == t )
                    t3 = tasks.erase(t3);
                else
                    t3++;
            }
        } else
            t2++;
    }
}

void SimSimpleInterferenceWithBurstBuffers::start_io(simt_t date, App *app)
{
    AppTaskIO *t = nullptr;

    if( app->is_checkpointing() ) {
        /* We need to stop this checkpoint, as it would compete with the
         * current (final) output */
        cancel_concurrent_io(app);
    }
    
    // Value of remaining_io is decided up, because it depends
    // if it is a restarting application or an initial run
    t = new IOEndTask(this, date + app->remaining_io, app);
    start_remaining_io(date, app, t);
}

void SimSimpleInterferenceWithBurstBuffers::end_io(simt_t date, App *app)
{
    bool found_task = false;
    int nb_nodes_doing_io = update_remaining_ios(date);
    for(auto i = io_tasks.begin(); i != io_tasks.end();) {
        AppTask *ai = static_cast<AppTask*>(*i);
        if( ai->app == app ) {
            i = io_tasks.erase(i);
            if( found_task == true ) throw std::runtime_error("Task appears multiple times in the I/O tasks list");
            found_task = true;
        } else {
            i++;
        }
    }
    if(found_task) {
        reschedule_end_ios(nb_nodes_doing_io, date);
    }
}

bool SimSimpleInterferenceWithBurstBuffers::start_ckpt(simt_t date, App *app)
{
    AppTaskIO *t = nullptr;

    if( app->is_checkpointing() ) {
        Debug{}  << "App " << *app << " is already checkpointing, updating the remaining work but not doing a new checkpoint" << std::endl;
        app->stop_working(date);
        app->start_working(date);
        if( app->remaining_work <= 0 )
            throw std::runtime_error("Application is starting a checkpoint but no work remains");
        simt_t ckpt = app->ckpt_interval();
        if(ckpt < app->remaining_work) {
            t = new CkptStartTask(this, date + ckpt, app);
        } else {
            Debug{}  << "App " << *app << " will finish soon, cancelling ongoing checkpoint and scheduling final output" << std::endl;
            cancel_concurrent_io(app);            
            app->remaining_io = app->app_class->output_time;
            t = new IOStartTask(this, date + app->remaining_work, app);
        }        
        app->addtask(t);
        return false;
    }
    
    Debug{}  << "App " << *app << " is not checkpointing, starting a new checkpoint" << std::endl;
    app->start_checkpointing();
    t = new CkptEndTask(this, date + app->app_class->bb_ckpt_time, app);
    app->addtask(t);
    // In this mode, checkpoints always start now
    return true;
}

bool SimSimpleInterferenceWithBurstBuffers::end_ckpt(simt_t date, App *app)
{
    AppTaskIO *t = nullptr;

    t = new CkptIOStartTask(this, date, app);
    app->addtask(t);

    return false; /* No, the checkpoint was not succesfull yet, it will be at the CkptIOEnd event */
}

void SimSimpleInterferenceWithBurstBuffers::start_ckpt_io(simt_t date, App *app)
{
    AppTaskIO *t = nullptr;

    app->remaining_io = app->app_class->ckpt_time;
    Debug{} << "At " << date << ", " << *app << " Starts its checkpoint I/O, planned for " << app->remaining_io << " additional time " << std::endl;
    t = new CkptIOEndTask(this, date + app->remaining_io, app);
    start_remaining_io(date, app, t);
}

void SimSimpleInterferenceWithBurstBuffers::end_ckpt_io(simt_t date, App *app)
{
    Debug{} << "At " << date << ", " << *app << " ends its checkpoint I/O and succeeds checkpoint" << std::endl;
    app->checkpoint_success(date);
    app->stop_checkpointing();
    end_io(date, app);
}

/** SimOrderedIOBlockingFCFS */

SimOrderedIOBlockingFCFS::~SimOrderedIOBlockingFCFS()
{
    tasks.clear();
    io_tasks.clear();
}

void SimOrderedIOBlockingFCFS::start_io(simt_t date, App *app)
{
    simt_t start_date;
    simt_t end_date;
    if( 0 == app->remaining_io ) {
        IOEndTask *t = new IOEndTask(this, date, app);
        app->addtask(t);
        return;
    }
    if( date_of_last_io == UNDEFINED_DATE )
        start_date = date;
    else
        start_date = date_of_last_io;
    end_date = start_date + app->remaining_io;
    IOEndTask *t = new IOEndTask(this, end_date, app);
    Debug{} << "At " << date << ", " << *app << " schedules I/O End at " << end_date << std::endl;
    io_tasks.push_back(t);
    app->addtask(t);
    date_of_last_io = end_date;
}

void SimOrderedIOBlockingFCFS::end_io(simt_t date, App *app)
{
    bool found_task = false;
    if( date == date_of_last_io )
        date_of_last_io = UNDEFINED_DATE;
    
    for(auto i = io_tasks.begin(); i != io_tasks.end();) {
        AppTask *ai = static_cast<AppTask*>(*i);
        if( ai->app == app ) {
            app->remaining_io = 0;
            i = io_tasks.erase(i);
            if( found_task == true ) throw std::runtime_error("Task appears multiple times in the I/O tasks list");
            found_task = true;
        } else {
            i++;
        }
    }
}

bool SimOrderedIOBlockingFCFS::start_ckpt(simt_t date, App *app)
{
    simt_t start_date;
    simt_t end_date;
    if( date_of_last_io == UNDEFINED_DATE )
        start_date = date;
    else
        start_date = date_of_last_io;
    app->remaining_io = app->app_class->ckpt_time;
    end_date = start_date + app->remaining_io;
    CkptEndTask *t = new CkptEndTask(this, end_date, app);
    io_tasks.push_back(t);
    app->addtask(t);
    date_of_last_io = end_date;
    return true;
}

bool SimOrderedIOBlockingFCFS::end_ckpt(simt_t start_date, App *app)
{
    end_io(start_date, app);
    return true;
}

/** SimNoInterference */

SimNoInterference::~SimNoInterference()
{
    tasks.clear();
    io_tasks.clear();
}

void SimNoInterference::start_io(simt_t start_date, App *app)
{
    IOEndTask *t = new IOEndTask(this, start_date + app->remaining_io, app);
    io_tasks.push_back(t);
    app->addtask(t);
}

void SimNoInterference::end_io(simt_t start_date, App *app)
{
    bool found_task = false;
    for(auto i = io_tasks.begin(); i != io_tasks.end();) {
        AppTask *ai = static_cast<AppTask*>(*i);
        if( ai->app == app ) {
            app->remaining_io = 0;
            i = io_tasks.erase(i);
            if( found_task == true ) throw std::runtime_error("Task appears multiple times in the I/O tasks list");
            found_task = true;
        } else {
            i++;
        }
    }
}

bool SimNoInterference::start_ckpt(simt_t start_date, App *app)
{
    app->remaining_io = app->app_class->ckpt_time;
    auto t = new CkptEndTask(this, start_date + app->remaining_io, app);
    io_tasks.push_back(t);
    app->addtask(t);
    return true;
}

bool SimNoInterference::end_ckpt(simt_t start_date, App *app)
{
    end_io(start_date, app);
    return true;
}


/** SimNoInterferenceWithBurstBuffers */

SimNoInterferenceWithBurstBuffers::~SimNoInterferenceWithBurstBuffers()
{
    tasks.clear();
    io_tasks.clear();
}

void SimNoInterferenceWithBurstBuffers::cancel_concurrent_io(App *app)
{
    for(auto t2 = io_tasks.begin(); t2 != io_tasks.end(); ) {
        auto t = *t2;
        if( t->app == app ) {
            if( t->type != Task::CKPT_IO_END )
                throw std::runtime_error("There is another I/O task than a CKPT IO END");
            t2 = io_tasks.erase(t2);
            app->removetask(t);
            for(auto t3 = tasks.begin(); t3 != tasks.end(); ) {
                auto t4 = t3->second;
                if( t4 == t )
                    t3 = tasks.erase(t3);
                else
                    t3++;
            }
        } else
            t2++;
    }
}

void SimNoInterferenceWithBurstBuffers::start_io(simt_t start_date, App *app)
{
    if( app->is_checkpointing() ) {
        /* We need to stop this checkpoint, as it would compete with the
         * current (final) output */
        cancel_concurrent_io(app);
    }
    
    IOEndTask *t = new IOEndTask(this, start_date + app->remaining_io, app);
    io_tasks.push_back(t);
    app->addtask(t);
}

void SimNoInterferenceWithBurstBuffers::end_io(simt_t start_date, App *app)
{
    bool found_task = false;
    for(auto i = io_tasks.begin(); i != io_tasks.end();) {
        AppTask *ai = static_cast<AppTask*>(*i);
        if( ai->app == app ) {
            app->remaining_io = 0;
            i = io_tasks.erase(i);
            if( found_task == true ) throw std::runtime_error("Task appears multiple times in the I/O tasks list");
            found_task = true;
        } else {
            i++;
        }
    }
}

bool SimNoInterferenceWithBurstBuffers::start_ckpt(simt_t start_date, App *app)
{
    AppTaskIO *t = nullptr;
    
    if( app->is_checkpointing() ) {
        Debug{}  << "App " << *app << " is already checkpointing, updating the remaining work but not doing a new checkpoint" << std::endl;
        app->stop_working(start_date);
        app->start_working(start_date);
        if( app->remaining_work <= 0 )
            throw std::runtime_error("Application is starting a checkpoint but no work remains");
        simt_t ckpt = app->ckpt_interval();
        if(ckpt < app->remaining_work) {
            t = new CkptStartTask(this, start_date + ckpt, app);
        } else {
            Debug{}  << "App " << *app << " will finish soon, cancelling ongoing checkpoint and scheduling final output" << std::endl;
            cancel_concurrent_io(app);            
            app->remaining_io = app->app_class->output_time;
            t = new IOStartTask(this, start_date + app->remaining_work, app);
        }        
        app->addtask(t);
        return false;
    }
    
    Debug{}  << "App " << *app << " is not checkpointing, starting a new checkpoint" << std::endl;
    app->start_checkpointing();
    
    app->remaining_io = app->app_class->bb_ckpt_time;
    t = new CkptEndTask(this, start_date + app->remaining_io, app);
    app->addtask(t);
    return true;
}

bool SimNoInterferenceWithBurstBuffers::end_ckpt(simt_t start_date, App *app)
{
    AppTaskIO *t = nullptr;

    t = new CkptIOStartTask(this, start_date, app);
    app->addtask(t);

    return false; /* No, the checkpoint was not succesfull yet, it will be at the CkptIOEnd event */
}

void SimNoInterferenceWithBurstBuffers::start_ckpt_io(simt_t date, App *app)
{
    AppTaskIO *t = nullptr;

    app->remaining_io = app->app_class->ckpt_time;
    Debug{} << "At " << date << ", " << *app << " Starts its checkpoint I/O, planned for " << app->remaining_io << " additional time " << std::endl;
    t = new CkptIOEndTask(this, date + app->remaining_io, app);
    io_tasks.push_back(t);
    app->addtask(t);
}

void SimNoInterferenceWithBurstBuffers::end_ckpt_io(simt_t date, App *app)
{
    Debug{} << "At " << date << ", " << *app << " ends its checkpoint I/O and succeeds checkpoint" << std::endl;
    app->checkpoint_success(date);
    app->stop_checkpointing();
    end_io(date, app);
}

/** SimOrderedIOFCFS */

SimOrderedIOFCFS::~SimOrderedIOFCFS()
{
    tasks.clear();
    io_tasks.clear();
}

void SimOrderedIOFCFS::start_io(simt_t date, App *app)
{
    simt_t enddate = date + app->remaining_io;
    AppTaskIO *t = nullptr;
    if( app->remaining_io == 0 ) {
        t = new IOEndTask(this, date, app);
        app->addtask(t);
        return;
    }
    
    if( !io_tasks.empty() ) {
        for(auto l = io_tasks.rbegin();
            l != io_tasks.rend();
            l++) {
            if( (*l)->type == Task::IO_END ||
                (*l)->type == Task::CKPT_END ) {
                enddate = (*l)->date + app->remaining_io;
                break;
            }
        }
    }
    
    t = new IOEndTask(this, enddate, app);
    io_tasks.push_back(t);
    app->addtask(t);
}

void SimOrderedIOFCFS::end_io(simt_t start_date, App *app)
{
    bool found_task = false;
    for(auto i = io_tasks.begin(); i != io_tasks.end();) {
        AppTask *ai = static_cast<AppTask*>(*i);
        if( ai->app == app && ai->type == Task::IO_END ) {
            app->remaining_io = 0;
            i = io_tasks.erase(i);
            if( found_task == true ) throw std::runtime_error("Task appears multiple times in the I/O tasks list");
            found_task = true;
        } else {
            i++;
        }
    }
}

bool SimOrderedIOFCFS::start_ckpt(simt_t start_date, App *app)
{
    AppTaskIO *t = nullptr;
    app->remaining_io = app->app_class->ckpt_time;
    if( io_tasks.empty() ) {
        t = new CkptEndTask(this, start_date + app->remaining_io, app);
        app->addtask(t);
        io_tasks.push_back(t);
        return true;
    } else {
        t = io_tasks.front();
        if( t->type == Task::CKPT_START &&
            t->app == app ) {
            io_tasks.erase(io_tasks.begin());
            // The CkptEndTask has been pushed already
            return true;
        } else {
            for(auto l = io_tasks.rbegin();
                l != io_tasks.rend();
                l++) {
                if( (*l)->type == Task::IO_END ||
                    (*l)->type == Task::CKPT_END ) {
                    if( (*l)->date < start_date ) throw std::runtime_error("registered I/O tasks should preceded new checkpointing task");
                    simt_t done_work = start_date - app->date_start_work;
                    simt_t real_remaining_work = app->remaining_work - done_work;
                    Debug{} << "## For " << *app
                            << ", at " << start_date
                            << " (start_ckpt), done_work = " << done_work
                            << ", real_remaining_work = " << real_remaining_work
                            << ", last scheduled I/O ending at " << (*l)->date
                            << std::endl;
                    if( (*l)->date - start_date < real_remaining_work ) {
                        t =  new CkptStartTask(this, (*l)->date, app);
                        app->addtask(t);
                        io_tasks.push_back(t);
                        t = new CkptEndTask(this, (*l)->date + app->remaining_io, app);
                        app->addtask(t);
                        io_tasks.push_back(t);
                    } else {
                        app->remaining_io = 0;
                        t = new IOStartTask(this, start_date + real_remaining_work, app);
                        app->addtask(t);
                    }
                    return false;
                }
            }
            throw std::runtime_error("Preceding I/O task not found");
        }
    }
    throw std::runtime_error("This code should be unreachable");
    return false;
}

bool SimOrderedIOFCFS::end_ckpt(simt_t start_date, App *app)
{
    bool found_task = false;
    for(auto i = io_tasks.begin(); i != io_tasks.end();) {
        AppTask *ai = static_cast<AppTask*>(*i);
        if( ai->app == app && ai->type == Task::CKPT_END ) {
            app->remaining_io = 0;
            i = io_tasks.erase(i);
            if( found_task == true ) throw std::runtime_error("Task appears multiple times in the I/O tasks list");
            found_task = true;
        } else {
            i++;
        }
    }
    return true;
}

/** SimOrderedIOCoop */

SimOrderedIOCoop::~SimOrderedIOCoop()
{
    tasks.clear();
    io_tasks.clear();
}

bool SimOrderedIOCoop::io_request_t::operator<(const struct io_request_s &b) const {
    return (app->app_index < b.app->app_index) || ((app->app_index == b.app->app_index) && (app->instance_index < b.app->instance_index));
}

void SimOrderedIOCoop::start_io(simt_t date, App *app)
{
    AppTaskIO *t = nullptr;
    if( app->remaining_io == 0 ) {
        Debug{} << "## " << *app
                  << " has no initial or final IO, skipping it"
                  << std::endl;
        t = new IOEndTask(this, date, app);
        app->addtask(t);
        return;
    }
    
    if( nullptr != current_io ) {
        Debug{} << "## IO of " << *app
                  << " requested to start at " << date
                  << " must be deferred because of ongoing IO for " << *current_io->app
                  << std::endl;
        io_request_t ior;
        ior.requested_start_date = date;
        ior.checkpoint = false;
        ior.app = app;
        io_requests.insert(ior);
        return;
    }

    Debug{} << "## IO of " << *app
              << " Starts at date " << date
              << " until " << date + app->remaining_io << std::endl;
    t = new IOEndTask(this, date + app->remaining_io, app);
    current_io = t;
    app->addtask(t);
}

double SimOrderedIOCoop::heuristic(simt_t date, io_request_t req)
{
    double Wi = 0.0;
    double vi = req.app->remaining_io;
    auto d = [&date](io_request_t r) {
        if( r.checkpoint ) {
            if( r.app->last_succesfull_ckpt != UNDEFINED_DATE ) {
                return date - r.app->last_succesfull_ckpt;
            } else {
                return date - r.app->start_date;
            }
        } else {
            return date - r.requested_start_date;
        }
    };

    for(auto r : io_requests) {
        if( r.app == req.app) {
            continue;
        }
        if( r.checkpoint ) {
            Wi += r.app->nb_nodes * (d(r) + vi);
        } else {
            Wi += r.app->nb_nodes * r.app->nb_nodes * vi / r.app->app_class->system->mtbf_ind * (r.app->app_class->ckpt_time + d(r) + vi / 2.0);
        }
    }

    return Wi;
}

void SimOrderedIOCoop::select_next_io_task(simt_t date)
{
    if( io_requests.empty() ) {
        Debug{} << "## No more IO tasks to schedule" << std::endl;
        return;
    }
    
    auto it = io_requests.begin();
    double score = heuristic(date, *it);
    auto best_it = it;
    double best_score = score;
    it++;
    while(it != io_requests.end() ) {
        score = heuristic(date, *it);
        if( score < best_score ) {
            best_score = score;
            best_it = it;
        }
        it++;
    }

    Debug{} << "## Selected " << (best_it->checkpoint ? "Checkpoint" : "IO")
              << " of app " << *best_it->app
              << " with score " << score
              << " to start at date " << date
              << " and complete at date " << date + best_it->app->remaining_io
              << std::endl;

    App *best_app = best_it->app;

    AppTaskIO *t = nullptr;
    if(nullptr != current_io) throw std::runtime_error("When selecting a new io task, another one should already be running");
    if( best_it->checkpoint ) {
        t = new CkptStartTask(this, date, best_app);
        best_app->addtask(t);
        current_io = t;
    } else {
        t = new IOEndTask(this, date + best_app->remaining_io, best_app);
        best_app->addtask(t);
        current_io = t;
    }
    
    io_requests.erase(best_it);

    // There might be some checkpoints that we need to cancel, we won't have time to do them
    for(it = io_requests.begin(); it != io_requests.end();) {
        if( it->checkpoint ) {
            App *app = it->app;
            if( app->remaining_work < (date-app->date_start_work) )
                throw std::runtime_error("Application's remaining work should be less than the time spent since it started working (lost applicaiton end?)");
            if( app->date_start_work + app->remaining_work < date + best_app->remaining_io ) {
                Debug{} << "## Checkpoint of " << *app
                          << " Will not be able to run, not enough time left. Scheduling end IO to start at " << app->date_start_work + app->remaining_work
                          << std::endl;
                t = new IOStartTask(this, app->date_start_work + app->remaining_work, app);
                app->addtask(t);
                it = io_requests.erase(it);
            } else {
                Debug{} << "## Checkpoint of " << *app
                        << " will still have time to run after " << *best_app
                        << " has checkpointed at date " << date
                        << std::endl;
                it++;
            }
        } else {
            it++;
        }
    }
    
}

void SimOrderedIOCoop::end_io(simt_t start_date, App *app)
{
    current_io = nullptr;
    app->remaining_io = 0;
    Debug{} << "## IO of " << *app << " Ends at date " << start_date << std::endl;
    select_next_io_task(start_date);
}

bool SimOrderedIOCoop::start_ckpt(simt_t start_date, App *app)
{
    AppTaskIO *t = nullptr;
    app->remaining_io = app->app_class->ckpt_time;
    if( nullptr == current_io ) {
        Debug{} << "## Checkpoint of " << *app
                  << " Starts at date " << start_date
                  << " until " << start_date + app->remaining_io
                  << std::endl;
        t = new CkptEndTask(this, start_date + app->remaining_io, app);
        app->addtask(t);
        current_io = t;
        return true;
    } else {
        if( current_io->type == Task::CKPT_START &&
            current_io->app == app ) {
            Debug{} << "## Checkpoint of " << *app
                      << " Starts at date " << start_date
                      << " until " << start_date + app->remaining_io 
                      << std::endl;
            if( app->remaining_work <= 0 )
                throw std::runtime_error("Application has no work to do when starting checkpoint");
            t = new CkptEndTask(this, start_date + app->remaining_io, app);
            app->addtask(t);
            current_io = t;
            return true;
        } else {
            // Do we have enough time?
            simt_t enddate;
            if( current_io->type == Task::CKPT_START ) {
                enddate = current_io->app->remaining_io + start_date;
            } else {
                enddate = current_io->date;
            }
            if( enddate < app->date_start_work + app->remaining_work ) {
                // yes we might
                Debug{} << "## Checkpoint of " << *app
                          << " requested to start at " << start_date
                          << " for " << app->remaining_work
                          << " must be deferred because of ongoing IO for " << *current_io->app
                          << " that will end at " << enddate
                          << std::endl;
                io_request_t ior;
                ior.requested_start_date = start_date;
                ior.checkpoint = true;
                ior.app = app;
                io_requests.insert(ior);
                return false;
            } else {
                // no we don't
                Debug{} << "## Checkpoint of " << *app
                          << " requested to start at " << start_date
                          << " must be cancelled because of ongoing IO for " << *current_io->app
                          << std::endl;
                t = new IOStartTask(this, app->date_start_work + app->remaining_work, app);
                app->addtask(t);
                return false;
            }
        }
    }
    throw std::runtime_error("Code should be unreachable");
    return false;
}

bool SimOrderedIOCoop::end_ckpt(simt_t start_date, App *app)
{
    Debug{} << "## Checkpoint of " << *app << " Ends at date " << start_date << std::endl;
    end_io(start_date, app);
    return true;
}

void SimOrderedIOCoop::clear_app(App *app,simt_t date)
{
    Simulation::clear_app(app, date);
    for(auto f = io_requests.begin(); f != io_requests.end(); ) {
        if( f->app == app ) {
            Debug{} << "## Request of IO of " << *app
                      << " is cancelled because of failure" << std::endl;
            f = io_requests.erase(f);
        } else {
            f++;
        }
    }
    if( nullptr != current_io && current_io->app == app ) {
        Debug{} << "## Special case, this was an ongoing IO" << std::endl;
        current_io = nullptr;
        select_next_io_task(date);
    }
}
