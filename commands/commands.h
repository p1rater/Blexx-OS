/*
 * Copyright (C) 2026 Blex
 * This file is part of BNU (Blex Not Unix).
 * Licensed under the Blex Open Source License (BOSL).
 */

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
