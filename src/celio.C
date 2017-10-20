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
#define DOUBLE_CHECKS 1

int main(int argc, char *argv[])
{
    /*
      Debug::debug = true;
      std::ofstream ostrm("/tmp/debug");
      Debug::stream = &ostrm;
    */
    struct timeval now, before, diff;
    bool coop = true, fcfs = true, no = true, simple = true, baseline = true, header = true, blockingfcfs = true;
    gettimeofday(&now, NULL);
    unsigned int seed = (now.tv_usec * getpid()) ^ now.tv_sec;
    seed = getCmdOption(argv, argv+argc, "-s", seed);
    double bw = getCmdOption(argv, argv+argc, "-b", 1e12);
    double mtbf = getCmdOption(argv, argv+argc, "-m", 24.0*3600.0);
    unsigned int N = getCmdOption(argv, argv+argc, "-n", (unsigned int)1);
    double ckpt_interval = getCmdOption(argv, argv+argc, "-c", -1.0);

    double ignore_start = 24.0*3600.0;               // 1 day
    double ignore_end   = 24.9*3600.0;               // 1 day
    double segment_size = 1.0*31.0*24.0*3600.0;      // 2 months
    double min_run = 1.2*segment_size + ignore_end + ignore_start;

    double isr = ignore_start / min_run;
    double ier = (min_run - ignore_end) / min_run;
    bool converged;
    
    if( cmdOptionExists(argv, argv+argc, "-C") ) coop = false;
    if( cmdOptionExists(argv, argv+argc, "-F") ) fcfs = false;
    if( cmdOptionExists(argv, argv+argc, "-N") ) no = false;
    if( cmdOptionExists(argv, argv+argc, "-S") ) simple = false;
    if( cmdOptionExists(argv, argv+argc, "-B") ) baseline = false;
    if( cmdOptionExists(argv, argv+argc, "-BF") ) blockingfcfs = false;
    if( cmdOptionExists(argv, argv+argc, "-H") ) header = false;
    
    System system("cielo", 17784, 16, bw, 32e9, mtbf, min_run);
   
    Schedule s(&system);
    
    system.add_app_class(16384, 0.03, 1.05, 262.4*3600.0, 0.0, 1.6, 0.6);
    system.add_app_class(4096, 0.05, 2.2, 64.0*3600.0, 0.0, 1.85, 0.05);
    system.add_app_class(32768, 0.7, 0.43, 128.0*3600.0, 0.05, 3.5, 0.15);
    system.add_app_class(30000, 0.1, 2.7, 157.2*3600.0, 20.0, 0.85, 0.1);

    if( header ) {
        std::cout << "## System: " << system << std::endl;
        for(auto ac: system.classes) {
            std::cout << "##  App Class: " << *ac << std::endl;
        }
    }

    before = now;
    for(unsigned int n = 0; n < N; n++) {
        if( baseline ) {
            s.clear();
            system.set_fixed_checkpoint_interval(2*min_run);
            system.clear();
            StatTrace t(system.nb_nodes, isr, ier);
            
            SimNoInterference sim(&s, t, seed, false);
        
            s.reschedule_apps(0);

            converged = true;
            while( sim.step() ) {
#if DOUBLE_CHECKS
                std::cerr << "     " << sim.cur_date() << "         \r";
                std::cerr.flush();
#else
                gettimeofday(&now, NULL);
                timersub(&now, &before, &diff);
                if( diff.tv_sec*1e6 + diff.tv_usec > 5e5 ) {
                    std::cerr << "     " << sim.cur_date() << "         \r";
                    std::cerr.flush();
                    before = now;
                }
#endif
                if( sim.cur_date() > 20.0 * min_run ) {
                    converged = false;
                    break;
                }
            }
        
            auto r = t.getStat(segment_size, seed);
            std::cout << (converged ? "" : "#")
                      << "baseline nofaultnoint: WORK/IO/CKPT/WASTED/TOTAL (s.node) "
                      << std::get<0>(r)/TIME_UNIT << " "
                      << std::get<1>(r)/TIME_UNIT << " "
                      << std::get<2>(r)/TIME_UNIT << " "
                      << std::get<3>(r)/TIME_UNIT << " "
                      << std::get<4>(r)/TIME_UNIT << " "
                      << "Seed: " << seed << " "
                      << "Convergence: " << converged
                      << std::endl;
        }
        if( ckpt_interval != -1.0 ) {
            system.set_fixed_checkpoint_interval(ckpt_interval);
        } else {
            system.set_daly_checkpoint_interval();
        }
        if( coop ) {
            s.clear();
            system.clear();
            StatTrace t(system.nb_nodes, isr, ier);
            
            SimOrderedIOCoop sim(&s, t, seed);
        
            s.reschedule_apps(0);
        
            converged = true;
            while( sim.step() ) {
#if DOUBLE_CHECKS
                std::cerr << "     " << sim.cur_date() << "         \r";
                std::cerr.flush();
#else
                gettimeofday(&now, NULL);
                timersub(&now, &before, &diff);
                if( diff.tv_sec*1e6 + diff.tv_usec > 5e5 ) {
                    std::cerr << "     " << sim.cur_date() << "         \r";
                    std::cerr.flush();
                    before = now;
                }
#endif
                if( sim.cur_date() > 20.0 * min_run ) {
                    converged = false;
                    break;
                }
            }
        
            auto r = t.getStat(segment_size, seed);
            std::cout << "Coop Interference: WORK/IO/CKPT/WASTED/TOTAL (s.node) "
                      << std::get<0>(r)/TIME_UNIT << " "
                      << std::get<1>(r)/TIME_UNIT << " "
                      << std::get<2>(r)/TIME_UNIT << " "
                      << std::get<3>(r)/TIME_UNIT << " "
                      << std::get<4>(r)/TIME_UNIT << " "
                      << "Seed: " << seed << " "
                      << "Convergence: " << converged
                      << std::endl;
        }
        if(fcfs) {
            s.clear();
            system.clear();
            StatTrace t(system.nb_nodes, isr, ier);
            
            SimOrderedIOFCFS sim(&s, t, seed);
            
            s.reschedule_apps(0);
        
            converged = true;
            while( sim.step() ) {
#if DOUBLE_CHECKS
                std::cerr << "     " << sim.cur_date() << "         \r";
                std::cerr.flush();
#else
                gettimeofday(&now, NULL);
                timersub(&now, &before, &diff);
                if( diff.tv_sec*1e6 + diff.tv_usec > 5e5 ) {
                    std::cerr << "     " << sim.cur_date() << "         \r";
                    std::cerr.flush();
                    before = now;
                }
#endif
                if( sim.cur_date() > 20.0 * min_run ) {
                    converged = false;
                    break;
                }
            }
        
            auto r = t.getStat(segment_size, seed);
            std::cout << "FCFS Interference: WORK/IO/CKPT/WASTED/TOTAL (s.node) "
                      << std::get<0>(r)/TIME_UNIT << " "
                      << std::get<1>(r)/TIME_UNIT << " "
                      << std::get<2>(r)/TIME_UNIT << " "
                      << std::get<3>(r)/TIME_UNIT << " "
                      << std::get<4>(r)/TIME_UNIT << " "
                      << "Seed: " << seed << " "
                      << "Convergence: " << converged
                      << std::endl;
        }
        if(blockingfcfs) {
            s.clear();
            system.clear();
            StatTrace t(system.nb_nodes, isr, ier);
            
            SimOrderedIOBlockingFCFS sim(&s, t, seed);
            
            s.reschedule_apps(0);
        
            converged = true;
            while( sim.step() ) {
#if DOUBLE_CHECKS
                std::cerr << "     " << sim.cur_date() << "         \r";
                std::cerr.flush();
#else
                gettimeofday(&now, NULL);
                timersub(&now, &before, &diff);
                if( diff.tv_sec*1e6 + diff.tv_usec > 5e5 ) {
                    std::cerr << "     " << sim.cur_date() << "         \r";
                    std::cerr.flush();
                    before = now;
                }
#endif
                if( sim.cur_date() > 20.0 * min_run ) {
                    converged = false;
                    break;
                }
            }
        
            auto r = t.getStat(segment_size, seed);
            std::cout << "BLOCKING_FCFS Interference: WORK/IO/CKPT/WASTED/TOTAL (s.node) "
                      << std::get<0>(r)/TIME_UNIT << " "
                      << std::get<1>(r)/TIME_UNIT << " "
                      << std::get<2>(r)/TIME_UNIT << " "
                      << std::get<3>(r)/TIME_UNIT << " "
                      << std::get<4>(r)/TIME_UNIT << " "
                      << "Seed: " << seed << " "
                      << "Convergence: " << converged
                      << std::endl;
        }
        if(no) {
            s.clear();
            system.clear();
            StatTrace t(system.nb_nodes, isr, ier);

            SimNoInterference sim(&s, t, seed);
        
            s.reschedule_apps(0);

            converged = true;
            while( sim.step() ) {
#if DOUBLE_CHECKS
                std::cerr << "     " << sim.cur_date() << "         \r";
                std::cerr.flush();
#else
                gettimeofday(&now, NULL);
                timersub(&now, &before, &diff);
                if( diff.tv_sec*1e6 + diff.tv_usec > 5e5 ) {
                    std::cerr << "     " << sim.cur_date() << "         \r";
                    std::cerr.flush();
                    before = now;
                }
#endif
                if( sim.cur_date() > 20.0 * min_run ) {
                    converged = false;
                    break;
                }
            }

            auto r = t.getStat(segment_size, seed);
            std::cout << "No Interference: WORK/IO/CKPT/WASTED/TOTAL (s.node) "
                      << std::get<0>(r)/TIME_UNIT << " "
                      << std::get<1>(r)/TIME_UNIT << " "
                      << std::get<2>(r)/TIME_UNIT << " "
                      << std::get<3>(r)/TIME_UNIT << " "
                      << std::get<4>(r)/TIME_UNIT << " "
                      << "Seed: " << seed << " "
                      << "Convergence: " << converged
                      << std::endl;
        }
        if(simple) {
            s.clear();
            system.clear();
            StatTrace t(system.nb_nodes, isr, ier);

            SimSimpleInterference sim(&s, t, seed);
        
            s.reschedule_apps(0);

            converged = true;
            while( sim.step() ) {
#if DOUBLE_CHECKS
                std::cerr << "     " << sim.cur_date() << "         \r";
                std::cerr.flush();
#else
                gettimeofday(&now, NULL);
                timersub(&now, &before, &diff);
                if( diff.tv_sec*1e6 + diff.tv_usec > 5e5 ) {
                    std::cerr << "     " << sim.cur_date() << "         \r";
                    std::cerr.flush();
                    before = now;
                }
#endif
                if( sim.cur_date() > 20.0 * min_run ) {
                    converged = false;
                    break;
                }
            }

            auto r = t.getStat(segment_size, seed);
            std::cout << "Simple Interference: WORK/IO/CKPT/WASTED/TOTAL (s.node) "
                      << std::get<0>(r)/TIME_UNIT << " "
                      << std::get<1>(r)/TIME_UNIT << " "
                      << std::get<2>(r)/TIME_UNIT << " "
                      << std::get<3>(r)/TIME_UNIT << " "
                      << std::get<4>(r)/TIME_UNIT << " "
                      << "Seed: " << seed << " "
                      << "Convergence: " << converged
                      << std::endl;
        }

        seed += now.tv_sec;
    }
        
    exit(0);
}
