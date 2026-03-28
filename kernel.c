#include "kernel_utils.h"
#include "commands/commands.h"

#define VIDEO_BUF ((char*)0xB8000)
#define MAX_COLS 80
#define MAX_ROWS 25

extern int str_match(const char* s1, const char* s2);

/* --- GLOBAL DEGISKENLER --- */
int cursor_x = 0; int cursor_y = 0;
char current_color = 0x0F;
char shell_buffer[80];
char cmd_name[80];
char args[80];
int buf_idx = 0;

/* --- VFS (RAM DISK) --- */
vfs_node_t ram_disk[3] = {
    {"readme.txt", "BlexxOS v0.0.3 - Stable Terminal Edition.", 40},
    {"version.txt", "0.0.3", 5},
    {"author.txt", "p1rater", 7}
};

/* --- KOMUT GECMISI (HISTORY) --- */
char history[5][80];  
int history_count = 0;
int history_index = -1;

/* --- DONANIM IMLECI (CURSOR) KONTROLU --- */
void update_cursor(int x, int y) {
    unsigned short pos = y * MAX_COLS + x;
    // CRT Controller Index Port (0x3D4) ve Data Port (0x3D5)
    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0F), "Nd"((unsigned short)0x3D4));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)(pos & 0xFF)), "Nd"((unsigned short)0x3D5));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)0x0E), "Nd"((unsigned short)0x3D4));
    asm volatile ("outb %0, %1" : : "a"((unsigned char)((pos >> 8) & 0xFF)), "Nd"((unsigned short)0x3D5));
}

/* --- EKRAN KAYDIRMA (SCROLL) --- */
void scroll() {
    if (cursor_y >= MAX_ROWS) {
        for (int i = 0; i < (MAX_ROWS - 1) * MAX_COLS; i++) {
            VIDEO_BUF[i * 2] = VIDEO_BUF[(i + MAX_COLS) * 2];
            VIDEO_BUF[i * 2 + 1] = VIDEO_BUF[(i + MAX_COLS) * 2 + 1];
        }
        for (int i = (MAX_ROWS - 1) * MAX_COLS; i < MAX_ROWS * MAX_COLS; i++) {
            VIDEO_BUF[i * 2] = ' ';
            VIDEO_BUF[i * 2 + 1] = 0x07;
        }
        cursor_y = MAX_ROWS - 1;
    }
    update_cursor(cursor_x, cursor_y);
}

/* --- PORT GIRIS --- */
unsigned char inb(unsigned short port) {
    unsigned char ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* --- EKRAN TEMIZLEME --- */
void clear_screen() {
    for(int i = 0; i < 80 * 25 * 2; i += 2) { 
        VIDEO_BUF[i] = ' '; 
        VIDEO_BUF[i+1] = 0x07; 
    }
    cursor_x = 0; 
    cursor_y = 0;
    update_cursor(0, 0);
}

/* --- KARAKTER BASMA --- */
void putchar(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            VIDEO_BUF[(cursor_y * 80 + cursor_x) * 2] = ' ';
        }
    } else {
        int offset = (cursor_y * 80 + cursor_x) * 2;
        VIDEO_BUF[offset] = c;
        VIDEO_BUF[offset + 1] = current_color;
        cursor_x++;
    }
    
    if (cursor_x >= MAX_COLS) { 
        cursor_x = 0; 
        cursor_y++; 
    }
    scroll();
}

void print_str(const char* str) { 
    for(int i = 0; str[i] != '\0'; i++) putchar(str[i]); 
}

void print_int(int n) {
    if (n == 0) { putchar('0'); return; }
    if (n < 0) { putchar('-'); n = -n; }
    char buf[12];
    int i = 0;
    while (n > 0) { buf[i++] = (n % 10) + '0'; n /= 10; }
    while (--i >= 0) putchar(buf[i]);
}

/* --- HISTORY YUKLEME --- */
void load_history(int idx) {
    while(buf_idx > 0) { buf_idx--; putchar('\b'); }
    int i = 0;
    while(history[idx][i] != '\0') {
        shell_buffer[buf_idx++] = history[idx][i];
        putchar(history[idx][i]);
        i++;
    }
}

/* --- KOMUT ISLEME --- */
void process_command() {
    shell_buffer[buf_idx] = '\0';
    if (buf_idx == 0) { print_str("\n>> "); return; }

    // History'e ekle
    for(int i=4; i>0; i--) { 
        for(int j=0; j<80; j++) history[i][j] = history[i-1][j]; 
    }
    for(int j=0; j<80; j++) history[0][j] = shell_buffer[j];
    if(history_count < 5) history_count++;
    history_index = -1;

    // Argüman ayrıştırma
    for(int k=0; k<80; k++) { cmd_name[k] = 0; args[k] = 0; }
    int i = 0, j = 0;
    while (shell_buffer[i] != ' ' && shell_buffer[i] != '\0') { 
        cmd_name[i] = shell_buffer[i]; i++; 
    }
    cmd_name[i] = '\0';
    if (shell_buffer[i] == ' ') { 
        i++; 
        while (shell_buffer[i] != '\0') args[j++] = shell_buffer[i++]; 
    }
    args[j] = '\0';

    // Komut tablosunda ara
    for (int k = 0; k < command_count; k++) {
        if (str_match(cmd_name, command_table[k].name)) {
            command_table[k].func(args);
            print_str("\n>> ");
            buf_idx = 0;
            return;
        }
    }
    print_str("\nUnknown command\n>> ");
    buf_idx = 0;
}

/* --- KLAVYE HARITASI --- */
unsigned char kbd_us[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',   0, '*',
    0, ' ', 0
};

/* --- ANA GIRIS NOKTASI --- */
void kernel_main() {
    clear_screen();
    print_str("BlexxOS v0.0.3 - BLEX Kernel Active.\n");
    print_str("Cursor & History support enabled.\n>> ");
    
    while(1) {
        if (inb(0x64) & 1) { // Klavye verisi var mı?
            unsigned char sc = inb(0x60);
            if (!(sc & 0x80)) { // Tuşa basıldı mı?
                if (sc == 0x48 && history_index < history_count - 1) { 
                    history_index++; 
                    load_history(history_index); 
                }
                else if (sc == 0x50 && history_index > 0) { 
                    history_index--; 
                    load_history(history_index); 
                }
                else {
                    char c = kbd_us[sc];
                    if (c == '\n') process_command();
                    else if (c == '\b' && buf_idx > 0) { 
                        buf_idx--; 
                        putchar('\b'); 
                    }
                    else if (c && c != '\b' && buf_idx < 79) { 
                        shell_buffer[buf_idx++] = c; 
                        putchar(c); 
                    }
                }
            }
        }
    }
}
