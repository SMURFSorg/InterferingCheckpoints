#include "AppClass.h"


static unsigned int gradient[] = {
    0x00a900, 0x013401,
    0x414d40, 0x515963,
    0xc48647, 0x502f0c,
    0xb674db, 0x35104f
};
static int next_grad = 0;

std::ostream& operator<<(std::ostream& os, const AppClass& ac) {
    return os << "AppClass " << ac.class_id << "\t"
              << "Nodes: " << ac._nb_nodes << "\t"
              << "Input Time: " << (double)ac.input_time / TIME_UNIT << " (s)\t"
              << "Output Time: " << (double)ac.output_time / TIME_UNIT <<  " (s)\t"
              << "Wall Time: " << (double)ac._wall_time / TIME_UNIT <<  " (s)\t"
              << "I/O Time: " << (double)ac.io_time / TIME_UNIT <<  " (s)\t"
              << "Checkpoint Time: " << (double)ac.ckpt_time / TIME_UNIT <<  " (s)\t"
              << "Target Resource: " << ac.target_resource <<  "\t";
}

AppClass::AppClass(System *_sys,
                   int _nb,
                   simt_t _it,
                   simt_t _ot,
                   simt_t _wt,
                   simt_t _iot,
                   simt_t _ct,
                   double _tr) :
    class_id(_sys->next_appclass_id),
    system(_sys),
    _nb_nodes(_nb),
    input_time(_it),
    output_time(_ot),
    _wall_time(_wt),
    io_time(_iot),
    ckpt_time(_ct),
    target_resource(_tr)
{
    r1 = gradient[next_grad] >> 16;
    g1 = (gradient[next_grad] >> 8) & 0xFF;
    b1 = gradient[next_grad] & 0xFF;
    next_grad = (next_grad + 1) % 8;
    r2 = gradient[next_grad] >> 16;
    g2 = (gradient[next_grad] >> 8) & 0xFF;
    b2 = gradient[next_grad] & 0xFF;
    next_grad = (next_grad + 1) % 8;
    _sys->next_appclass_id++;
}
