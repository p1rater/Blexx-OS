#include "commands.h"

extern int cursor_x, cursor_y;
extern void update_cursor(int x, int y);
extern vfs_node_t ram_disk[3];
extern char current_color;
extern unsigned char inb(unsigned short port);

/* --- RAM EDITOR BUFFER --- */
char editor_buffer[2000]; 
int edit_idx = 0;

/* --- YARDIMCI FONKSİYONLAR --- */
int str_match(const char* s1, const char* s2) {
    int i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        if (s1[i] != s2[i]) return 0;
        i++;
    }
    return (s1[i] == s2[i]);
}

static inline unsigned long long rdtsc(void) {
    unsigned int lo, hi;
    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((unsigned long long)hi << 32) | lo;
}

/* --- 1. SİSTEM İZLEME (STATUS) --- */
void command_status(const char* args) {
    unsigned long long cycles = rdtsc();
    unsigned int lo = (unsigned int)cycles;
    unsigned int hi = (unsigned int)(cycles >> 32);
    unsigned int total_seconds = (hi * 2) + (lo / 1500000000);
    
    unsigned int hours = total_seconds / 3600;
    unsigned int minutes = (total_seconds % 3600) / 60;
    unsigned int seconds = total_seconds % 60;

    print_str("\n--- BLEX MONITOR ---");
    print_str("\nUPTIME: ");
    print_int(hours); print_str("h ");
    print_int(minutes); print_str("m ");
    print_int(seconds); print_str("s");
    print_str("\nRAM: 144KB / 4096KB (Static)");
    print_str("\nCPU: BLEX x86_32 Core\n");
}

/* --- 2. BLEX-EDIT v3.0 (ARROWS & RAM STORAGE) --- */
void command_edit(const char* args) {
    clear_screen();
    print_str("--- BLEX-EDIT v3.0 --- (ARROWS to Move, ESC to Save)\n");
    print_str("------------------------------------------------------\n");
    
    extern unsigned char kbd_us[128];
    int editing = 1;
    edit_idx = 0;
    for(int i=0; i<2000; i++) editor_buffer[i] = 0;

    while(editing) {
        if (inb(0x64) & 1) {
            unsigned char sc = inb(0x60);
            if (!(sc & 0x80)) {
                if (sc == 0x01) editing = 0; // ESC: Çıkış
                else if (sc == 0x48) { if(cursor_y > 3) cursor_y--; } // YUKARI
                else if (sc == 0x50) { if(cursor_y < 24) cursor_y++; } // AŞAĞI
                else if (sc == 0x4B) { if(cursor_x > 0) cursor_x--; } // SOL
                else if (sc == 0x4D) { if(cursor_x < 79) cursor_x++; } // SAĞ
                else if (sc == 0x1C) { // ENTER
                    cursor_x = 0; cursor_y++; 
                    if(edit_idx < 1999) editor_buffer[edit_idx++] = '\n';
                } 
                else if (sc == 0x0E) { // BACKSPACE
                    if (edit_idx > 0) {
                        edit_idx--;
                        editor_buffer[edit_idx] = 0;
                        putchar('\b');
                    }
                }
                else {
                    char c = kbd_us[sc];
                    if (c && edit_idx < 1999) {
                        editor_buffer[edit_idx++] = c;
                        putchar(c);
                    }
                }
                update_cursor(cursor_x, cursor_y); // İmleci her harekette tazele
            }
        }
    }

    /* RAM DISK'E YAZMA */
    for(int i=0; i<127 && i < edit_idx; i++) {
        ram_disk[0].content[i] = editor_buffer[i];
    }
    ram_disk[0].content[127] = '\0';
    ram_disk[0].size = edit_idx;

    clear_screen();
    print_str("\n[BLEX] Saved to readme.txt. Returning to shell...\n>> ");
}

/* --- 3. GÖRSEL VE BİLGİ (NEOFETCH) --- */
void command_neofetch(const char* args) {
    print_str("\n  ____  _     _______  __ ");
    print_str("\n | __ )| |   | ____\\ \\/ / ");
    print_str("\n |  _ \\| |   |  _|  \\  /  ");
    print_str("\n | |_) | |___| |___ /  \\  ");
    print_str("\n |____/|_____|_____/_/\\_\\ ");
    print_str("\n ------------------------ ");
    print_str("\n OS: BlexxOS v0.0.3");
    print_str("\n KERNEL: BLEX-Core\n USER: p1rater\n");
}

/* --- 4. DOSYA SİSTEMİ (LS / CAT) --- */
void command_ls(const char* args) {
    print_str("\nFiles in RAMDisk:");
    for(int i=0; i<3; i++) { print_str("\n - "); print_str(ram_disk[i].name); }
    print_str("\n");
}

void command_cat(const char* args) {
    if(!args || args[0] == '\0') { print_str("\nUsage: cat [filename]"); return; }
    for(int i=0; i<3; i++) {
        if(str_match(args, ram_disk[i].name)) {
            print_str("\n"); print_str(ram_disk[i].content); return;
        }
    }
    print_str("\nFile not found.");
}

/* --- 5. DİĞER KOMUTLAR --- */
void command_help(const char* args) {
    print_str("\nCommands: help, status, edit, neofetch, ls, cat, uname, whoami, ver, echo, color, clear, reboot, shutdown");
}

void command_whoami(const char* args) { print_str("\nUser: p1rater (Root)"); }
void command_ver(const char* args) { print_str("\nBlexxOS v0.0.3 [Kernel: BLEX]"); }
void command_echo(const char* args) { if(args[0]) { print_str("\n"); print_str(args); } }
void command_uname(const char* args) { print_str("\nBLEX 0.0.3-i386"); }
void command_clear(const char* args) { clear_screen(); }
void command_color(const char* args) { 
    current_color++; if(current_color > 0x0F) current_color = 0x01; 
    print_str("\nColor updated."); 
}

void command_reboot(const char* args) { asm volatile ("outb %0, %1" : : "a"((unsigned char)0xFE), "Nd"(0x64)); }
void command_shutdown(const char* args) { asm volatile ("outw %0, %1" : : "a"((unsigned short)0x2000), "Nd"((unsigned short)0x604)); }

/* --- 6. KOMUT TABLOSU --- */
command_t command_table[] = {
    {"help", command_help, ""}, {"status", command_status, ""}, {"edit", command_edit, ""},
    {"neofetch", command_neofetch, ""}, {"fetch", command_neofetch, ""},
    {"ls", command_ls, ""}, {"cat", command_cat, ""}, {"uname", command_uname, ""},
    {"whoami", command_whoami, ""}, {"ver", command_ver, ""}, {"echo", command_echo, ""},
    {"color", command_color, ""}, {"clear", command_clear, ""}, {"cls", command_clear, ""},
    {"reboot", command_reboot, ""}, {"shutdown", command_shutdown, ""}
};

int command_count = sizeof(command_table) / sizeof(command_t);
