#ifndef AppClass_h
#define AppClass_h

#include "Simulation.h"

class System;

class AppClass {
public:
    int              class_id;
    System          *system;
    int              _nb_nodes;
    simt_t           input_time;
    simt_t           output_time;
    simt_t           _wall_time;
    simt_t           io_time;
    simt_t           ckpt_time;
    double           target_resource;
    png_byte         r1, g1, b1;
    png_byte         r2, g2, b2;

    AppClass(System *_sys,
             int _nb,
             simt_t _it,
             simt_t _ot,
             simt_t _wt,
             simt_t _iot,
             simt_t _ct,
             double _tr);

    friend std::ostream& operator<< (std::ostream& stream, const AppClass& ac);
};

#endif
