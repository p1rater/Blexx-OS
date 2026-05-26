/*
 * kernel.c – BlexOS x86_64 kernel  (framebuffer TTY edition, 1920×1080)
 * Copyright (C) 2026 Blex – BOSL License
 *
 * Changes vs. WM version:
 *   - Window manager removed; framebuffer used as a single full-screen TTY
 *   - Target resolution: 1920×1080 (requested via GRUB gfxpayload)
 *   - Font and pixel code unchanged; fonts are embedded in fb.c
 */

#include "kernel_utils.h"
#include "commands/commands.h"
#include "fb.h"
#include "fonts/ttf_render.h"   // <-- YENİ: dinamik font yükleme için

/* ── Externals from command_logic.c ────────────────────── */
extern int   str_match(const char* s1, const char* s2);
extern void  str_copy(char* dest, const char* src);
extern unsigned char* current_layout;
extern char   history[10][64];
extern int    history_count;
extern int    history_index;
extern command_t command_table[];
extern int    command_count;

/* ── Global Kernel State ────────────────────────────────── */
char          current_user[16]    = "user";
char          system_hostname[16] = "Blex";
unsigned int  boot_ticks          = 0;
int           caps_lock           = 0;

char shell_buffer[64];
int  buf_idx = 0;

int  cursor_x = 0;
int  cursor_y = 0;
char current_color = 0x07;

/* ── Initramfs module location ──────────────────────────── */
uint32_t initramfs_mod_start = 0;
uint32_t initramfs_mod_end   = 0;

/* ── Compat shims ───────────────────────────────────────── */
void update_cursor(int x, int y) { (void)x; (void)y; }

void clear_screen(void) { tty_clear(); }

void putchar(char c) { tty_putchar(c); }

void print_str(const char* s) { tty_print(s); }

void print_str_color(const char* s, char vga_attr) {
    uint32_t col;
    switch (vga_attr & 0x0F) {
        case 0xA: col = COL_GREEN;   break;
        case 0xB: col = COL_CYAN;    break;
        case 0xC: col = COL_RED;     break;
        case 0xE: col = COL_YELLOW;  break;
        case 0xF: col = COL_WHITE;   break;
        default:  col = COL_TEXT_FG; break;
    }
    tty_print_color(s, col);
}

void print_int(int n) { tty_print_int(n); }

/* ── Keyboard layout ────────────────────────────────────── */
unsigned char kbd_us[128] = {
    0, 27,'1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\',
    'z','x','c','v','b','n','m',',','.','/', 0,'*', 0,' '
};

/* ── Boot log helpers ───────────────────────────────────── */
static void boot_delay(void) {
    for (volatile int i = 0; i < 5000000; i++);
}

void bootlog_ok(const char* msg) {
    print_str_color("[  OK  ] ", (char)0xA);
    print_str(msg); putchar('\n');
}
void bootlog_info(const char* msg) {
    print_str_color("[ BOOT ] ", (char)0xB);
    print_str(msg); putchar('\n');
}
void bootlog_warn(const char* msg) {
    print_str_color("[ WARN ] ", (char)0xE);
    print_str(msg); putchar('\n');
}

/* ── BRUN loader ────────────────────────────────────────── */
typedef void (*brun_entry_t)(void);

void brun_exec(const char* filename) {
    es1_node_t *node = es1_find(filename);
    if (!node) {
        print_str_color("brun: file not found: ", (char)0xC);
        print_str(filename); putchar('\n'); return;
    }
    brun_header_t* hdr = (brun_header_t*)node->inline_data;
    if (hdr->magic != BRUN_MAGIC) {
        print_str_color("brun: bad magic\n", (char)0xC); return;
    }
    void* code_entry = (void*)((unsigned char*)node->inline_data + sizeof(brun_header_t));
    print_str("brun: launching "); print_str(hdr->name); print_str("...\n");
    brun_entry_t fn = (brun_entry_t)code_entry;
    fn();
    print_str_color("\nbrun: done.\n", (char)0xA);
}

/* ── Multiboot2 module detection ──────────────────────────*/
#define MB2_MAGIC_EXPECTED 0x36D76289u

typedef struct { uint32_t type; uint32_t size; } mb2_tag_t;
typedef struct {
    uint32_t type;
    uint32_t size;
    uint32_t mod_start;
    uint32_t mod_end;
    char     cmdline[];
} mb2_module_tag_t;

static void detect_initramfs(uint32_t mb2_magic, void* mb2_info) {
    if (mb2_magic != MB2_MAGIC_EXPECTED) {
        bootlog_warn("Multiboot2 magic mismatch – no module scan");
        return;
    }
    uint8_t *p     = (uint8_t*)mb2_info + 8;
    uint32_t total = *(uint32_t*)mb2_info;
    uint8_t *end   = (uint8_t*)mb2_info + total;

    int found = 0;
    while (p < end) {
        mb2_tag_t *tag = (mb2_tag_t*)p;
        if (tag->type == 0) break;
        if (tag->type == 3) {
            mb2_module_tag_t *mt = (mb2_module_tag_t*)p;
            initramfs_mod_start = mt->mod_start;
            initramfs_mod_end   = mt->mod_end;
            bootlog_ok("initramfs module mapped");
            print_str_color("         CPIO at 0x", (char)0xB);
            const char *hx = "0123456789ABCDEF";
            char hbuf[11]; hbuf[0]='0'; hbuf[1]='x';
            for (int i=0; i<8; i++)
                hbuf[2+i] = hx[(mt->mod_start >> (28-4*i)) & 0xF];
            hbuf[10]='\0'; print_str(hbuf);
            print_str(" size=");
            uint32_t sz = mt->mod_end - mt->mod_start;
            char sbuf[12]; int si=0;
            if (sz==0) { sbuf[si++]='0'; }
            else { uint32_t tmp=sz; while(tmp){ sbuf[si++]=(char)('0'+tmp%10); tmp/=10; } }
            for (int a=0,b=si-1; a<b; a++,b--) { char t=sbuf[a]; sbuf[a]=sbuf[b]; sbuf[b]=t; }
            sbuf[si]='\0'; print_str(sbuf);
            print_str(" B\n");
            found++;
        }
        uint32_t aligned = (tag->size + 7) & ~7u;
        p += aligned;
    }
    if (!found) bootlog_warn("No initramfs module found (boot without module2?)");
}

/* ── CPIO ve Font Config Parse Mantığı (YENİ) ──────────── */
/* Standart CPIO Newc Header Yapısı */
struct cpio_header {
    char magic[6];
    char ino[8];
    char mode[8];
    char uid[8];
    char gid[8];
    char nlink[8];
    char mtime[8];
    char filesize[8];
    char devmajor[8];
    char devminor[8];
    char rdevmajor[8];
    char rdevminor[8];
    char namesize[8];
    char check[8];
};

/* Basit string yardımcıları (standart kütüphane yokken) */
static int unsigned_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int unsigned_strncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == '\0') break;
    }
    return 0;
}

static char* unsigned_strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++) {
        if (unsigned_strncmp(haystack, needle, (int)__builtin_strlen(needle)) == 0)
            return (char*)haystack;
    }
    return 0;
}

static void unsigned_strcat(char *dest, const char *src) {
    while (*dest) dest++;
    while ((*dest++ = *src++));
}

/* Sekizli (hex) stringi tam sayıya çeviren yardımcı fonksiyon */
static uint32_t parse_hex(const char *s, int len) {
    uint32_t val = 0;
    for (int i = 0; i < len; i++) {
        char c = s[i];
        val <<= 4;
        if (c >= '0' && c <= '9') val += (c - '0');
        else if (c >= 'A' && c <= 'F') val += (c - 'A' + 10);
        else if (c >= 'a' && c <= 'f') val += (c - 'a' + 10);
    }
    return val;
}

/* CPIO içinden dosya bulan fonksiyon */
uint8_t* cpio_find_file(const char *archive, const char *filename, uint32_t *out_size) {
    const char *ptr = archive;
    
    while (1) {
        struct cpio_header *head = (struct cpio_header*)ptr;
        if (unsigned_strncmp(head->magic, "070701", 6) != 0) break;
        
        uint32_t filesize = parse_hex(head->filesize, 8);
        uint32_t namesize = parse_hex(head->namesize, 8);
        
        const char *name = ptr + sizeof(struct cpio_header);
        
        if (unsigned_strncmp(name, "TRAILER!!!", 10) == 0) break;
        
        // CPIO isimleri başında "./" ile başlayabilir, kontrolü esnetelim
        const char *check_name = name;
        if (name[0] == '.' && name[1] == '/') check_name += 2;
        
        if (unsigned_strcmp(check_name, filename) == 0) {
            // Verinin başladığı yer (Header + Namesize 4 bayta hizalanır)
            uint32_t offset = sizeof(struct cpio_header) + namesize;
            offset = (offset + 3) & ~3; // 4-byte alignment
            *out_size = filesize;
            return (uint8_t*)(ptr + offset);
        }
        
        // Bir sonraki dosya kaydına atla (Veri alanı da 4 bayta hizalanır)
        uint32_t next_offset = sizeof(struct cpio_header) + namesize;
        next_offset = (next_offset + 3) & ~3;
        next_offset += filesize;
        next_offset = (next_offset + 3) & ~3;
        
        ptr += next_offset;
    }
    return 0;
}

/* font.cfg okuyup dinamik font yükler */
void init_dynamic_font(void) {
    uint32_t cfg_size = 0;
    char *cfg_data = (char*)cpio_find_file((const char*)initramfs_mod_start, "fonts/font.cfg", &cfg_size);
    
    char font_name[64] = "JetBrainsMono-Regular.ttf"; // Fallback varsayılan
    
    if (cfg_data && cfg_size > 0) {
        // Basit FONT= ayrıştırması
        char *line = unsigned_strstr(cfg_data, "FONT=");
        if (line) {
            line += 5; // "FONT=" kısmını geç
            int i = 0;
            while (line[i] != '\n' && line[i] != '\r' && line[i] != '\0' && i < 63) {
                font_name[i] = line[i];
                i++;
            }
            font_name[i] = '\0';
        }
    }
    
    // Şimdi config'den çıkan veya fallback olan fontu yükle
    uint32_t font_size = 0;
    char font_path[128] = "fonts/";
    unsigned_strcat(font_path, font_name); // fonts/JetBrainsMono-Regular.ttf
    
    uint8_t *font_data = cpio_find_file((const char*)initramfs_mod_start, font_path, &font_size);
    
    if (font_data && font_size > 0) {
        bootlog_info("Loading dynamic font...");
        ttf_init(font_data, font_size);
        ttf_set_size(18); // Font boyutunu ata
        bootlog_ok("Dynamic font loaded successfully");
    } else {
        bootlog_warn("Dynamic font not found, using embedded fallback");
    }
}

/* ── SATA boot info ─────────────────────────────────────── */
static void report_sata(void) {
    int any = 0;
    for (int d = 0; d < ATA_MAX_DRIVES; d++) {
        if (!ata_drives[d].present) continue;
        if (!any) { bootlog_ok("ATA/SATA drives detected:"); any = 1; }
        print_str("         drive");
        putchar('0' + d);
        print_str(ata_drives[d].is_sata ? " [SATA] " : " [ATA]  ");
        print_str(ata_drives[d].model);
        print_str(" – ");
        uint32_t mb = (ata_drives[d].sectors) / 2048;
        print_int((int)mb); print_str(" MB\n");
    }
    if (!any) bootlog_warn("No ATA/SATA drives found");
}

/* ── ES1 boot info ──────────────────────────────────────── */
static void report_es1(void) {
    print_str_color("[  OK  ] ", (char)0xA);
    print_str("ES1 filesystem mounted: ");
    print_int((int)es1_sb.used_nodes);
    print_str("/");
    print_int(ES1_MAX_NODES);
    print_str(" nodes, label=\"");
    print_str(es1_sb.label);
    print_str("\"\n");
}

/* ── Shell ──────────────────────────────────────────────── */
void print_prompt(void) {
    tty_print_prompt(current_user, system_hostname);
}

void process_command(void) {
    shell_buffer[buf_idx] = '\0';
    putchar('\n');

    if (buf_idx > 0) {
        if (history_count < 10) str_copy(history[history_count++], shell_buffer);
        else {
            for (int i = 0; i < 9; i++) str_copy(history[i], history[i+1]);
            str_copy(history[9], shell_buffer);
        }
        history_index = history_count;

        char cmd_name[32]; int i = 0;
        while (shell_buffer[i] != ' ' && shell_buffer[i] != '\0' && i < 31) {
            cmd_name[i] = shell_buffer[i]; i++;
        }
        cmd_name[i] = '\0';
        const char* args = (shell_buffer[i] == ' ') ? &shell_buffer[i+1] : "";

        int found = 0;
        for (int k = 0; k < command_count; k++) {
            if (str_match(cmd_name, command_table[k].name)) {
                command_table[k].func(args); found = 1; break;
            }
        }
        if (!found) {
            print_str_color("BNU-SH: command not found: ", (char)0xC);
            print_str(cmd_name); putchar('\n');
        }
    }

    print_prompt();
    buf_idx = 0;
}

/* ── Kernel entry ─────────────────────────────────────────
 * GRUB passes mb2_magic in rdi, mb2_info in rsi (SysV64 ABI)
 * via boot.s which moves eax→edi, ebx→esi before the call.
 * ──────────────────────────────────────────────────────── */
void kernel_main(uint32_t mb2_magic, uint64_t mb2_info) {
    /* 1. Init framebuffer (TTY) */
    fb_init(mb2_magic, (void*)(uintptr_t)mb2_info);

    if (!fb.addr) {
        __asm__ volatile ("cli; hlt");
    }

    /* 2. Locate initramfs before ES1 (ES1 needs the CPIO address) */
    detect_initramfs(mb2_magic, (void*)(uintptr_t)mb2_info);

    /* 3. Dinamik font yükle (CPIO içinden font.cfg ve .ttf) */
    init_dynamic_font();

    /* 4. Init filesystem */
    es1_init();

    current_layout = kbd_us;

    /* ── Boot sequence ─────────────────────────────────── */
    bootlog_info("BlexOS x86_64 starting..."); boot_delay();
    bootlog_ok("Framebuffer TTY initialised (1920x1080)");

    bootlog_info("Initialising ES1 filesystem...");
    report_es1();

    bootlog_info("Probing ATA/SATA drives...");
    ata_init();
    report_sata();

    bootlog_info("Probing PS/2 keyboard..."); boot_delay();
    bootlog_ok("Keyboard driver ready");

    bootlog_info("Starting shell service..."); boot_delay();

    tty_print_color("\n  BlexOS x86_64 v2.0   (c) 2026 Blex\n\n", COL_CYAN);
    tty_print_color("  Filesystem: ES1 (Embed File System 1)\n",  COL_TEXT_FG);
    tty_print_color("  Storage:    ATA/SATA PIO driver\n\n",       COL_TEXT_FG);
    print_str("Type 'help' for a list of commands.\n\n");
    print_prompt();

    /* ── Main keyboard loop ────────────────────────────── */
    static int ctrl_pressed  = 0;
    static int shift_pressed = 0;

    while (1) {
        boot_ticks++;

        if (inb(0x64) & 1) {
            unsigned char sc  = inb(0x60);
            unsigned char key = sc & 0x7F;
            int is_release    = (sc & 0x80);

            if (key == 0x1D) { ctrl_pressed  = !is_release; continue; }
            if (key == 0x2A || key == 0x36) { shift_pressed = !is_release; continue; }

            if (is_release) continue;

            if (key == 0x3A) { caps_lock = !caps_lock; continue; }

            if (key == 0x48) {   /* Up arrow – history */
                if (history_count > 0 && history_index > 0) {
                    history_index--;
                    while (buf_idx > 0) { putchar('\b'); buf_idx--; }
                    str_copy(shell_buffer, history[history_index]);
                    for (int j = 0; shell_buffer[j]; j++) { putchar(shell_buffer[j]); buf_idx++; }
                }
            } else if (key == 0x50) {   /* Down arrow – history */
                if (history_index < history_count - 1) {
                    history_index++;
                    while (buf_idx > 0) { putchar('\b'); buf_idx--; }
                    str_copy(shell_buffer, history[history_index]);
                    for (int j = 0; shell_buffer[j]; j++) { putchar(shell_buffer[j]); buf_idx++; }
                }
            } else {
                char c = (char)current_layout[key];

                if (shift_pressed || caps_lock) {
                    if (c >= 'a' && c <= 'z') c -= 32;
                }

                if (c == '\n')      process_command();
                else if (c == '\b') { if (buf_idx > 0) { buf_idx--; putchar('\b'); } }
                else if (c && buf_idx < 63) { shell_buffer[buf_idx++] = c; putchar(c); }
            }
        }
        for (volatile int i = 0; i < 1000; i++);
    }
}
