#ifndef Task_h
#define Task_h

#include "App.h"

#include <string>
#include <iostream>
#include <assert.h>
#include "Simulation.h"

class Simulation;

class Task {
public:
    typedef enum { NODE_FAULT, APP_FAILURE, CKPT_START, CKPT_END, APP_START, APP_END, IO_START, IO_END } type_t;
    Simulation *sim;
    type_t type;
    simt_t date;

    Task(Simulation *_sim, type_t _type, simt_t _date) :
        sim(_sim),
        type(_type),
        date(_date) {}

    virtual ~Task() { }

    std::string str_type(void) const {
        switch( type ) {
        case Task::NODE_FAULT:
            return std::string("FAULT");
            break;
        case Task::APP_FAILURE:
            return std::string("APP FAILURE");
            break;
        case Task::CKPT_START:
            return std::string("CKPT START");
            break;
        case Task::CKPT_END:
            return std::string("CKPT END");
            break;
        case Task::APP_START:
            return std::string("START");
            break;
        case Task::APP_END:
            return std::string("END");
            break;
        case Task::IO_START:
            return std::string("IO START");
            break;
        case Task::IO_END:
            return std::string("IO END");
        default:
            return std::string("UKNOWN TYPE");
        }
    }
    
    virtual void print(std::ostream &o) const;
    friend std::ostream& operator<< (std::ostream& stream, const Task& task);
    
    virtual bool step(void) = 0;
};

class NodeFaultTask : public Task {
public:
    int node_id;
    NodeFaultTask(Simulation *sim, simt_t _date, int _node) :
        Task(sim, Task::NODE_FAULT, _date),
        node_id(_node) {}

    ~NodeFaultTask() { }

    void print(std::ostream &o) const;

    bool step(void);
};

class AppTask: public Task {
 public:
    App *app;
    
    AppTask(Simulation *sim, Task::type_t kind, simt_t _date, App* _app) :
        Task(sim, kind, _date),
        app(_app) {
            assert(NULL != _app);
        }

    void print(std::ostream &o) const;

    virtual bool vstep(void) = 0;
    bool step(void);
};

class AppStartTask: public AppTask {
public:
    AppStartTask(Simulation *sim, simt_t _date, App* _app) :
        AppTask(sim, Task::APP_START, _date, _app) { }

    ~AppStartTask() { }

    bool vstep(void);
};

class AppFailureTask: public AppTask {
public:
    AppFailureTask(Simulation *sim, simt_t _date, App* _app) :
        AppTask(sim, Task::APP_FAILURE, _date, _app) { }

    ~AppFailureTask() { }

    bool vstep(void);
};

class AppEndTask: public AppTask {
public:
    AppEndTask(Simulation *sim, simt_t _date, App* _app) :
        AppTask(sim, Task::APP_END, _date, _app)  {  }

    ~AppEndTask() { }


    bool vstep(void);
};

class AppTaskIO: public AppTask {
public:
    AppTaskIO(Simulation *sim, Task::type_t type, simt_t _date, App* _app) :
        AppTask(sim, type, _date, _app) {    }

    ~AppTaskIO() { }

    virtual bool vstep(void) = 0;
};

class CkptStartTask: public AppTaskIO {
public:
    CkptStartTask(Simulation *sim, simt_t _date, App* _app) :
        AppTaskIO(sim, Task::CKPT_START, _date, _app) {    }

    ~CkptStartTask() { }

    bool vstep(void);
};

class CkptEndTask: public AppTaskIO {
public:
    CkptEndTask(Simulation *sim, simt_t _date, App* _app) :
        AppTaskIO(sim, Task::CKPT_END, _date, _app) {    }

    ~CkptEndTask() { }

    bool vstep(void);
};

class IOStartTask: public AppTaskIO {
public:
    IOStartTask(Simulation *sim, simt_t _date, App* _app) :
        AppTaskIO(sim, Task::IO_START, _date, _app) {    }

    ~IOStartTask() { }

    bool vstep(void);
};

class IOEndTask: public AppTaskIO {
public:
    IOEndTask(Simulation *sim, simt_t _date, App* _app) :
        AppTaskIO(sim, Task::IO_END, _date, _app) {    }

    ~IOEndTask() { }

    bool vstep(void);
};

#endif
