#include "System.h"

#include "AppClass.h"
#include "App.h"
#include <stdlib.h>
#include <math.h>

std::ostream& operator<<(std::ostream& os, const System& sys) {
    os << sys.name << ":\t"
       << "Nodes: " << sys.nb_nodes << "\t"
       << "Cores per node: " << sys.cores_per_node << "\t"
       << "Bandwidth: " << sys.bandwidth <<  " (Byte/s)\t"
       << "Burst Buffer Bandwidth: " << sys.bb_bandwidth <<  " (Byte/s)\t"
       << "Memory/node: " << sys.mem_per_node << " (Byte)\t"
       << "MTBF_ind: " << sys.mtbf_ind/TIME_UNIT << " (s)\t"
       << "MTBF_sys: " << sys.mtbf_ind/sys.nb_nodes/TIME_UNIT << " (s)\t";
    if( sys.fixed_checkpoint_interval == UNDEFINED_DATE ) {
        return os << "Checkpoint Interval: Daly\t";
    } else {
        return os << "Checkpoint Interval: " << sys.fixed_checkpoint_interval/TIME_UNIT << " (s)\t";
    }
}

System::System(const char *name, int _nodes, int _cores, double _band, double _bb_band, double _mem, simt_t _mtbf_sys, simt_t min_duration) :
    name(name),
    nb_nodes(_nodes),
    cores_per_node(_cores),
    bandwidth(_band),
    bb_bandwidth(_bb_band),
    mem_per_node(_mem),
    classes(),
    mtbf_ind(ceil(_mtbf_sys*nb_nodes*TIME_UNIT)),
    sim(nullptr),
    finalized(false),
    next_appclass_id(0),
    fixed_checkpoint_interval(UNDEFINED_DATE),
    min_duration(min_duration*TIME_UNIT)
        {
            Debug{} << name << ":"
                      << " bandwidth = " << bandwidth/1e12 << " TB/s"
                      << " burst buffer bandwidth (per node) = " << bb_bandwidth/1e9 << " GB/s"
                      << " mem per node = " << mem_per_node / 1e12 << " TB"
                      << " cores per node = " << cores_per_node
                      << " nodes = " << nb_nodes
                      << " MTBF per node = " << mtbf_ind/3600/24/365/TIME_UNIT << " y"
                      << std::endl;
        }

System::~System() {
    clear();
    while(!classes.empty()) {
        AppClass *ac = classes.back();
        delete ac;
        classes.pop_back();
    }
    classes.clear();
}

void System::clear() {
    sim = nullptr;
}

void System::add_app_class(int nb_cores, double input, double output, simt_t wall, double io, double ckpt, double target)
{
    double wall_us = TIME_UNIT * wall;
    int app_size = nb_cores / cores_per_node;
    double input_size = app_size * mem_per_node * input;
    double output_size = app_size * mem_per_node * output;
    double io_size = app_size * mem_per_node * io;
    double ckpt_size = app_size * mem_per_node * ckpt;
    simt_t input_time = ceil(TIME_UNIT * input_size / bandwidth);
    simt_t output_time = ceil(TIME_UNIT * output_size / bandwidth);
    simt_t io_time = ceil(TIME_UNIT * io_size / bandwidth);
    simt_t ckpt_time = ceil(TIME_UNIT * ckpt_size / bandwidth);
    simt_t bb_ckpt_time = ceil(TIME_UNIT * ckpt_size / (double)app_size / bb_bandwidth);
    AppClass *ac = new AppClass(this, app_size, input_time, output_time, wall_us, io_time, ckpt_time, bb_ckpt_time,
                                target);
    classes.push_back(ac);
}

std::pair<int, App*> System::pick_class(std::vector<AppClass *>&goals, unsigned int *seed)
{
    double weight = 0.0;
    double target_resource[goals.size()] = {0.0, };
    for(unsigned int i=0; i < goals.size(); i++) {
        weight += goals[i]->target_resource;
        target_resource[i] = weight;
    }
    double coin = (weight * (double)rand_r(seed)) / (double)RAND_MAX;
    for(unsigned int i = 0; i < goals.size(); i++) {
        if( coin <= target_resource[i] ) {
            App *app = new App(goals[i], seed);
            apps.push_back(app);
            return std::pair<int, App*>(goals[i]->class_id, app);
        }
    }
    assert(0);
    return std::pair<int, App*>(-1, nullptr);
}

void System::set_fixed_checkpoint_interval(simt_t intvl)
{
    if( intvl < 0 ) {
        throw std::runtime_error("Trying to set a negative checkpoint interval");
    }
    fixed_checkpoint_interval = (simt_t)(intvl * TIME_UNIT);
}

void System::set_daly_checkpoint_interval()
{
    fixed_checkpoint_interval = UNDEFINED_DATE;
}

void System::finalize(Simulation *_sim, unsigned int *seed)
{
    sim = _sim;
    if( finalized ) {
        for(auto ait = apps.begin(); ait != apps.end(); ) {
            if( (*ait)->instance_index == 0 ) {
                (*ait)->clear(seed);
                ait++;
            } else {
                delete *ait;
                ait = apps.erase(ait);
            }
        }
    } else {
        std::vector<AppClass*>goals;
        unsigned int aci = 0;
        double current_resource[classes.size()] = {0.0, };
        double resource_sum = 0.0;
        int nb_apps = 0;

        double sum = 0.0;
        for(auto ac: classes) {
            sum += ac->target_resource;
        }
        for(auto ac: classes) {
            ac->target_resource = ac->target_resource/sum;
        }        
        
        do {
            if( resource_sum / nb_nodes > 2 * min_duration ) {
                // Failsafe, if the number of applications is too many, retry
                for(auto a: apps) {
                    delete a;
                }
                apps.clear();
                nb_apps = 0;
                for(aci = 0; aci < classes.size(); aci++)
                    current_resource[aci] = 0.0;
                resource_sum = 0.0;
            }

            goals.clear();
            for(unsigned int i = 0; i < classes.size(); i++) {
                if(resource_sum / nb_nodes < min_duration || current_resource[i]/resource_sum < classes[i]->target_resource)
                    goals.push_back(classes[i]);
            }
            if(goals.empty()) {
                for(unsigned int i = 0; i < classes.size(); i++) {
                    if(current_resource[i]/resource_sum < classes[i]->target_resource + 0.01)
                        goals.push_back(classes[i]);
                }
            }
            
            std::pair<int, App*>picked = pick_class(goals, seed);
            double resource =  (double)picked.second->nb_nodes * (double)picked.second->wall_time;
            current_resource[picked.first] += resource;
            resource_sum += resource;
            nb_apps++;
            
            aci = 0;
            for(auto acit = classes.begin(); acit != classes.end(); acit++) {
                if( current_resource[aci]/resource_sum < (*acit)->target_resource - 0.01 ||
                    current_resource[aci]/resource_sum > (*acit)->target_resource + 0.01) {
                    break;
                }
                aci++;
            }
        } while( resource_sum / nb_nodes < min_duration || aci < classes.size() );

#if defined(DEBUG) || 1
        aci = 0;
        std::cout << nb_apps << " Apps : ";
        for(auto acit = classes.begin(); acit != classes.end(); acit++) {
            std::cout << current_resource[aci]/resource_sum << "/" << (*acit)->target_resource;
            if( current_resource[aci]/resource_sum < (*acit)->target_resource - 0.01 )
                std::cout << "< ";
            else if( current_resource[aci]/resource_sum > (*acit)->target_resource + 0.01 )
                std::cout << "> ";
            else
                std::cout << "= ";
            aci++;
        }
        std::cout << "(" << resource_sum/nb_nodes/TIME_UNIT/3600 << "h)" << std::endl;
#endif
        
        finalized = true;
    }
}
