#ifndef DELIVERY_H
#define DELIVERY_H

#include "options.h"

int deliver(const char* container_root, const char* container_prog, char* container_argv[], struct options** opts);

#endif