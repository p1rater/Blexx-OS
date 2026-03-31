/*
 * Copyright (C) 2026 Blex
 * This file is part of BNU (Blex Not Unix).
 * Licensed under the Blex Open Source License (BOSL).
 */

#include "kernel_utils.h"
#include "commands/commands.h"

/* --- 1. DIŞ REFERANSLAR --- */
extern int str_match(const char* s1, const char* s2);
extern void str_copy(char* dest, const char* src);
extern unsigned char* current_layout; 
extern unsigned char kbd_tr[128];
extern char history[10][64];
extern int history_count;
extern int history_index;
extern command_t command_table[];
extern int command_count;

/* --- 2. GLOBAL SİSTEM DURUMU --- */
vfs_node_t ram_disk[32];
char current_user[16] = "user";
char system_hostname[16] = "Blex"; 
unsigned int boot_ticks = 0;   
int cursor_x = 0; 
int cursor_y = 0;
char current_color = 0x07; 
int caps_lock = 0;

char shell_buffer[64];
int buf_idx = 0;   

#define VIDEO_BUF ((char*)0xB8000)
#define MAX_COLS 80
#define MAX_ROWS 25

/* --- 3. KLAVYE VE PORT I/O --- */
unsigned char kbd_us[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

unsigned char inb(unsigned short port) {
    unsigned char ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void outb(unsigned short port, unsigned char val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

void outw(unsigned short port, unsigned short val) {
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

/* --- 4. EKRAN YÖNETİMİ --- */
void update_cursor(int x, int y) {
    unsigned short pos = y * MAX_COLS + x;
    outb(0x3D4, 0x0F); outb(0x3D5, (unsigned char)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (unsigned char)((pos >> 8) & 0xFF));
}

void clear_screen() {
    for (int i = 0; i < MAX_COLS * MAX_ROWS * 2; i += 2) {
        VIDEO_BUF[i] = ' '; VIDEO_BUF[i+1] = 0x07;
    }
    cursor_x = 0; cursor_y = 0; update_cursor(0, 0);
}

void putchar(char c) {
    if (c == '\n') { cursor_x = 0; cursor_y++; }
    else if (c == '\b') {
        if (cursor_x > 0) { 
            cursor_x--;
            int offset = (cursor_y * MAX_COLS + cursor_x) * 2;
            VIDEO_BUF[offset] = ' '; VIDEO_BUF[offset + 1] = current_color;
        }
    } else {
        int offset = (cursor_y * MAX_COLS + cursor_x) * 2;
        VIDEO_BUF[offset] = c; VIDEO_BUF[offset + 1] = current_color;
        cursor_x++;
    }
    if (cursor_y >= MAX_ROWS) {
        for (int i = 0; i < (MAX_ROWS - 1) * MAX_COLS * 2; i++)
            VIDEO_BUF[i] = VIDEO_BUF[i + MAX_COLS * 2];
        for (int i = (MAX_ROWS - 1) * MAX_COLS * 2; i < MAX_ROWS * MAX_COLS * 2; i += 2) {
            VIDEO_BUF[i] = ' '; VIDEO_BUF[i+1] = current_color;
        }
        cursor_y = MAX_ROWS - 1;
    }
    update_cursor(cursor_x, cursor_y);
}

void print_str(const char* str) { for (int i = 0; str[i] != '\0'; i++) putchar(str[i]); }

void print_int(int n) {
    if (n == 0) { putchar('0'); return; }
    if (n < 0) { putchar('-'); n = -n; }
    char buf[12]; int i = 10; buf[11] = '\0';
    while (n > 0 && i >= 0) { buf[i--] = (n % 10) + '0'; n /= 10; }
    print_str(&buf[i+1]);
}

/* --- 5. MODERN SHELL FONKSİYONLARI --- */
void print_prompt() {
    current_color = 0x0A; // Yeşil
    print_str(current_user);
    current_color = 0x07;
    print_str("@");
    current_color = 0x0B; // Turkuaz
    print_str(system_hostname);
    current_color = 0x07;
    print_str(":~# ");
}

void process_command() {
    shell_buffer[buf_idx] = '\0';
    putchar('\n');

    if (buf_idx > 0) {
        if (history_count < 10) str_copy(history[history_count++], shell_buffer);
        else { 
            for(int i=0; i<9; i++) str_copy(history[i], history[i+1]); 
            str_copy(history[9], shell_buffer); 
        }
        history_index = history_count;

        char cmd_name[32]; int i = 0;
        while (shell_buffer[i] != ' ' && shell_buffer[i] != '\0' && i < 31) { cmd_name[i] = shell_buffer[i]; i++; }
        cmd_name[i] = '\0';
        char* args = (shell_buffer[i] == ' ') ? &shell_buffer[i+1] : "";

        int found = 0;
        for (int k = 0; k < command_count; k++) {
            if (str_match(cmd_name, command_table[k].name)) { 
                command_table[k].func(args); 
                found = 1; 
                break; 
            }
        }
        if (!found) { 
            current_color = 0x0C; // Kırmızı
            print_str("BNU-SH: command not found: "); 
            print_str(cmd_name);
            current_color = 0x07;
        }
        if (cursor_x != 0) putchar('\n');
    }
    
    print_prompt(); 
    buf_idx = 0;
}

void kernel_main() {
    clear_screen();
    current_layout = kbd_us; 
    current_color = 0x0E; // Sarı
    print_str("BNU/Blex Project - Protected by BOSL\n");
    current_color = 0x07;
    print_prompt();

    while (1) {
        boot_ticks++;
        if (inb(0x64) & 1) {
            unsigned char sc = inb(0x60);
            if (sc & 0x80) continue; 

            if (sc == 0x3A) { caps_lock = !caps_lock; continue; }

            if (sc == 0x48) { // UP
                if (history_count > 0 && history_index > 0) {
                    history_index--; 
                    while(buf_idx > 0) { putchar('\b'); buf_idx--; }
                    str_copy(shell_buffer, history[history_index]);
                    for(int j=0; shell_buffer[j]; j++) { putchar(shell_buffer[j]); buf_idx++; }
                }
            } else if (sc == 0x50) { // DOWN
                if (history_index < history_count - 1) {
                    history_index++; 
                    while(buf_idx > 0) { putchar('\b'); buf_idx--; }
                    str_copy(shell_buffer, history[history_index]);
                    for(int j=0; shell_buffer[j]; j++) { putchar(shell_buffer[j]); buf_idx++; }
                }
            } else {
                char c = current_layout[sc];
                if (caps_lock && c >= 'a' && c <= 'z') c -= 32;

                if (c == '\n') process_command();
                else if (c == '\b') { 
                    if (buf_idx > 0) { buf_idx--; putchar('\b'); } 
                }
                else if (c && buf_idx < 63) { 
                    shell_buffer[buf_idx++] = c; 
                    putchar(c); 
                }
            }
        }
        for(volatile int i=0; i<1000; i++); 
    }
}