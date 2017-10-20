#include <vector>
#include <map>
#include <set>
#include <iostream>
#include <assert.h>
#include <math.h>
#include <sstream>
#include <stdlib.h>
#include <sys/time.h>
#include <string>
#include <fstream>

#include "System.h"
#include "AppClass.h"
#include "App.h"
#include "SchedEvent.h"
#include "Schedule.h"
#include "Simulation.h"
#include "Task.h"
#include "Trace.h"
#include <algorithm>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    //Debug::debug = true;
    //std::ofstream ostrm("/tmp/debug");
    //Debug::stream = &ostrm;

    //(const char *name, int _nodes, int _cores, double _band_tot, double _mem_per_node, simt_t _mtbf_sys, simt_t min_duration);
    System system("demo", 300, 1, 1e6, 1e3, 100, 100);
    Schedule s(&system);
    
    //add_app_class(int nb_cores, double input (%mem), double output (%mem), simt_t wall, double io, double ckpt (%mem), double target);
    system.add_app_class(30, 50, 200, 25, 0.0, 20, 0.6);
    system.add_app_class(50, 30, 100, 30, 0.0, 20, 0.4);

    std::cout << "## System: " << system << std::endl;
    for(auto ac: system.classes) {
        std::cout << "##  App Class: " << *ac << std::endl;
    }

    {
        s.clear();
        system.set_fixed_checkpoint_interval(10);
        system.clear();
        PNGTrace t("figure.png", system.nb_nodes);
            
        SimOrderedIOCoop sim(&s, t, 1);
        
        s.reschedule_apps(0);
        simt_t prev_date = 0;
        
        while( sim.step() ) {
            std::cerr << "     " << sim.cur_date() << "\r";
            std::cerr.flush();
            if( sim.cur_date() != prev_date ) {
                prev_date = sim.cur_simt();
                std::stringstream filename;
                filename << "fig-at-" << prev_date << ".png";
                t.output(filename.str(), prev_date);
            }
        }
    }
    exit(0);
}
