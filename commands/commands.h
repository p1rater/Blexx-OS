#ifndef COMMANDS_H
#define COMMANDS_H

#include "../kernel_utils.h"

typedef void (*command_func_t)(const char* args);

typedef struct {
    const char* name;
    command_func_t func;
    const char* help;
} command_t;

extern command_t command_table[];
extern int command_count;

#endif
