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

char* getCmdOption(char ** begin, char ** end, const std::string & option, char *default_value = nullptr)
{
    char ** itr = std::find(begin, end, option);
    if (itr != end && ++itr != end)
    {
        return *itr;
    }
    return default_value;
}

unsigned int getCmdOption(char ** begin, char ** end, const std::string & option, unsigned int default_value = 0)
{
    char *opt = getCmdOption(begin, end, option, nullptr);
    if(nullptr == opt) {
        return default_value;
    }
    return atoi(opt);
}

double getCmdOption(char ** begin, char ** end, const std::string & option, double default_value = -1.0)
{
    char *opt = getCmdOption(begin, end, option, nullptr);
    if(nullptr == opt) {
        return default_value;
    }
    std::string::size_type sz;
    double ret = std::stod(opt, &sz);
    if( opt[sz] == '\0' )
        return ret;
    return default_value;
}

bool cmdOptionExists(char** begin, char** end, const std::string& option)
{
    return std::find(begin, end, option) != end;
}

#undef DOUBLE_CHECKS
#define DOUBLE_CHECKS 0

static double sim_and_compute_strategy(System &system, Schedule &s, double segment_size, unsigned int seed, double min_run, double isr, double ier, int runtype)
{
    struct timeval now, before, diff;
    StatTrace t(system.nb_nodes, isr, ier);
    s.clear();

    Simulation *sim = nullptr;
    switch( runtype ) {
    case 1:
        // Proportional Interference with 1h Checkpoint Interval
        system.set_fixed_checkpoint_interval(3600);
        system.clear();
        sim = new SimSimpleInterference(&s, t, seed);
        break;
    case 2:
        // Proportional Interference with Daly Checkpoint Interval
        system.set_daly_checkpoint_interval();
        system.clear();
        sim = new SimSimpleInterference(&s, t, seed);
        break;
    case 3:
        // FCFS Interference with 1h Checkpoint Interval
        system.set_fixed_checkpoint_interval(3600);
        system.clear();
        sim = new SimOrderedIOFCFS(&s, t, seed);
        break;
    case 4:
        // FCFS Interference with Daly Checkpoint Interval
        system.set_daly_checkpoint_interval();
        system.clear();
        sim = new SimOrderedIOFCFS(&s, t, seed);
        break;
    case 5:
        // Blocking FCFS Interference with 1h Checkpoint Interval
        system.set_fixed_checkpoint_interval(3600);
        system.clear();
        sim = new SimOrderedIOBlockingFCFS(&s, t, seed);
        break;
    case 6:
        // Blocking FCFS Interference with Daly Checkpoint Interval
        system.set_daly_checkpoint_interval();
        system.clear();
        sim = new SimOrderedIOBlockingFCFS(&s, t, seed);
        break;
    case 7:
        // Cooperative Interference
        system.set_daly_checkpoint_interval();
        system.clear();
        sim = new SimOrderedIOCoop(&s, t, seed);
        break;
    }
    s.reschedule_apps(0);

    while( sim->step() ) {
#if DOUBLE_CHECKS
        std::cerr << "     " << sim->cur_date() << "         \r";
        std::cerr.flush();
#else
        gettimeofday(&now, NULL);
        timersub(&now, &before, &diff);
        if( diff.tv_sec*1e6 + diff.tv_usec > 5e5 ) {
            std::cerr << "     " << sim->cur_date() << "         \r";
            std::cerr.flush();
            before = now;
        }
#endif
        if( sim->cur_date() > 20 * min_run ) {
            break;
        }
    }
    
    auto r = t.getStat(segment_size, seed);

    delete sim;
    return (std::get<0>(r)+std::get<1>(r))/TIME_UNIT;
}

static double sim_and_compute(double segment_size, unsigned int seed, double min_run, double isr, double ier,
                              double bw, double mtbf, int runtype)
{
    System system("prospection", 50000, 160, bw, 140e9, mtbf/50000.0, min_run);
    Schedule s(&system);

    system.add_app_class(1638400, 0.03, 1.05, 262.4*3600.0, 0.0, 1.6, 0.6);
    system.add_app_class(409600, 0.05, 2.2, 64.0*3600.0, 0.0, 1.85, 0.05);
    system.add_app_class(3276800, 0.7, 0.43, 128.0*3600.0, 0.05, 3.5, 0.15);
    system.add_app_class(3000000, 0.1, 2.7, 157.2*3600.0, 2.0, 0.85, 0.1);

    std::cout << "## System: " << system << std::endl;
    for(auto ac: system.classes) {
        std::cout << "##  App Class: " << *ac << std::endl;
    }

    struct timeval now, before, diff;
    StatTrace t(system.nb_nodes, isr, ier);
    s.clear();

    Simulation *sim = nullptr;
    // baseline
    system.set_fixed_checkpoint_interval(2*min_run);
    system.clear();
    sim = new SimNoInterference(&s, t, seed, false);

    s.reschedule_apps(0);

    while( sim->step() ) {
#if DOUBLE_CHECKS
        std::cerr << "     " << sim->cur_date() << "         \r";
        std::cerr.flush();
#else
        gettimeofday(&now, NULL);
        timersub(&now, &before, &diff);
        if( diff.tv_sec*1e6 + diff.tv_usec > 5e5 ) {
            std::cerr << "     " << sim->cur_date() << "         \r";
            std::cerr.flush();
            before = now;
        }
#endif
        if( sim->cur_date() > 2.0 * min_run ) {
            break;
        }
    }
    
    auto r = t.getStat(segment_size, seed);

    delete sim;

    double basework = (std::get<0>(r)+std::get<1>(r))/TIME_UNIT;
    std::cerr << "At " << bw << ", basework = " << basework;

    double work = sim_and_compute_strategy(system, s, segment_size, seed, min_run, isr, ier, runtype);
    std::cerr << " work = " << work << " (" << work/basework << ")"<<std::endl;
    
    return work/basework;
}

int main(int argc, char *argv[])
{
    static const std::string names[] = {
        std::string("Undefined"),
        std::string("Prop1h"),
        std::string("PropDaly"),
        std::string("FCFS1h"),
        std::string("FCFSDaly"),
        std::string("BlockingFCFS1h"),
        std::string("BlockingFCFSDaly"),
        std::string("Coop")
    };
    /*
    Debug::debug = false;
    std::ofstream ostrm("/tmp/debug");
    Debug::stream = &ostrm;
    */
    struct timeval now;
    gettimeofday(&now, NULL);
    unsigned int seed = (now.tv_usec * getpid()) ^ now.tv_sec;
    seed = getCmdOption(argv, argv+argc, "-s", seed);
    double mtbf = getCmdOption(argv, argv+argc, "-m", 25.0*365.0*24.0*3600.0);
    double START_BW = getCmdOption(argv, argv+argc, "-b", 1e12);
    double MAX_BW = getCmdOption(argv, argv+argc, "-B", 1e15);

    double ignore_start = 24.0*3600.0;               // 1 day
    double ignore_end   = 24.9*3600.0;               // 1 day
    double segment_size = 3.0*31.0*24.0*3600.0;      // 3 months
    double min_run = 1.2*segment_size + ignore_end + ignore_start;

    double isr = ignore_start / min_run;
    double ier = (min_run - ignore_end) / min_run;

    for(int runtype = 7; runtype > 0; runtype--) {
        double min_bw = START_BW;
        double max_bw = START_BW;
        double bw = START_BW;
        bool found_min = false;
        bool found_max = false;
        double ratio = 0.0;
        do {
            ratio = sim_and_compute(segment_size, seed, min_run, isr, ier, bw, mtbf, runtype);
            std::cout << std::endl << "At " << bw << " (between "<< min_bw <<" and "<< max_bw <<" ), runtype = " << names[runtype] << " ratio = " << ratio << std::endl;
            if( ratio > 0.8 ) {
                // too fast
                found_max = true;
                max_bw = bw;
                min_bw = bw = bw / 10.0;
            } else {
                // too slow
                found_min = true;
                min_bw = bw;
                max_bw = bw = bw * 10.0;
            }
        } while( min_bw > 1e3 && max_bw < MAX_BW && (!found_min || !found_max) );
        if( min_bw <= 1e3 ||
            max_bw >= MAX_BW ) {
            std::cout << std::endl << "At " << bw << " (between "<< min_bw <<" and "<< max_bw <<" ), runtype = " << names[runtype] << " 80%ratio = " << ratio << " MTBF = " << mtbf << " s"  << std::endl;
            continue;
        }
        while( max_bw - min_bw > 1e12 ) {
            bw = (min_bw + max_bw) / 2.0;   
            ratio = sim_and_compute(segment_size, seed, min_run, isr, ier, bw, mtbf, runtype);
            std::cout << std::endl << "At " << bw << " (between "<< min_bw <<" and "<< max_bw <<" ), runtype = " << names[runtype] << " ratio = " << ratio << " MTBF = " << mtbf << " s" << std::endl;
            if( ratio > 0.8 ) {
                // too fast
                max_bw = bw;
            } else {
                // too slow
                min_bw = bw;
            }
        }
        std::cout << std::endl << "At " << bw << " (between "<< min_bw <<" and "<< max_bw <<" ), runtype = " << names[runtype] << " 80%ratio = " << ratio << " MTBF = " << mtbf << " s" << std::endl;
    }
        
    exit(0);
}
