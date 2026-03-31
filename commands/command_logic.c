/*
 * Copyright (C) 2026 Blex
 * This file is part of BNU (Blex Not Unix).
 * Licensed under the BOSL.
 */

#include "commands.h"

/* --- 1. KERNEL REFERANSLARI --- */
extern vfs_node_t ram_disk[32];
extern char current_user[16];
extern unsigned int boot_ticks;
extern char current_color;
extern void print_int(int n);
extern void print_str(const char* str);
extern void putchar(char c);
extern void clear_screen();
extern unsigned char inb(unsigned short port);
extern void outb(unsigned short port, unsigned char val);
extern void outw(unsigned short port, unsigned short val);

/* --- SİSTEM DEĞİŞKENLERİ --- */
extern char system_hostname[16];
char cwd_path[64] = "/";
char history[10][64];
int history_count = 0;
int history_index = -1;
unsigned char* current_layout; 

/* --- 2. YARDIMCI FONKSİYONLAR --- */
int str_match(const char* s1, const char* s2) {
    int i = 0;
    while (s1[i] && s2[i]) { if (s1[i] != s2[i]) return 0; i++; }
    return (s1[i] == '\0' && s2[i] == '\0');
}

void str_copy(char* dest, const char* src) {
    int i = 0; while (src[i]) { dest[i] = src[i]; i++; } dest[i] = '\0';
}

unsigned int get_total_ram() {
    unsigned short low, high;
    outb(0x70, 0x30); low = inb(0x71);
    outb(0x70, 0x31); high = inb(0x71);
    return ((unsigned int)((high << 8) | low) / 1024) + 1;
}

/* --- 3. KOMUT FONKSİYONLARI --- */

void cmd_ls(const char* a) {
    print_str("\n");
    for(int i=0; i<32; i++) if(ram_disk[i].name[0] != '\0') {
        if(ram_disk[i].is_directory) print_str("/");
        print_str(ram_disk[i].name); print_str("  ");
    }
    print_str("\n");
}

void cmd_cat(const char* a) {
    for(int i=0; i<32; i++) if(str_match(a, ram_disk[i].name)) {
        print_str("\n"); print_str(ram_disk[i].content); print_str("\n"); return;
    }
}

void cmd_touch(const char* a) {
    if(!a[0]) return;
    for(int i=0; i<32; i++) if(ram_disk[i].name[0] == '\0') {
        str_copy(ram_disk[i].name, a); ram_disk[i].is_directory = 0;
        ram_disk[i].content[0] = '\0'; return;
    }
}

void cmd_mkdir(const char* a) {
    if(!a[0]) return;
    for(int i=0; i<32; i++) if(ram_disk[i].name[0] == '\0') {
        str_copy(ram_disk[i].name, a); ram_disk[i].is_directory = 1; return;
    }
}

void cmd_rm(const char* a) {
    for(int i=0; i<32; i++) if(str_match(a, ram_disk[i].name)) {
        ram_disk[i].name[0] = '\0'; return;
    }
}

void cmd_pwd(const char* a) { print_str("\n"); print_str(cwd_path); print_str("\n"); }

void cmd_cd(const char* a) { str_copy(cwd_path, a[0] ? a : "/"); }

void cmd_free(const char* a) {
    unsigned int total = get_total_ram();
    print_str("\nRAM: "); print_int(total); print_str("MB\n");
}

void cmd_uptime(const char* a) { print_str("\nUptime: "); print_int(boot_ticks/100); print_str("s\n"); }

void cmd_uname(const char* a) { print_str("\nBNU/Blex\n"); }

void cmd_date(const char* a) {
    outb(0x70, 0x04); unsigned char h = inb(0x71);
    outb(0x70, 0x02); unsigned char m = inb(0x71);
    print_str("\n"); print_int(h); putchar(':'); print_int(m); print_str("\n");
}

void cmd_whoami(const char* a) { print_str("\n"); print_str(current_user); print_str("\n"); }

void cmd_hostname(const char* a) {
    if(a[0]) str_copy(system_hostname, a);
    else { print_str("\n"); print_str(system_hostname); print_str("\n"); }
}

void cmd_clear(const char* a) { clear_screen(); }

void cmd_reboot(const char* a) { outb(0x64, 0xFE); }

void cmd_poweroff(const char* a) { outw(0x604, 0x2000); asm volatile("cli; hlt"); }

void cmd_history(const char* a) {
    print_str("\n");
    for(int i=0; i<history_count; i++) {
        print_int(i+1); print_str(". "); print_str(history[i]); print_str("\n");
    }
}

void cmd_echo(const char* a) { print_str("\n"); print_str(a); print_str("\n"); }

void cmd_exit(const char* a) { clear_screen(); asm volatile("cli; hlt"); }

void cmd_help(const char* a) {
    print_str("\nls      cat     touch   mkdir   rm\n");
    print_str("pwd     cd      free    uptime  uname\n");
    print_str("date    whoami  host    clear   reboot\n");
    print_str("off     hist    echo    exit    ver\n");
    print_str("cls     clr     sync    man     help\n");
}

void cmd_ver(const char* a) { print_str("\nBNU/Blex\n"); }
void cmd_cls(const char* a) { clear_screen(); }
void cmd_clr(const char* a) { clear_screen(); }
void cmd_sync(const char* a) { print_str("\nOK\n"); }
void cmd_man(const char* a) { cmd_help(a); }

/* --- 4. KOMUT TABLOSU --- */
command_t command_table[] = {
    {"ls", cmd_ls}, {"cat", cmd_cat}, {"touch", cmd_touch}, {"mkdir", cmd_mkdir},
    {"rm", cmd_rm}, {"pwd", cmd_pwd}, {"cd", cmd_cd}, {"free", cmd_free},
    {"uptime", cmd_uptime}, {"uname", cmd_uname}, {"date", cmd_date},
    {"whoami", cmd_whoami}, {"hostname", cmd_hostname}, {"clear", cmd_clear},
    {"reboot", cmd_reboot}, {"poweroff", cmd_poweroff}, {"history", cmd_history},
    {"echo", cmd_echo}, {"exit", cmd_exit}, {"help", cmd_help}, {"ver", cmd_ver},
    {"cls", cmd_cls}, {"clr", cmd_clr}, {"sync", cmd_sync}, {"man", cmd_man}
};

int command_count = 25;