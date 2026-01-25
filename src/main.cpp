#include "uring.h"


int main(){

    _io_uring io_uring_instance;
    io_uring_instance.event_Loop();

    return 0;
}