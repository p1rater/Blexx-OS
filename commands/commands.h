/*
 * commands.h – BlexOS x86_64
 * Copyright (C) 2026 Blex – BOSL License
 */

#ifndef COMMANDS_H
#define COMMANDS_H

#include "../kernel_utils.h"

typedef void (*command_func_t)(const char* args);

typedef struct {
    const char*    name;
    command_func_t func;
    const char*    help;
} command_t;

extern command_t command_table[];
extern int       command_count;

/* Forward declarations for all commands */
void cmd_ls(const char*);
void cmd_cd(const char*);
void cmd_pwd(const char*);
void cmd_mkdir(const char*);
void cmd_rmdir(const char*);
void cmd_touch(const char*);
void cmd_rm(const char*);
void cmd_cp(const char*);
void cmd_mv(const char*);
void cmd_cat(const char*);
void cmd_write(const char*);
void cmd_stat(const char*);
void cmd_edit(const char*);
void cmd_lsdev(const char*);
void cmd_lspci(const char*);
void cmd_lsusb(const char*);
void cmd_lssata(const char*);
void cmd_cpuinfo(const char*);
void cmd_meminfo(const char*);
void cmd_partinfo(const char*);
void cmd_help(const char*);
void cmd_man(const char*);
void cmd_date(const char*);
void cmd_uptime(const char*);
void cmd_uname(const char*);
void cmd_free(const char*);
void cmd_fetch(const char*);
void cmd_reboot(const char*);
void cmd_shutdown(const char*);
void cmd_clear(const char*);
void cmd_history(const char*);
void cmd_echo(const char*);
void cmd_exit(const char*);
void cmd_ver(const char*);
void cmd_sync(const char*);
void cmd_brun(const char*);
void cmd_beep(const char*);
void cmd_morse(const char*);

/* Utility used by kernel.c */
void read_line(char* buf, int limit);

#endif /* COMMANDS_H */
