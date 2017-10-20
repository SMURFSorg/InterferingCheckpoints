#include "Trace.h"
#include "Task.h"
#include "AppClass.h"

#include <math.h>

extern "C" {
#include <png.h>
}

PNGTrace::PNGTrace(const char *filename, int nb_nodes) :
    filename(filename),
    all_events(),
    pmap(),
    nb_nodes(nb_nodes),
    max_date(0)
{
}

void PNGTrace::output(const std::string filename, simt_t at_date)
{
    if(0 == max_date || 0 == nb_nodes) {
        std::cerr << "Trace did not receive enough events to determine size; bailing out" << std::endl;
        return;
    }

    FILE *fp = NULL;
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    png_bytep row = NULL;
    png_bytep white_row = NULL;
    int h = 0;
    int hfactor = 817;
    int height;
    typedef enum { EMPTY, RUNNING, IO, CKPT } node_state_t;
    typedef struct {
        app_id_t app_id;
        node_state_t state;
    } node_info_t;
    std::vector<node_info_t>node_state;
    auto se = all_events.begin();

    while(max_date / hfactor > nb_nodes) {
        hfactor++;
    }
    height = (max_date+hfactor-1)/hfactor;
    height = 300;
    fp = fopen(filename.c_str(), "wb");
    if (fp == NULL) {
        std::cerr << "#Could not open file " << filename << " for writing" << std::endl;
        goto finalise;
    }

    // Initialize write structure
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png_ptr == NULL) {
        std::cerr << "#Could not allocate write struct" << std::endl;
        goto finalise;
    }

    // Initialize info structure
    info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL) {
        std::cerr << "#Could not allocate info struct" << std::endl;
        goto finalise;
    }
    
    // Setup Exception handling
    if (setjmp(png_jmpbuf(png_ptr))) {
        std::cerr << "#Error during png creation" << std::endl;
        goto finalise;
    }
    png_init_io(png_ptr, fp);

    // Write header (8 bit colour depth)
    png_set_IHDR(png_ptr, info_ptr, nb_nodes, height,
                 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    png_write_info(png_ptr, info_ptr);
    // Allocate memory for one row (3 bytes per pixel - RGB)
    row = (png_bytep) malloc(3 * nb_nodes * sizeof(png_byte));
    white_row = (png_bytep) malloc(3*nb_nodes*sizeof(png_byte));
    memset(row, 0, 3*nb_nodes*sizeof(png_byte));
    memset(white_row, 0xFF, 3*nb_nodes*sizeof(png_byte));
    
    for(int n = 0; n < nb_nodes; n++) {
        node_info_t ns;
        ns.state = EMPTY;
        ns.app_id.app_index = -1;
        ns.app_id.instance_index = -1;
        node_state.push_back(ns);
    }
    
    for(h = 0; h < height; h++) {
        simt_t maxt = hfactor * (h+1) - 1;
        for(;
            se != all_events.end() && se->date < maxt;
            se++) {
            node_info_t ns;
            switch( se->type ) {
            case Task::APP_END:
            case Task::APP_FAILURE:
                ns.state = EMPTY;
                ns.app_id.app_index = -1;
                ns.app_id.instance_index = -1;
                break;
            case Task::APP_START:
            case Task::CKPT_END:
            case Task::IO_END:
                ns.state = RUNNING;
                ns.app_id = se->app_id;
                break;
            case Task::CKPT_START:
                ns.state = CKPT;
                ns.app_id = se->app_id;
                break;
            case Task::IO_START:
                ns.state = IO;
                ns.app_id = se->app_id;
                break;
            }
            for(auto n: pmap.at(se->app_id).nodes) {
                node_state[n] = ns;
            }
        }
        for(int n = 0; n < nb_nodes; n++) {
            switch( node_state[n].state ) {
            case EMPTY:
                row[n*3] = 0;
                row[n*3+1] = 0;
                row[n*3+2] = 0;
                break;
            case CKPT:
                row[n*3] = 0xFF;
                row[n*3+1] = 0xFF;
                row[n*3+2] = 0;
                break;
            case IO:
                row[n*3] = 0xFF;
                row[n*3+1] = 0;
                row[n*3+2] = 0;
                break;
            case RUNNING:
                app_t ap = pmap.at(node_state[n].app_id);
                row[n*3] = ap.r;
                row[n*3+1] = ap.g;
                row[n*3+2] = ap.b;
                break;
            }
        }
        if( at_date <= hfactor * h ) {
            png_write_row(png_ptr, white_row);
            memset(white_row, 0x0, 3*nb_nodes*sizeof(png_byte));
        } else {
            png_write_row(png_ptr, row);
        }
    }
    // End write
    png_write_end(png_ptr, NULL);
 finalise:
    if (fp != NULL) fclose(fp);
    if (info_ptr != NULL) png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
    if (png_ptr != NULL) png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
    if (row != NULL) free(row);
    if (white_row != NULL) free(white_row);
}

PNGTrace::~PNGTrace()
{
    output(filename, max_date);
}

PNGTrace &PNGTrace::operator<<(const Task *task)
{
    Trace::operator<<(task);
    if(task->date > max_date)
        max_date = task->date;

    assert(task->type != Task::NODE_FAULT);

    const AppTask *at = static_cast<const AppTask*>(task);
    app_id_t app_id;
    app_id.app_index = at->app->app_index;
    app_id.instance_index = at->app->instance_index;
    if( pmap.count(app_id) == 0 ) {
        app_t app_desc;
        app_desc.nodes = at->app->nodes;
        app_desc.r = at->app->r;
        app_desc.g = at->app->g;
        app_desc.b = at->app->b;
        pmap.insert(std::pair<app_id_t, app_t>(app_id, app_desc));
    }
    event_t e;
    e.app_id = app_id;
    e.type = task->type;
    e.date = at->date;
    all_events.push_back(e);
    return *this;
}

std::tuple<simt_t, simt_t, simt_t, simt_t, simt_t> StatTrace::getStat(simt_t intv_length, unsigned int seed)
{
    simt_t res_ckpt = 0;
    simt_t res_io = 0;
    simt_t res_work = 0;
    simt_t res_wasted = 0;

    simt_t min_date = ignore_start * last_event;
    simt_t max_date = ignore_end * last_event;

    intv_length *= TIME_UNIT;
    if( max_date - min_date < intv_length ) {
        std::cerr << "Usable interval: " << min_date / TIME_UNIT << " - " << max_date / TIME_UNIT << " = " << (max_date-min_date)/TIME_UNIT/3600 << "h"
                  << " is smaller than requested interval " << intv_length << std::endl;
        throw std::runtime_error("Interval too big");
    }
    simt_t offset = (simt_t)floor(((double)rand_r(&seed)/RAND_MAX) * (max_date - min_date - intv_length));
    min_date = min_date + offset;
    max_date = min_date + intv_length;
    
    while( !stat_event.empty() ) {
        stat_event_t ev = stat_event.front();
        stat_event.pop_front();

        if( ev.event_date > max_date )
            continue;
        if( ev.event_date + ev.event_duration < min_date )
            continue;
        simt_t duration;
        if( ev.event_date + ev.event_duration > max_date )
            duration = max_date - ev.event_date;
        else if ( ev.event_date < min_date )
            duration = ev.event_date + ev.event_duration - min_date;
        else
            duration = ev.event_duration;
        
        auto app = app_status.find(ev.app_id);
        assert(app != app_status.end());
        app_status_t app_status = app->second;
        
        switch( ev.event_type ) {
        case LIMBO:
            assert(0);
            break;
        case WORK:
            res_work += app_status.nb_nodes * duration;
            break;
        case CKPT:
            res_ckpt += app_status.nb_nodes * duration;
            break;                
        case IO:
            res_io += app_status.nb_nodes * duration;
            break;
        case WASTING:
            res_wasted += app_status.nb_nodes * duration;
            break;
        }
    }
    
    simt_t res_total = (max_date - min_date) * nb_nodes;
    return {res_work, res_io, res_ckpt, res_wasted, res_total};
}

void StatTrace::interrupt_action(const AppTask *t, app_action_t new_act) {
    auto ai = app_status.find(t->app->app_index);
    assert(ai != app_status.end());
    if(new_act == WASTING) {
        for(auto pe = stat_event.rbegin(); pe != stat_event.rend(); pe++) {
            if(pe->app_id == ai->first) {
                if(pe->event_type == CKPT)
                    break;
                pe->event_type = WASTING;
            }
        }
        ai->second.current_action = WASTING;
        new_act = IO;
    }
    
    switch( ai->second.current_action ) {
    case LIMBO:
    case WASTING:
        break;
    case WORK:
    case CKPT:
    case IO:
        if( ai->second.start_action_date == t->date)
            break;
        stat_event_t ev;
        ev.event_date = ai->second.start_action_date;
        assert(t->date > ai->second.start_action_date);
        ev.event_duration = t->date - ai->second.start_action_date;
        ev.event_type = ai->second.current_action;
        ev.app_id = t->app->app_index;
        stat_event.push_back(ev);
        break;
    }
    ai->second.start_action_date = t->date;
    ai->second.current_action = new_act;
}

StatTrace &StatTrace::operator <<(const Task *task) {
    //Trace::operator<<(task);
    if( task->date > last_event )
        last_event = task->date;
    if( task->type != Task::NODE_FAULT ) {
        const AppTask *t = static_cast<const AppTask*>(task);
        switch(t->type) {
        case Task::NODE_FAULT:
            assert(0);
            break;
        case Task::CKPT_END:
        case Task::IO_END:
            interrupt_action(t, WORK);
            break;
        case Task::APP_FAILURE:
            interrupt_action(t, WASTING);
            break;
        case Task::APP_END:
            interrupt_action(t, LIMBO);
            break;
        case Task::CKPT_START:
            interrupt_action(t, CKPT);
            break;
        case Task::APP_START:
            {
                auto f = app_status.find(t->app->app_index);
                if( f == app_status.end() ) {
                    app_status_t as;
                    as.nb_nodes = t->app->nb_nodes;
                    as.current_action = LIMBO;
                    as.start_action_date = UNDEFINED_DATE;
                    app_status.insert( std::pair<int, app_status_t>(t->app->app_index, as));
                } else {
                    f->second.current_action  = LIMBO;
                    f->second.start_action_date = UNDEFINED_DATE;
                }
            }
            break;
        case Task::IO_START:
            interrupt_action(t, IO);
            break;
        }
    }
    return *this;
}
