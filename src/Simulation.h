#ifndef Simulation_h
#define Simulation_h

#include <stdint.h>
#include <mutex>
#include <iostream>
#include <sstream>
#include <set>

typedef int64_t simt_t;
#define UNDEFINED_DATE ((int64_t)-1)
#define TIME_UNIT 1000.0

#include "System.h"
#include "Schedule.h"
#include "Task.h"
#include "Trace.h"

#include <vector>
#include <map>

class Debug: public std::ostringstream
{
public:
    static bool debug;
    static std::ostream *stream;
    Debug() = default;

    ~Debug()
    {
        if(debug) {
            std::lock_guard<std::mutex> guard(_mutexDebug);
            *stream << this->str();
            stream->flush();
        }
    }

private:
    static std::mutex _mutexDebug;
};

class Task;
class AppTaskIO;
class Schedule;
class Trace;

class Simulation {
public:
    std::multimap<simt_t, Task*>tasks;
    Schedule *schedule;
    std::vector<AppTaskIO *>io_tasks;
    Trace &trace;
    unsigned int seed_fault;
    unsigned int seed_app_order;
    simt_t curdate;
    
    Simulation(Schedule *_sched, Trace &t, unsigned int seed, bool inject_failure = true);
    virtual ~Simulation() {}

    bool step(void);
    void inject_next_fault(simt_t from_date);
    double cur_date(void);
    simt_t cur_simt(void);
    virtual void start_io(simt_t start_date, App *app) = 0;
    virtual void end_io(simt_t start_date, App *app) = 0;
    virtual bool start_ckpt(simt_t start_date, App *app) = 0;
    virtual void end_ckpt(simt_t start_date, App *app) = 0;
    
    virtual void clear_app(App *app,simt_t date);
};

/** SimNoInterference
 *    Theoretical model where the I/O subsystem is perfectly scalable
 */
class SimNoInterference : public Simulation {
public:
 SimNoInterference(Schedule *_sched, Trace &t, unsigned int seed, bool inject_failure = true) :
    Simulation(_sched, t, seed, inject_failure) {}
    ~SimNoInterference();

    void start_io(simt_t start_date, App *app);
    void end_io(simt_t start_date, App *app);
    bool start_ckpt(simt_t start_date, App *app);
    void end_ckpt(simt_t start_date, App *app);
};

/** SimSimpleInterference
 *    Two interfering I/O are slowed down proportionnaly to the
 *    number of nodes doing I/O.
 **/
class SimSimpleInterference : public Simulation {
public:
    simt_t date_of_last_iorate_change;

    SimSimpleInterference(Schedule *_sched, Trace &t, unsigned int seed, bool inject_failure = true) :
    Simulation(_sched, t, seed, inject_failure),
        date_of_last_iorate_change(UNDEFINED_DATE) {}
    ~SimSimpleInterference();

    void start_io(simt_t start_date, App *app);
    void end_io(simt_t start_date, App *app);
    bool start_ckpt(simt_t start_date, App *app);
    void end_ckpt(simt_t start_date, App *app);

    int update_remaining_ios(simt_t date);
    void reschedule_end_ios(int nb_nodes_doing_io, simt_t date);
    void start_remaining_io(simt_t start_date, App *app, AppTaskIO *task);
};

/** SimOrderedIOBlockingFCFS
 *    IO and checkpoint happen in FIFO order, and
 *    checkpoints are blocking
 */
class SimOrderedIOBlockingFCFS : public Simulation {
public:
    simt_t date_of_last_io;
    
    SimOrderedIOBlockingFCFS(Schedule *_sched, Trace &t, unsigned int seed, bool inject_failure = true) :
        Simulation(_sched, t, seed, inject_failure),
        date_of_last_io(UNDEFINED_DATE) {}
        ~SimOrderedIOBlockingFCFS();

    void start_io(simt_t start_date, App *app);
    void end_io(simt_t start_date, App *app);
    bool start_ckpt(simt_t start_date, App *app);
    void end_ckpt(simt_t start_date, App *app);
};

/** SimOrderedIOFCFS
 *    If an application wants to do some I/O, it gets in the queue.
 *    The checkpoint start is moved down only when it's time.
 **/
class SimOrderedIOFCFS : public Simulation {
public:
    simt_t date_of_last_iorate_change;

    SimOrderedIOFCFS(Schedule *_sched, Trace &t, unsigned int seed, bool inject_failure = true) :
    Simulation(_sched, t, seed, inject_failure) {}
    ~SimOrderedIOFCFS();

    void start_io(simt_t start_date, App *app);
    void end_io(simt_t start_date, App *app);
    bool start_ckpt(simt_t start_date, App *app);
    void end_ckpt(simt_t start_date, App *app);
};

/** SimOrderedIOCoop
 *    If an application wants to do some I/O, it gets in the queue.
 *    The checkpoint start is moved down only when it's time.
 *    The selected I/O is done using the paper's heuristic.
 **/
class SimOrderedIOCoop : public Simulation {
public:
    typedef struct io_request_s {
        App *app;
        simt_t requested_start_date;
        bool checkpoint;
        bool operator<(const struct io_request_s &b) const;
    } io_request_t;

    std::set<io_request_t> io_requests;
    AppTaskIO *current_io; /* Can be a START_CKPT to actually start a checkpoint
                            * or END_CKPT / END_IO to know the date of the end of the checkpoint
                            * We don't store anything in io_tasks */
    
    SimOrderedIOCoop(Schedule *_sched, Trace &t, unsigned int seed, bool inject_failure = true) :
    Simulation(_sched, t, seed, inject_failure),
        io_requests(),
        current_io(nullptr) {}
    ~SimOrderedIOCoop();

    double heuristic(simt_t date, io_request_t ior);
    void select_next_io_task(simt_t start_date);
    
    void start_io(simt_t start_date, App *app);
    void end_io(simt_t start_date, App *app);
    bool start_ckpt(simt_t start_date, App *app);
    void end_ckpt(simt_t start_date, App *app);

    void clear_app(App *app, simt_t date);
};

#endif
