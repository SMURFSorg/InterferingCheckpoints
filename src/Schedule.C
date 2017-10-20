#include "Schedule.h"

#include <iostream>
#include <sstream>
#include <math.h>

#include "SchedEvent.h"
#include "App.h"
#include "AppClass.h"
#include "System.h"
#include "Simulation.h"

extern "C" {
#include <png.h>
}


Schedule::Schedule(System *sys) :
    s(sys),
    scheduling()
{
    SchedEvent *all_free = new SchedEvent(s->nb_nodes);
    scheduling.insert( std::pair<simt_t, SchedEvent* >( 0, all_free ) );
}

Schedule::~Schedule()
{
    auto se = scheduling.begin();
    while( se != scheduling.end() ) {
        delete se->second;
        se = scheduling.erase(se);
    }
    scheduling.clear();
}

void Schedule::clear()
{
    auto se = scheduling.begin();
    while( se != scheduling.end() ) {
        delete se->second;
        se = scheduling.erase(se);
    }
    scheduling.clear();
    SchedEvent *all_free = new SchedEvent(s->nb_nodes);
    scheduling.insert( std::pair<simt_t, SchedEvent* >( 0, all_free ) );
}

void Schedule::print(std::ostream &o)
{
    std::set<App*>shown;
    for(auto se : scheduling) {
        shown.clear();
        o << "At " << se.first << ": ";
        for(auto a: se.second->apps) {
            if( NULL != a && shown.find(a) == shown.end() ) {
                o << a->app_index << "(" << a->nb_nodes << ") ";
                shown.insert(a);
            }
        }
        o << std::endl;
    }
}
    
int Schedule::print(const std::string filename = std::string("sched.png"), simt_t at_date = 0)
{
    int code = 0;
    FILE *fp = NULL;
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    png_bytep row = NULL;
    unsigned char *nb = NULL;
    int width = s->nb_nodes;
    int h = 0;
    simt_t last = scheduling.rbegin()->first;
    double hfactor = 1.0;
    int height;
    do {
        height = (int)(ceil(last/hfactor));
        hfactor += 1.0;
    } while( height > width );
    hfactor -= 1.0;
    hfactor = 817.0;
    height = 300;

    fp = fopen(filename.c_str(), "wb");
    if (fp == NULL) {
        std::cerr << "#Could not open file " << filename << " for writing" << std::endl;
        code = 1;
        goto finalise;
    }

    // Initialize write structure
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png_ptr == NULL) {
        std::cerr << "#Could not allocate write struct" << std::endl;
        code = 1;
        goto finalise;
    }

    // Initialize info structure
    info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL) {
        std::cerr << "#Could not allocate info struct" << std::endl;
        code = 1;
        goto finalise;
    }
    
    // Setup Exception handling
    if (setjmp(png_jmpbuf(png_ptr))) {
        std::cerr << "#Error during png creation" << std::endl;
        code = 1;
        goto finalise;
    }
    png_init_io(png_ptr, fp);

    // Write header (8 bit colour depth)
    png_set_IHDR(png_ptr, info_ptr, width, height,
                 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    png_write_info(png_ptr, info_ptr);
    // Allocate memory for one row (3 bytes per pixel - RGB)
    row = (png_bytep) malloc(3 * width * sizeof(png_byte));
    nb = (unsigned char*)malloc(width * sizeof(unsigned char));

    for(h = 0; h < height; h++) {
        simt_t mint = ceil(hfactor * h);
        simt_t maxt = ceil(hfactor * (h+1));
        memset(row, 0, 3*width*sizeof(png_byte));
        memset(nb, 0, width*sizeof(unsigned char));

        auto se = scheduling.lower_bound(mint);
        if( se->first > mint ) {
            se--;
        }
        for(;
            se != scheduling.end() && se->first < maxt;
            se++) {
            for(auto app : se->second->apps) {
                for(auto n : app->nodes)
                    nb[n]++;
            }
        }
        se = scheduling.lower_bound(mint);
        if( se->first > mint ) {
            se--;
        }
        for(;
            se != scheduling.end() && se->first < maxt;
            se++) {
            for(auto app : se->second->apps) {
                for(auto n : app->nodes)
                    app->color(&(row[n*3]), 1.0/nb[n]);
            }
        }
        if( mint <= at_date && maxt >= at_date ) {
            memset(row, 0xFF, 3*width*sizeof(png_byte));
        }
        png_write_row(png_ptr, row);
    }
   
    // End write
    png_write_end(png_ptr, NULL);
 finalise:
    if (fp != NULL) fclose(fp);
    if (info_ptr != NULL) png_free_data(png_ptr, info_ptr, PNG_FREE_ALL, -1);
    if (png_ptr != NULL) png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
    if (row != NULL) free(row);
    if (nb != NULL) free(nb);
    
    return code;
}

/**
 * Return true iff all the ranks in candidates are free between at_date and at_date + wall_time
 */
bool Schedule::app_fits(App *app, simt_t at_date, std::vector<int> *candidates)
{
    auto se = scheduling.lower_bound(at_date);
    do {
        for(auto i : *candidates)
            if( se->second->occ[i] )
                return false;
        se++;
        if( se == scheduling.end() )
            return true;
    } while( se->first < at_date + app->wall_time );
    return true;
}

bool Schedule::node_remains_free(int n, simt_t from_date, simt_t to_date)
{
    auto se = scheduling.lower_bound(from_date);
    do {
        if( se->second->occ[n] )
            return false;
        se++;
        if( se == scheduling.end() )
            return true;
    } while( se->first < to_date );
    return true;
}

/**
 * Returns the list of nodes that fit Application app on the first
 * scheduling events at at_date */
std::vector<int> *Schedule::app_fits(App *app, simt_t at_date)
{
    /* Find the scheduling event at at_date. There is one. */
    auto se = scheduling.lower_bound(at_date);
    if( se->first != at_date ) throw std::runtime_error("There is no event for the starting date (error calling app_fits)");

    /* Quick pass: do we have enough free nodes between at_date and at_date+app->wall_time? */
    do {
        int nb_free = se->second->occ.size();
        for(auto a2: se->second->apps) {
            nb_free -= a2->nb_nodes;
            if( nb_free < app->nb_nodes ) {
                break;
            }
        }
        /* There are not enough free nodes at the date se, then we can't schedule
         * app at this date */
        if(nb_free < app->nb_nodes)
            return NULL;
        se++;
    } while( se != scheduling.end() && se->first < at_date + app->wall_time );

    /* Slow pass: do we have app->nb_nodes nodes that remain free during this entire time? 
     * Let's start again */
    se = scheduling.lower_bound(at_date);
    /* We're building a list of candidate nodes */
    auto candidates = new std::vector<int>(app->nb_nodes, -1);

    /* Let's try something fast first:
     * find the first nb_nodes nodes that are free in se, and see if they fit */
    int i = 0, j = 0, k = 0;
    for(i = 0; i < s->nb_nodes - app->nb_nodes; i++) {
        if( se->second->occ[i] == false ) {
            (*candidates)[k++] = i;
            for(j = i+1; (k < app->nb_nodes) && (j < s->nb_nodes); j++) {
                if( se->second->occ[j] == false )
                    (*candidates)[k++] = j;
            }
            /* If we found enough free nodes, and they fit from at_date to at_date+wall_time
             * then we found a place for app */
            if(k == app->nb_nodes) {
                if(app_fits(app, at_date, candidates))
                    return candidates;
            }
        }
        /* They don't fit */
        break;
    }
   
    /* Let's take the time to try each node independently */
    k = 0;
    for(int i = 0; k < app->nb_nodes && i < s->nb_nodes; i++) {
        if( se->second->occ[i] == false && node_remains_free(i, at_date, at_date + app->wall_time) )
            (*candidates)[k++] = i;
    }
    if( k == app->nb_nodes )
        return candidates;
    
    /* There was enough nodes during the entire execution, but this would require
     * migrating applications at different time steps. This is not a good fit */
    delete candidates;
    return NULL;
}

/**
 * Change the date of completion of an application to new_end_date
 * Where new_end_date < app->end_date
 */
void Schedule::update_sched_event(App *app, simt_t new_end_date)
{
    if( new_end_date < app->end_date ) {
        /* begin is the event that stores the new_end_date (if there is one)
         * end is the event that stores the current end date of app
         * (and there must be one)
         */
        auto begin = scheduling.find(new_end_date);
        auto end   = scheduling.find(app->end_date);
        if( end == scheduling.end() ) throw std::runtime_error("There must be a scheduling event for the end of the application");
        std::vector<App*> apps = s->apps;

        /* If there is no event for new_end_date, we create one */
        if( begin == scheduling.end() ) {
            /* We take the event precending new_end_date */
            auto se = scheduling.lower_bound(new_end_date);
            se--;

            /* Copy it into a new event, removing just app since it's
             * going to end at new_end_date */
            SchedEvent *scopy = new SchedEvent(se->second);
            for(auto n: app->nodes) {
#if DOUBLE_CHECKS
                if( scopy->occ[n] == false ) throw std::runtime_error("Node is already occupied, so application should not fit");
#endif
                scopy->occ[n] = false;
            }
            auto ap_it = scopy->apps.find(app);
            if( ap_it == scopy->apps.end() ) throw std::runtime_error("Application must belong to scopy as scopy is the last scheduling event that holds it");
            scopy->apps.erase(ap_it);

            /* And insert it to the new end date */
            auto p = scheduling.insert(std::pair<simt_t, SchedEvent*>(new_end_date, scopy));
            if( p.second == false ) throw std::runtime_error("Scheduling event should not already be in the schedule.");
            begin = p.first;

            /* We already removed app at begin, so let's go to
             * the next one before we finish iterating */
            begin++;
        }

        /* For all the events between begin and end, we remove app and mark the
         * nodes free */
        while(begin != end) {
            auto se = *begin;
            for(auto n: app->nodes) {
#if DOUBLE_CHECKS
                if( se.second->occ[n] == false ) throw std::runtime_error("Node is not occupied by an application that belongs to it");
#endif
                se.second->occ[n] = false;
            }
            auto ap_it = se.second->apps.find(app);
            if( ap_it == se.second->apps.end() ) throw std::runtime_error("Application must belong to se.second as se.second is the last scheduling event that holds it");
            se.second->apps.erase(ap_it);
            begin++;
        }

        /* Then we can update the end date of app, and its computation time */
        app->end_date = new_end_date;
        app->wall_time = app->end_date - app->start_date;
        
        /* And since we created a hole in the scheduling, we reschedule all applications
         * at new_end_date or after */
        remove_events_at_date(new_end_date);
        reschedule_apps(new_end_date);
    } else {
        remove_events_at_date(app->end_date);        
        auto se = scheduling.find(app->end_date);
        while( se != scheduling.end() && se->first < new_end_date ) {
            for(auto n: app->nodes) {
#if DOUBLE_CHECKS
                if( se->second->occ[n] == true ) throw std::runtime_error("Node is already occupied, so application should not fit");
#endif
                se->second->occ[n] = true;
            }
            se->second->apps.insert(app);
            se++;
        }
        if( se == scheduling.end() || se->first != new_end_date ) {
            se--;
            SchedEvent *scopy = new SchedEvent(se->second);
            for(auto n: app->nodes) {
#if DOUBLE_CHECKS
                if( scopy->occ[n] == false ) throw std::runtime_error("Node is not occupied by application that belongs to it");
#endif
                scopy->occ[n] = false;
            }
            auto ap_it = scopy->apps.find(app);
            if( ap_it == scopy->apps.end() ) throw std::runtime_error("Application must belong to scopy as scopy is the last scheduling event that holds it");
            scopy->apps.erase(ap_it);
            
            /* And insert it to the new end date */
            scheduling.insert(std::pair<simt_t, SchedEvent*>(new_end_date, scopy));
        }
        
        /* And since we created a hole in the scheduling, we reschedule all applications
         * at app->end_date or after */
        simt_t end_date = app->end_date;
        app->end_date = UNDEFINED_DATE; /* So that schedule creates the scheduled end task */
        app->schedule(app->start_date, new_end_date);
        reschedule_apps(end_date);
    }
}

void Schedule::remove_events_at_date(simt_t at_date)
{
    std::set<App*> apps_to_remove;
    std::set<simt_t> events_to_keep;
    std::vector<App*> apps = s->apps;

    /* First, remove all scheduling events corresponding to applications
     * that start after at_date;
     * Remember start and end date of all applications that started before
     * at_date, as the corresponding scheduling events should be kept */
    for(auto se = scheduling.begin(); se !=  scheduling.end(); ) {
        for(auto app: se->second->apps) {
            /* In this phase, just remember that the event needs to be kept if
             * the app started before this scheduling event; and that the app
             * must be removed if it started after */
            if( app->start_date > se->first ||
                app->end_date < se->first )
                throw std::runtime_error("This app does not intersect with its scheduling event");
            if( app->start_date < at_date ) {
                events_to_keep.insert(app->start_date);
                events_to_keep.insert(app->end_date);
            } else {
                apps_to_remove.insert(app);
            }
        }
        if( at_date > 0 && events_to_keep.find(se->first) == events_to_keep.end() ) {
            /* Now, if no app wants to keep this event, delete it */
            delete se->second;
            se = scheduling.erase(se);
        } else {
            /* But if there is a reason to keep this event, clean it:
             * remove only the apps that started after at_date from this event */
            for(auto app = se->second->apps.begin(); app != se->second->apps.end(); ) {
                if( (*app)->start_date >= at_date ) {
                    for(auto n: (*app)->nodes) {
#if DOUBLE_CHECKS
                        if( se->second->occ[n] == false ) throw std::runtime_error("Node is not occupied by application that belongs to it");
#endif
                        se->second->occ[n] = false;
                    }
                    app = se->second->apps.erase(app);
                } else {
                    app++;
                }
            }
            se++;
        }
    }
    /* The apps in apps_to_remove have been removed from all scheduling events;
     * however, we need to remove the corresponding start/end tasks from the
     * simulation, and to clean what nodes the app was scheduled on */
    for(App *app : apps_to_remove) {
        app->unschedule(at_date);
        if( (int)app->nodes.size() != app->nb_nodes )
            throw std::runtime_error("inconsistent number of nodes occupied by application");
        app->nodes.clear();
    }
}

bool Schedule::all_nodes_busy_between(simt_t start, simt_t end, const std::vector<int> *nodes)
{
    for(auto n : *nodes) {
        auto se = scheduling.lower_bound(start);
        while( se->first < end ) {
            if( se->second->occ[n] )
                break;
            se++;
        }
        if( se->first >= end ) {
            return false;
        }
    }
    return true;
}

/**
 * Reschedules all applications that did not start at date at_date,
 */
void Schedule::reschedule_apps(simt_t at_date)
{
    /* Now, we iterate over all applications, and find a slot after a_date where
     * to put the ones that are not scheduled anymore. We consider each app in the
     * order defined by apps. */
    for(auto app : s->apps) {
        //int milli = 0;
        //char mill[] = {'|', '/', '-', '\\' };
        //std::cerr << mill[milli] << '\r'; milli = (milli+1) % 4;
        if( app->start_date == UNDEFINED_DATE ) {
            auto se = scheduling.lower_bound(at_date);
            /* We try each existing scheduling event after (or at) at_date,
             * and see if there is a set of nodes that will fit app.
             * Because the last scheduling event is the removal of the last
             * application according to the current partial scheduling, there
             * is always a scheduling event before end() that fits. */
            std::vector<int> *nodes = NULL;
            while(NULL == nodes) {
                nodes = app_fits(app, se->first);
                if(NULL == nodes) se++;
                if( se == scheduling.end() ) throw std::runtime_error("The schedule is not well terminated with an event where all nodes are free");
            }

            if( all_nodes_busy_between(at_date, se->first, nodes) ) {
                /* It is not necessary to schedule this app: all the nodes it would use
                 * are already used between at_date and se->first, so we're going to
                 * reschedule it later */
                delete nodes;
                continue;
            }
            
            /* Copy the nodes found into the app structure */
            app->nodes = *nodes;
            delete nodes;
            /* Push the start and end task into the simulation */
            app->schedule(se->first, se->first + app->wall_time);
            do {
                /* And for the duration of the application, add the app to the
                 * scheduling event, marking each node as occupied as we go along */
                for(auto i : app->nodes) {
#if DOUBLE_CHECKS
                    if( se->second->occ[i] == true ) throw std::runtime_error("Node is already occupied, so application should not fit");
#endif
                    se->second->occ[i] = true;
                }
                if( app->start_date > se->first || app->end_date < se->first )
                    throw std::runtime_error("Application must intersect with scheduling event");
                se->second->apps.insert(app);
                se++;
            } while(se != scheduling.end() && se->first < app->end_date);
            /* If we reached the current end, or if we stopped before an existing
             * scheduling event, we need to create a scheduling events that marks
             * the end of this application */
            if( se == scheduling.end() ||
                se->first != app->end_date ) {
                /* We copy the previous scheduling event as the new event */
                se--;
                SchedEvent *scopy = new SchedEvent(se->second);
                /* But remove from the new event the application that just completed */
                for(auto i : app->nodes) {
                    scopy->occ[i] = false;
                }
                auto ap_it = scopy->apps.find(app);
                if( ap_it == scopy->apps.end() ) throw std::runtime_error("Application must belong to scheduling event");
                scopy->apps.erase(ap_it);
                /* And insert that event at the application completion date */
                scheduling.insert(std::pair<simt_t, SchedEvent*> (app->start_date + app->wall_time, scopy));
            }
        }
    }
    /*
      std::stringstream filename;
      filename << "figure-at" << at_date << ".png";
      print( filename.str(), at_date );
    */
}
