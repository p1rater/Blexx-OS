/*
 * command_logic.c  BlexOS x86_64 shell commands
 * Copyright (C) 2026 Blex  BOSL License
 *
 * All filesystem operations go through ES1.
 * lsdev / lsusb / lspci query real PCI hardware.
 * The editor (cmd_edit) uses the framebuffer TTY, not VGA text.
 * fetch supports PNG image rendering via stb_image from initramfs.
 *
 * PNG fetch image path: /initramfs/etc/fetchpng/<any .png file>
 * The first PNG found in that directory is rendered next to sysinfo.
 */

#include "commands.h"
#include <stdint.h>

/*  stb_image  implemented in commands/stb_image_impl.c 
 * That TU owns the bump allocator, memset/memcpy shims, and the
 * STB_IMAGE_IMPLEMENTATION define.  We only need the declarations. */
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#include "stb_image.h"   /* declarations only  no STB_IMAGE_IMPLEMENTATION here */

/* Reset the bump allocator after each decode (defined in stb_image_impl.c) */
extern void stbi_bump_reset(void);

extern void fb_put_pixel(int x, int y, uint32_t color);

#ifndef NULL
#define NULL ((void*)0)
#endif

/*  Externals  */
extern unsigned char kbd_us[128];
extern char          current_user[16];
extern char          system_hostname[16];
extern unsigned int  boot_ticks;
extern char          current_color;
extern int           cursor_x;
extern int           cursor_y;
extern void          print_int(int n);
extern void          print_str(const char* str);
extern void          print_str_color(const char* str, char color);
extern void          putchar(char c);
extern void          clear_screen(void);
extern void          brun_exec(const char* filename);

/* fb.h WM functions + framebuffer state */
#include "../fb.h"
extern int focused_win;

/*  Shell state  */
char          cwd_path[256]  = "/";
char          history[10][64];
int           history_count  = 0;
int           history_index  = -1;
unsigned char *current_layout;
char          pipe_temp_file[32] = "";

/*  String helpers  */
int str_match(const char* s1, const char* s2) {
    int i = 0;
    while (s1[i] && s2[i]) { if (s1[i] != s2[i]) return 0; i++; }
    return (s1[i] == '\0' && s2[i] == '\0');
}

void str_copy(char* dest, const char* src) {
    int i = 0;
    while (src[i]) { dest[i] = src[i]; i++; }
    dest[i] = '\0';
}

int str_len(const char* s) { int i=0; while(s[i]) i++; return i; }

void str_append(char *dest, const char *src) {
    int i = str_len(dest), j = 0;
    while (src[j]) dest[i++] = src[j++];
    dest[i] = '\0';
}

static void str_int(char *buf, int n) {
    if (n < 0) { buf[0]='-'; str_int(buf+1, -n); return; }
    char tmp[20]; int i=0;
    if (n == 0) { buf[0]='0'; buf[1]='\0'; return; }
    while (n > 0) { tmp[i++] = (char)('0' + n%10); n /= 10; }
    for (int j=0; j<i; j++) buf[j] = tmp[i-1-j];
    buf[i] = '\0';
}

static void pad_right(char* buf, const char* s, int w) {
    int i = 0;
    for (; s[i] && i < w; i++) buf[i] = s[i];
    for (; i < w; i++) buf[i] = ' ';
    buf[w] = '\0';
}

static char* internal_strchr(const char *s, int c) {
    char ch = (char)c;
    while (*s) { if (*s == ch) return (char*)s; s++; }
    return (char*)(*s == ch ? (char*)s : 0);
}

static char* internal_strtok(char *str, const char *delim) {
    static char *last = NULL;
    if (str) last = str;
    if (!last || !*last) return NULL;
    while (*last && internal_strchr(delim, *last)) last++;
    if (!*last) return NULL;
    char *tok = last;
    while (*last && !internal_strchr(delim, *last)) last++;
    if (*last) { *last = '\0'; last++; } else { last = NULL; }
    return tok;
}

/*  Path helpers  */
static char* normalize_path(const char *path, char *resolved) {
    char tmp[256];
    char *part[64];
    int count = 0;
    str_copy(tmp, path);
    char *tok = internal_strtok(tmp, "/");
    while (tok && count < 64) {
        if (str_match(tok, "."))       { }
        else if (str_match(tok, "..")) { if (count > 0) count--; }
        else                           { part[count++] = tok; }
        tok = internal_strtok(NULL, "/");
    }
    resolved[0] = '/'; int pos = 1;
    for (int i = 0; i < count; i++) {
        int len = str_len(part[i]);
        if (pos + len + 1 >= 256) break;
        for (int j=0; j<len; j++) resolved[pos++] = part[i][j];
        resolved[pos++] = '/';
    }
    if (pos > 1) pos--;
    resolved[pos] = '\0';
    if (!resolved[0]) str_copy(resolved, "/");
    return resolved;
}

static void resolve_path(const char *input, char *out) {
    char abs_path[256];
    if (input[0] == '/') {
        str_copy(abs_path, input);
    } else {
        str_copy(abs_path, cwd_path);
        int l = str_len(abs_path);
        if (l > 0 && abs_path[l-1] != '/') { abs_path[l]='/'; abs_path[l+1]='\0'; }
        str_append(abs_path, input);
    }
    normalize_path(abs_path, out);
}

/*  ES1 helpers  */
static int node_is_dir(const es1_node_t *n) {
    return (int)(n->flags & ES1_FL_DIR);
}
static int user_can(const es1_node_t *n, char need) {
    if (str_match(n->owner, current_user))
        return (n->perms[1]==need)||(n->perms[2]==need)||(n->perms[3]==need);
    return (n->perms[7]==need)||(n->perms[8]==need)||(n->perms[9]==need);
}

/* print a hex word */
void print_hex(uint32_t n) {
    const char *hx = "0123456789ABCDEF";
    print_str("0x");
    for (int i=28; i>=0; i-=4) {
        char c = hx[(n>>i)&0xF];
        char t[2]={c,'\0'}; print_str(t);
    }
}

/*  Hardware helpers  */
static unsigned int get_total_ram(void) {
    unsigned short lo, hi;
    outb(0x70, 0x30); lo = inb(0x71);
    outb(0x70, 0x31); hi = inb(0x71);
    return ((unsigned int)((hi << 8) | lo) / 1024) + 1;
}
static unsigned char bcd(unsigned char v) { return (v >> 4)*10 + (v&0xF); }

/* read_line  blocking PS/2 input using kbd_us table */
void read_line(char* buf, int limit) {
    int i = 0;
    while (i < limit - 1) {
        while (!(inb(0x64) & 1));
        unsigned char sc = inb(0x60);
        if (sc & 0x80) continue;
        if (sc == 0x1C) { putchar('\n'); break; }
        if (sc == 0x0E) {
            if (i > 0) { i--; putchar('\b'); putchar(' '); putchar('\b'); }
        } else {
            char c = (char)kbd_us[sc];
            if (c >= 32 && c <= 126) { buf[i++]=c; putchar(c); }
        }
        for (volatile int d=0; d<100000; d++);
    }
    buf[i] = '\0';
}

/*  Commands: filesystem  */

void cmd_ls(const char *a) {
    char target[256];
    if (!a[0]) str_copy(target, cwd_path);
    else       resolve_path(a, target);

    int tlen = str_len(target);
    if (tlen > 1 && target[tlen-1] != '/') { target[tlen]='/'; target[tlen+1]='\0'; tlen++; }

    int found = 0;
    putchar('\n');
    for (int i = 0; i < ES1_MAX_NODES; i++) {
        es1_node_t *n = &es1_nodes[i];
        if (!(n->flags & ES1_FL_USED)) continue;

        int plen = str_len(n->path);
        if (plen <= tlen) continue;
        int match = 1;
        for (int j=0; j<tlen; j++) { if (n->path[j]!=target[j]) { match=0; break; } }
        if (!match) continue;
        const char *rest = n->path + tlen;
        if (internal_strchr(rest, '/')) continue;

        if (node_is_dir(n))
            print_str_color(rest, VGA_ATTR(COL_LCYAN, COL_BLACK));
        else if (n->flags & ES1_FL_EXEC)
            print_str_color(rest, VGA_ATTR(COL_LGREEN, COL_BLACK));
        else
            print_str(rest);
        print_str("  ");
        found++;
    }
    if (!found) print_str("(empty)");
    putchar('\n');
}

void cmd_cd(const char *a) {
    if (!a[0]) { str_copy(cwd_path, "/"); return; }
    char new_path[256];
    resolve_path(a, new_path);
    es1_node_t *node = es1_find(new_path);
    if (node && node_is_dir(node)) {
        str_copy(cwd_path, new_path);
    } else {
        print_str_color("cd: no such directory: ", VGA_ATTR(COL_LRED, COL_BLACK));
        print_str(a); putchar('\n');
    }
}

void cmd_pwd(const char *a) { (void)a; putchar('\n'); print_str(cwd_path); putchar('\n'); }

void cmd_mkdir(const char *a) {
    if (!a[0]) { print_str("mkdir: missing operand\n"); return; }
    char full[256]; resolve_path(a, full);
    if (es1_find(full)) { print_str("mkdir: already exists\n"); return; }
    es1_node_t *n = es1_alloc(full, ES1_FL_USED | ES1_FL_DIR);
    if (!n) { print_str("mkdir: filesystem full\n"); return; }
    str_copy(n->owner, current_user);
    str_copy(n->perms, "rwxr-xr-x");
}

void cmd_touch(const char *a) {
    if (!a[0]) { print_str("touch: missing operand\n"); return; }
    char full[256]; resolve_path(a, full);
    if (es1_find(full)) return;
    es1_node_t *n = es1_alloc(full, ES1_FL_USED);
    if (!n) { print_str("touch: filesystem full\n"); return; }
    str_copy(n->owner, current_user);
    str_copy(n->perms, "rw-r--r--");
}

void cmd_rm(const char *a) {
    if (!a[0]) { print_str("rm: missing operand\n"); return; }
    char full[256]; resolve_path(a, full);
    es1_node_t *node = es1_find(full);
    if (!node) { print_str("rm: no such file\n"); return; }
    if (node_is_dir(node)) { print_str("rm: is a directory (use rmdir)\n"); return; }
    if (!user_can(node, 'w')) { print_str("rm: permission denied\n"); return; }
    es1_free(full);
    print_str("removed\n");
}

void cmd_rmdir(const char *a) {
    if (!a[0]) { print_str("rmdir: missing operand\n"); return; }
    char full[256]; resolve_path(a, full);
    es1_node_t *node = es1_find(full);
    if (!node) { print_str("rmdir: no such directory\n"); return; }
    if (!node_is_dir(node)) { print_str("rmdir: not a directory\n"); return; }
    int tlen = str_len(full);
    for (int i=0; i<ES1_MAX_NODES; i++) {
        if (!(es1_nodes[i].flags & ES1_FL_USED)) continue;
        if (&es1_nodes[i] == node) continue;
        int plen = str_len(es1_nodes[i].path);
        if (plen <= tlen) continue;
        int match=1;
        for (int j=0; j<tlen; j++) if (es1_nodes[i].path[j]!=full[j]) { match=0; break; }
        if (match) { print_str("rmdir: directory not empty\n"); return; }
    }
    es1_free(full);
    print_str("removed\n");
}

void cmd_cat(const char *a) {
    if (!a[0]) { print_str("cat: missing filename\n"); return; }
    char full[256]; resolve_path(a, full);
    es1_node_t *node = es1_find(full);
    if (!node) { print_str("cat: file not found\n"); return; }
    if (node_is_dir(node)) { print_str("cat: is a directory\n"); return; }
    if (!user_can(node, 'r')) { print_str("cat: permission denied\n"); return; }
    putchar('\n');
    print_str(node->inline_data);
    if (!node->size || node->inline_data[node->size-1] != '\n') putchar('\n');
}

void cmd_write(const char *a) {
    if (!a[0]) { print_str("Usage: write <file> <text>\n"); return; }
    char fname[64]; int i=0;
    while (a[i] && a[i]!=' ' && i<63) { fname[i]=a[i]; i++; }
    fname[i] = '\0';
    const char *text = (a[i]==' ') ? &a[i+1] : "";
    char full[256]; resolve_path(fname, full);
    if (!es1_find(full)) cmd_touch(fname);
    int r = es1_write(full, text, (uint32_t)str_len(text));
    if (r < 0) print_str("write: error\n");
    else print_str("written\n");
}

void cmd_stat(const char *a) {
    if (!a[0]) { print_str("Usage: stat <file>\n"); return; }
    char full[256]; resolve_path(a, full);
    es1_node_t *node = es1_find(full);
    if (!node) { print_str("stat: file not found\n"); return; }
    putchar('\n');
    print_str("  Path:  "); print_str(node->path); putchar('\n');
    print_str("  Type:  "); print_str(node_is_dir(node) ? "directory" : "regular file"); putchar('\n');
    print_str("  Size:  "); print_int((int)node->size); print_str(" B\n");
    print_str("  Perms: "); print_str(node->perms); putchar('\n');
    print_str("  Owner: "); print_str(node->owner); putchar('\n');
    putchar('\n');
}

void cmd_cp(const char *a) {
    char src[64], dst_name[64]; int i=0;
    while (a[i] && a[i]!=' ' && i<63) { src[i]=a[i]; i++; } src[i]='\0';
    if (a[i]!=' ') { print_str("Usage: cp <src> <dst>\n"); return; }
    i++;
    int j=0;
    while (a[i+j] && j<63) { dst_name[j]=a[i+j]; j++; } dst_name[j]='\0';

    char fsrc[256], fdst[256];
    resolve_path(src, fsrc);
    resolve_path(dst_name, fdst);

    es1_node_t *snode = es1_find(fsrc);
    if (!snode) { print_str("cp: source not found\n"); return; }
    if (node_is_dir(snode)) { print_str("cp: cannot copy directory\n"); return; }

    if (!es1_find(fdst)) cmd_touch(dst_name);
    es1_write(fdst, snode->inline_data, snode->size);
    print_str("copied\n");
}

void cmd_mv(const char *a) {
    char src[64], dst_name[64]; int i=0;
    while (a[i] && a[i]!=' ' && i<63) { src[i]=a[i]; i++; } src[i]='\0';
    if (a[i]!=' ') { print_str("Usage: mv <src> <dst>\n"); return; }
    i++;
    int j=0;
    while (a[i+j] && j<63) { dst_name[j]=a[i+j]; j++; } dst_name[j]='\0';

    char fsrc[256], fdst[256];
    resolve_path(src, fsrc);
    resolve_path(dst_name, fdst);

    es1_node_t *snode = es1_find(fsrc);
    if (!snode) { print_str("mv: source not found\n"); return; }
    if (!es1_find(fdst)) cmd_touch(dst_name);
    es1_write(fdst, snode->inline_data, snode->size);
    es1_free(fsrc);
    print_str("moved\n");
}

/*  Commands: hardware info  */

void cmd_lsdev(const char *args) {
    (void)args;
    print_str("\nDEVICE         TYPE       STATUS   DESCRIPTION\n");
    print_str("      \n");
    print_str("kbd0           PS/2       READY    Standard PS/2 Keyboard\n");
    print_str("spk0           PC_SPKR    READY    Programmable Interval Speaker\n");
    print_str("pit0           TIMER      ACTIVE   Intel 8253/8254 PIT\n");
    print_str("pic0           PIC        ACTIVE   8259A Interrupt Controller\n");
    print_str("rtc0           RTC        READY    CMOS Real-Time Clock\n");

    for (int d = 0; d < ATA_MAX_DRIVES; d++) {
        if (!ata_drives[d].present) continue;
        char name[8] = "sda0";
        name[3] = (char)('0' + d);
        print_str(name);
        print_str(ata_drives[d].is_sata ? "           SATA       ACTIVE   " :
                                          "           ATA        ACTIVE   ");
        print_str(ata_drives[d].model); putchar('\n');
    }

    for (int bus=0; bus<8; bus++) {
        for (int slot=0; slot<32; slot++) {
            uint32_t id = pci_read_config((uint8_t)bus,(uint8_t)slot,0,0);
            if ((id & 0xFFFF) == 0xFFFF) continue;
            uint32_t cls = pci_read_config((uint8_t)bus,(uint8_t)slot,0,0x08);
            uint8_t cc  = (cls>>24)&0xFF;
            uint8_t sc  = (cls>>16)&0xFF;
            const char *type = "PCI      ";
            if (cc==0x02)                      type = "NETWORK  ";
            else if (cc==0x01 && sc==0x06)     type = "SATA     ";
            else if (cc==0x01 && sc==0x01)     type = "IDE      ";
            else if (cc==0x0C && sc==0x03)     type = "USB_EHCI ";
            else if (cc==0x0C && sc==0x00)     type = "USB_UHCI ";
            else if (cc==0x03)                 type = "GPU      ";
            else if (cc==0x04)                 type = "AUDIO    ";
            print_str("pci"); print_int(bus); print_str(":"); print_int(slot);
            print_str("          "); print_str(type);
            print_str(" ACTIVE   ");
            print_hex(id & 0xFFFF); print_str(":"); print_hex((id>>16)&0xFFFF);
            putchar('\n');
        }
    }
}

void cmd_lsusb(const char *args) {
    (void)args;
    print_str("\nUSB controllers:\n");
    print_str("BUS:DEV  CLASS      VENDOR:DEVICE  DESCRIPTION\n");
    print_str("     \n");
    int found = 0;
    for (int bus=0; bus<8; bus++) {
        for (int slot=0; slot<32; slot++) {
            uint32_t cls = pci_read_config((uint8_t)bus,(uint8_t)slot,0,0x08);
            uint8_t cc = (cls>>24)&0xFF;
            uint8_t sc = (cls>>16)&0xFF;
            uint8_t pi = (cls>> 8)&0xFF;
            if (cc != 0x0C) continue;
            uint32_t id = pci_read_config((uint8_t)bus,(uint8_t)slot,0,0);
            if ((id & 0xFFFF) == 0xFFFF) continue;
            const char *utype = "USB     ";
            if      (sc==0x03 && pi==0x00) utype = "UHCI    ";
            else if (sc==0x03 && pi==0x10) utype = "OHCI    ";
            else if (sc==0x03 && pi==0x20) utype = "EHCI    ";
            else if (sc==0x03 && pi==0x30) utype = "xHCI    ";
            print_int(bus); print_str(":"); print_int(slot);
            print_str("      ");
            print_str(utype); print_str("   ");
            print_hex(id&0xFFFF); print_str(":"); print_hex((id>>16)&0xFFFF);
            print_str("  USB Controller\n");
            found++;
        }
    }
    if (!found) print_str("  (no USB controllers found via PCI)\n");
    putchar('\n');
}

void cmd_lspci(const char *args) {
    (void)args;
    print_str("\nBUS:SLOT.FUNC  VENDOR:DEVICE  CLASS\n");
    print_str("    \n");
    for (int bus=0; bus<8; bus++) {
        for (int slot=0; slot<32; slot++) {
            uint32_t id = pci_read_config((uint8_t)bus,(uint8_t)slot,0,0);
            uint16_t vendor = id & 0xFFFF;
            if (vendor == 0xFFFF) continue;
            uint16_t device = (id>>16)&0xFFFF;
            uint32_t cls = pci_read_config((uint8_t)bus,(uint8_t)slot,0,0x08);
            uint8_t cc=(cls>>24)&0xFF, sc=(cls>>16)&0xFF;
            print_int(bus); print_str(":"); print_int(slot); print_str(".0       ");
            print_hex(vendor); print_str(":"); print_hex(device); print_str("       ");
            char cbuf[6];
            str_int(cbuf, (int)cc);
            print_str(cbuf); print_str("/");
            str_int(cbuf, (int)sc);
            print_str(cbuf); putchar('\n');
        }
    }
    putchar('\n');
}

void cmd_lssata(const char *args) {
    (void)args;
    print_str("\nDRIVE  TYPE   SECTORS     SIZE (MB)  MODEL\n");
    print_str("\n");
    int any = 0;
    for (int d=0; d<ATA_MAX_DRIVES; d++) {
        if (!ata_drives[d].present) continue;
        print_str("sda"); putchar((char)('0'+d));
        print_str(ata_drives[d].is_sata ? "   SATA   " : "   ATA    ");
        print_int((int)ata_drives[d].sectors);
        print_str("   ");
        print_int((int)(ata_drives[d].sectors / 2048));
        print_str("         ");
        print_str(ata_drives[d].model);
        putchar('\n');
        any++;
    }
    if (!any) print_str("  (no drives found)\n");
    putchar('\n');
}

void cmd_cpuinfo(const char *a) {
    (void)a;
    uint32_t eax, ebx, ecx, edx;
    char vendor[13] = {0};
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
    *(uint32_t*)(vendor)   = ebx;
    *(uint32_t*)(vendor+4) = edx;
    *(uint32_t*)(vendor+8) = ecx;
    print_str("\nCPU Vendor: "); print_str(vendor);
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    print_str("\nFamily: ");   print_int((int)((eax>>8)&0xF));
    print_str("  Model: ");   print_int((int)((eax>>4)&0xF));
    print_str("  Stepping: ");print_int((int)(eax&0xF));
    print_str("\nFeatures: ");
    if (edx & (1<<23)) print_str("MMX ");
    if (edx & (1<<25)) print_str("SSE ");
    if (edx & (1<<26)) print_str("SSE2 ");
    if (ecx & (1<< 0)) print_str("SSE3 ");
    if (ecx & (1<<28)) print_str("AVX ");
    uint32_t ex_eax, ex_ecx, ex_edx;
    __asm__ volatile("cpuid" : "=a"(ex_eax), "=b"(ebx), "=c"(ex_ecx), "=d"(ex_edx) : "a"(0x80000001));
    if (ex_edx & (1<<29)) print_str("64bit ");
    print_str("\nMode: x86_64 (long mode active)\n");
}

void cmd_meminfo(const char *a) {
    (void)a;
    unsigned int total = get_total_ram();
    print_str("\n              total\nRAM (MB):     "); print_int((int)total);
    print_str("\nES1 nodes:    "); print_int((int)es1_sb.used_nodes);
    print_str("/"); print_int(ES1_MAX_NODES);
    print_str("\nES1 bytes:    "); print_int((int)es1_sb.total_bytes);
    print_str("\n\n");
}

void cmd_partinfo(const char *a) { (void)a; cmd_lssata(""); }

/*  Commands: help  */
void cmd_help(const char *a) {
    (void)a;
    extern command_t command_table[];
    extern int command_count;
    int per_col = (command_count + 2) / 3;
    print_str_color("\nBlexOS Shell Commands\n", VGA_ATTR(COL_YELLOW, COL_BLACK));
    for (int row=0; row<per_col; row++) {
        for (int col=0; col<3; col++) {
            int idx = row + col*per_col;
            if (idx >= command_count) break;
            char buf[14]; pad_right(buf, command_table[idx].name, 13);
            print_str_color(buf, VGA_ATTR(COL_LGREEN, COL_BLACK));
            print_str("  ");
        }
        putchar('\n');
    }
    print_str("\nType 'man <cmd>' for details.\n\n");
}

void cmd_man(const char *a) {
    if (!a[0]) { cmd_help(""); return; }
    extern command_t command_table[];
    extern int command_count;
    for (int k=0; k<command_count; k++) {
        if (str_match(a, command_table[k].name)) {
            putchar('\n');
            print_str_color("NAME\n", VGA_ATTR(COL_YELLOW, COL_BLACK));
            print_str("  "); print_str(command_table[k].name); putchar('\n');
            print_str_color("DESCRIPTION\n", VGA_ATTR(COL_YELLOW, COL_BLACK));
            print_str("  "); print_str(command_table[k].help); putchar('\n');
            putchar('\n'); return;
        }
    }
    print_str("man: no manual for '"); print_str(a); print_str("'\n");
}

/*  Editor (WM-based)  */
#define ED_ROWS   48
#define ED_COLS  180

typedef struct { int size; char chars[ED_COLS]; } ed_row_t;

static struct {
    int      cx, cy, rowoff;
    int      numrows;
    char     filename[64];
    int      dirty;
    ed_row_t row[ED_ROWS];
} E;

void cmd_edit(const char *args) {
    if (!args[0]) { print_str("Usage: edit <filename>\n"); return; }
    str_copy(E.filename, args);
    E.cx=0; E.cy=0; E.rowoff=0; E.numrows=0; E.dirty=0;
    for (int i=0; i<ED_ROWS; i++) { E.row[i].size=0; E.row[i].chars[0]='\0'; }

    char full[256]; resolve_path(args, full);
    es1_node_t *node = es1_find(full);
    if (node && !node_is_dir(node)) {
        const char *content = node->inline_data;
        int r=0, c=0;
        for (int i=0; content[i] && r<ED_ROWS; i++) {
            if (content[i]=='\n') { E.row[r].chars[c]='\0'; E.row[r].size=c; r++; c=0; }
            else if (c < ED_COLS-1) E.row[r].chars[c++]=content[i];
        }
        if (c>0) { E.row[r].chars[c]='\0'; E.row[r].size=c; r++; }
        E.numrows = r;
    }
    if (E.numrows==0) E.numrows=1;

    clear_screen();

    auto void redraw(void);
    void redraw(void) {
        clear_screen();
        wm_print_color(" BlexEdit   File: ", COL_CYAN);
        wm_print_color(E.filename, COL_YELLOW);
        if (E.dirty) wm_print_color("  [MODIFIED]", COL_RED);
        wm_print_color("  ^S=Save  ^Q=Quit\n", COL_TEXT_FG);
        wm_print_color("\n", COL_TEXT_DIM);

        for (int y=0; y<ED_ROWS; y++) {
            int file_row = y + E.rowoff;
            char lnbuf[6]; str_int(lnbuf, file_row+1);
            if (file_row < E.numrows) {
                wm_print_color(lnbuf, COL_TEXT_DIM);
                wm_print_color("  ", COL_TEXT_DIM);
                uint32_t col = (y == E.cy) ? COL_WHITE : COL_TEXT_FG;
                wm_print_color(E.row[file_row].chars, col);
                putchar('\n');
            } else {
                wm_print_color("~\n", COL_TEXT_DIM);
            }
        }
        wm_print_color("\n", COL_TEXT_DIM);
        wm_print_color("Line ", COL_TEXT_DIM);
        print_int(E.cy + E.rowoff + 1);
        wm_print_color(" / ", COL_TEXT_DIM);
        print_int(E.numrows);
        wm_print_color("  Col ", COL_TEXT_DIM);
        print_int(E.cx + 1);
        putchar('\n');
    }

    redraw();

    /* Track modifier keys inside the editor */
    int ed_ctrl  = 0;
    int ed_shift = 0;

    while (1) {
        while (!(inb(0x64) & 1));
        unsigned char sc = inb(0x60);
        unsigned char key = sc & 0x7F;
        int is_release = (sc & 0x80);

        /* Update modifier state */
        if (key == 0x1D) { ed_ctrl  = !is_release; continue; }
        if (key == 0x2A || key == 0x36) { ed_shift = !is_release; continue; }
        if (is_release) continue;

        /* ^Q  quit (requires Ctrl) */
        if (ed_ctrl && key == 0x10) { clear_screen(); return; }

        /* ^S  save (requires Ctrl) */
        if (ed_ctrl && key == 0x1F) {
            char sfull[256]; resolve_path(E.filename, sfull);
            if (!es1_find(sfull)) cmd_touch(E.filename);
            es1_node_t *sn = es1_find(sfull);
            if (sn && !node_is_dir(sn)) {
                char *dest = sn->inline_data;
                int p=0;
                for (int r=0; r<E.numrows && p < ES1_INLINE_SIZE-2; r++) {
                    for (int c=0; c<E.row[r].size && p < ES1_INLINE_SIZE-2; c++)
                        dest[p++] = E.row[r].chars[c];
                    dest[p++] = '\n';
                }
                dest[p]='\0'; sn->size=(uint32_t)p;
                E.dirty=0;
                wm_print_color("\n[Saved]\n", COL_GREEN);
            }
            redraw(); continue;
        }

        if (key == 0x48) { /* Up arrow */
            if (E.cy > 0) E.cy--;
            else if (E.rowoff > 0) E.rowoff--;
            /* Clamp column to new line length */
            { int fl = E.row[E.cy+E.rowoff].size; if (E.cx > fl) E.cx = fl; }
            redraw(); continue;
        }
        if (key == 0x50) { /* Down arrow */
            if (E.cy + E.rowoff < E.numrows - 1) {
                if (E.cy < ED_ROWS-1) E.cy++; else E.rowoff++;
            }
            /* Clamp column to new line length */
            { int fl = E.row[E.cy+E.rowoff].size; if (E.cx > fl) E.cx = fl; }
            redraw(); continue;
        }
        if (key == 0x4B) { /* Left arrow */
            if (E.cx > 0) E.cx--;
            redraw(); continue;
        }
        if (key == 0x4D) { /* Right arrow */
            int fl = E.row[E.cy+E.rowoff].size;
            if (E.cx < fl) E.cx++;
            redraw(); continue;
        }

        /* Enter  split the current line at the cursor */
        if (key == 0x1C) {
            if (E.numrows < ED_ROWS) {
                int file_row = E.cy + E.rowoff;
                ed_row_t *curr = &E.row[file_row];

                /* Shift all rows below down by one to make room */
                for (int r = E.numrows; r > file_row + 1; r--)
                    E.row[r] = E.row[r-1];

                /* New row gets the tail of the current row */
                ed_row_t *next = &E.row[file_row + 1];
                int tail = curr->size - E.cx;
                for (int i = 0; i < tail; i++)
                    next->chars[i] = curr->chars[E.cx + i];
                next->chars[tail] = '\0';
                next->size = tail;

                /* Truncate the current row at the cursor */
                curr->chars[E.cx] = '\0';
                curr->size = E.cx;

                E.numrows++;
                E.cx = 0;
                if (E.cy < ED_ROWS-1) E.cy++;
                else E.rowoff++;
                E.dirty = 1;
            }
            redraw(); continue;
        }

        /* Backspace  delete char before cursor; merge lines if at col 0 */
        if (key == 0x0E) {
            int file_row = E.cy + E.rowoff;
            ed_row_t *curr = &E.row[file_row];
            if (E.cx > 0) {
                /* Delete the character to the left of the cursor */
                for (int i = E.cx-1; i < curr->size-1; i++)
                    curr->chars[i] = curr->chars[i+1];
                curr->chars[--curr->size] = '\0';
                E.cx--;
                E.dirty = 1;
            } else if (file_row > 0) {
                /* At column 0  merge this line onto the previous one */
                ed_row_t *prev = &E.row[file_row - 1];
                int prev_end = prev->size;
                int tail     = curr->size;
                if (prev_end + tail < ED_COLS - 1) {
                    for (int i = 0; i < tail; i++)
                        prev->chars[prev_end + i] = curr->chars[i];
                    prev->chars[prev_end + tail] = '\0';
                    prev->size = prev_end + tail;

                    /* Remove the current row, shifting everything up */
                    for (int r = file_row; r < E.numrows - 1; r++)
                        E.row[r] = E.row[r+1];
                    E.numrows--;

                    E.cx = prev_end;
                    if (E.cy > 0) E.cy--;
                    else if (E.rowoff > 0) E.rowoff--;
                    E.dirty = 1;
                }
            }
            redraw(); continue;
        }

        /* Printable character insertion */
        {
            char ch = (char)kbd_us[key];
            if (ed_shift && ch >= 'a' && ch <= 'z') ch -= 32;
            if (ch >= 32 && ch <= 126) {
                ed_row_t *curr = &E.row[E.cy+E.rowoff];
                if (curr->size < ED_COLS-1) {
                    for (int i = curr->size; i > E.cx; i--)
                        curr->chars[i] = curr->chars[i-1];
                    curr->chars[E.cx++] = ch;
                    curr->size++;
                    curr->chars[curr->size] = '\0';
                    E.dirty = 1;
                }
                redraw();
            }
        }
    }
}

/*  Remaining simple commands  */

/*
 * cmd_date  reads the CMOS RTC registers directly.
 * BCD decoding via bcd() gives the real wall-clock time.
 */
void cmd_date(const char *a) {
    (void)a;
    outb(0x70, 0x00); unsigned char sec  = bcd(inb(0x71));
    outb(0x70, 0x02); unsigned char min  = bcd(inb(0x71));
    outb(0x70, 0x04); unsigned char hour = bcd(inb(0x71));
    outb(0x70, 0x07); unsigned char day  = bcd(inb(0x71));
    outb(0x70, 0x08); unsigned char mon  = bcd(inb(0x71));
    outb(0x70, 0x09); unsigned char year = bcd(inb(0x71));
    putchar('\n');
    print_int(2000+year); putchar('-');
    if (mon<10)  putchar('0'); print_int(mon);  putchar('-');
    if (day<10)  putchar('0'); print_int(day);  putchar(' ');
    if (hour<10) putchar('0'); print_int(hour); putchar(':');
    if (min<10)  putchar('0'); print_int(min);  putchar(':');
    if (sec<10)  putchar('0'); print_int(sec);
    print_str(" (RTC)\n");
}

/*
 * cmd_uptime  The kernel increments boot_ticks every ~1 000 inner-loop
 * iterations.  Without a calibrated timer we cannot give a precise wall
 * time; we instead report the raw PIT tick counter read from port 0x40
 * alongside the approximate loop-based uptime so the output is fully
 * hardware-derived and not a made-up number.
 *
 * PIT channel 0 decrements at 1 193 182 Hz and wraps at 65 536, giving
 * ~18.2 Hz per full period.  We read the current count to show the real
 * hardware value.
 */
void cmd_uptime(const char *a) {
    (void)a;
    /* Latch channel 0 count */
    outb(0x43, 0x00);
    unsigned char lo = inb(0x40);
    unsigned char hi = inb(0x40);
    uint16_t pit_count = (uint16_t)((hi << 8) | lo);

    /* boot_ticks is incremented once per keyboard-poll loop; its
     * granularity is machine-speed-dependent and therefore only an
     * approximation of elapsed time. */
    unsigned int approx_s = boot_ticks / 3000; /* rough estimate */

    print_str("\nUptime (approx): ");
    print_int((int)approx_s);
    print_str("s  |  PIT ch0 count: ");
    print_int((int)pit_count);
    print_str(" (1193182 Hz countdown)\n");
}

void cmd_uname(const char *a)   { (void)a; print_str("\nBlexOS x86_64  2.0  #1  BNU-SH\n"); }
void cmd_free(const char *a)    { cmd_meminfo(a); }

/*
 * cmd_reboot  pulse the PS/2 controller reset line (port 0x64, cmd 0xFE).
 * This is the standard x86 software-reset mechanism and works on real hardware.
 */
void cmd_reboot(const char *a)  { (void)a; outb(0x64, 0xFE); }

/*
 * cmd_shutdown  ACPI S5 soft-off via the two most common QEMU/Bochs/OVMF
 * power-management ports.  On real hardware the ACPI PM1a_CNT register
 * address comes from the FADT; we use the QEMU defaults (0x604 / 0x4004)
 * which cover the vast majority of emulated environments.
 * After the port writes we disable interrupts and halt as a fallback.
 */
void cmd_shutdown(const char *a){
    (void)a;
    print_str_color("\nShutting down BlexOS...\n", 0x0C);
    outw(0x604, 0x2000);   /* QEMU ACPI S5 */
    outw(0x4004, 0x3400);  /* Bochs/older QEMU */
    __asm__ volatile("cli; hlt");
}

void cmd_history(const char *a) { (void)a; putchar('\n'); for (int i=0; i<history_count; i++) { print_int(i+1); print_str("  "); print_str(history[i]); putchar('\n'); } }
void cmd_echo(const char *a)    { putchar('\n'); print_str(a); putchar('\n'); }
void cmd_exit(const char *a)    { (void)a; clear_screen(); __asm__ volatile("cli; hlt"); }
void cmd_ver(const char *a)     { (void)a; print_str("\nBlexOS x86_64 v2.0  kernel 2.0.0  [BOSL]\n"); }
void cmd_sync(const char *a)    { (void)a; print_str("\nsync: ES1 is in-memory  nothing to flush\n"); }
void cmd_clear(const char *a)   { (void)a; clear_screen(); }
void cmd_brun(const char *a)    { if (!a[0]) { print_str("Usage: brun <filename>\n"); return; } brun_exec(a); }

/*  PNG image blit 
 *
 * Renders a decoded RGBA image directly into the VESA framebuffer.
 * The image is placed at (dst_x, dst_y) and scaled to (out_w  out_h)
 * using nearest-neighbour sampling (fast, no floating-point).
 *
 * fb.addr   32-bpp linear framebuffer (0x00RRGGBB)
 * fb.pitch  bytes per scanline
 */
static void fb_blit_rgba_nn(
        const unsigned char *rgba, int src_w, int src_h,
        int dst_x, int dst_y, int out_w, int out_h)
{
    if (!fb.addr || src_w <= 0 || src_h <= 0 || out_w <= 0 || out_h <= 0) return;

    int fb_w = (int)fb.width;
    int fb_h = (int)fb.height;

    for (int y = 0; y < out_h; y++) {
        int screen_y = dst_y + y;
        if (screen_y < 0 || screen_y >= fb_h) continue;
        int src_y = y * src_h / out_h;
        if (src_y >= src_h) src_y = src_h - 1;

        uint32_t *row = (uint32_t*)((uint8_t*)fb.addr + (uint32_t)screen_y * fb.pitch);

        for (int x = 0; x < out_w; x++) {
            int screen_x = dst_x + x;
            if (screen_x < 0 || screen_x >= fb_w) continue;
            int src_x = x * src_w / out_w;
            if (src_x >= src_w) src_x = src_w - 1;

            /* stb_image delivers 4 channels: R G B A */
            const unsigned char *px = rgba + ((uint32_t)src_y * (uint32_t)src_w + (uint32_t)src_x) * 4;
            unsigned char r = px[0], g = px[1], b = px[2], a = px[3];

            if (a == 0) continue; /* fully transparent  skip */

            if (a == 255) {
                row[screen_x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            } else {
                /* Simple alpha blend over whatever is already in the framebuffer */
                uint32_t bg = row[screen_x];
                unsigned char br = (bg >> 16) & 0xFF;
                unsigned char bg8= (bg >>  8) & 0xFF;
                unsigned char bb =  bg        & 0xFF;
                unsigned char inv = (unsigned char)(255 - a);
                unsigned char or_ = (unsigned char)(((uint32_t)r * a + (uint32_t)br * inv) >> 8);
                unsigned char og  = (unsigned char)(((uint32_t)g * a + (uint32_t)bg8* inv) >> 8);
                unsigned char ob  = (unsigned char)(((uint32_t)b * a + (uint32_t)bb * inv) >> 8);
                row[screen_x] = ((uint32_t)or_ << 16) | ((uint32_t)og << 8) | ob;
            }
        }
    }
}

/*  fetch PNG loader 
 *
 * Searches ES1 for the first .png file under /initramfs/etc/fetchpng/.
 * If found, decodes with stb_image and blits it at a fixed position in
 * the terminal window body next to the sysinfo text.
 *
 * The PNG must be placed in the initramfs at:
 *   /initramfs/etc/fetchpng/<anything>.png
 * during the build step (update initramfs.cpio.gz via gen_initramfs).
 */
#define FETCH_PNG_DIR   "/etc/fetchpng"
#define FETCH_IMG_X     20          /* pixels from left edge of framebuffer */
#define FETCH_IMG_Y     140         /* pixels from top   below taskbar+titlebar */
#define FETCH_IMG_W     200         /* render width  (px) */
#define FETCH_IMG_H     200         /* render height (px) */

#include "itsblex_png.h" // Kopyaladn header dosyas

static int fetch_draw_png(void) {
    // FFMPEG ile oluturduun ham resmin orijinal boyutlar
    int src_w = 150; 
    int src_h = 150;

    // YEN DEV BOYUTLAR (Byk ve net olmas iin nearest-neighbour lekleyecek)
    int out_w = 450; 
    int out_h = 450;

    // 1920x1080 ekrana gre sa tarafta en dengeli duraca koordinatlar
    int dst_x = 1100; // Geniledii iin biraz sola ektik
    int dst_y = 140;  // Yukardaki barlara arpmamas iin ideal ykseklik

    /* Resmimizi sfr bellek ykyle devasa boyuta getirip basyoruz */
    fb_blit_rgba_nn(raw_logo_bin, src_w, src_h, dst_x, dst_y, out_w, out_h);

    return 1; 
}
/*  cmd_fetch 
 *
 * Prints system information to the terminal, then attempts to render
 * the user's custom PNG from /initramfs/etc/fetchpng/ via the framebuffer.
 * Falls back to the classic coloured block palette if no PNG is found.
 *
 * All reported values come directly from hardware:
 *   CPU   CPUID leaf 0 (vendor string) + leaf 1 (family/model/stepping)
 *   RAM   CMOS extended memory registers 0x30/0x31
 *   Date  CMOS RTC registers
 *   ATA   ata_drives[] enumerated by the real ATA driver at boot
 */
void cmd_fetch(const char *args) {
    (void)args;

    /*  Hardware Bilgilerini Topla  */
    uint32_t eax, ebx, ecx, edx;
    char cpu_vendor[13] = {0};
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
    *(uint32_t*)(cpu_vendor)   = ebx;
    *(uint32_t*)(cpu_vendor+4) = edx;
    *(uint32_t*)(cpu_vendor+8) = ecx;

    uint32_t cpu_info;
    __asm__ volatile("cpuid" : "=a"(cpu_info), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    int cpu_family   = (int)((cpu_info >> 8) & 0xF);
    int cpu_model    = (int)((cpu_info >> 4) & 0xF);
    int cpu_stepping = (int)( cpu_info       & 0xF);

    /*  Sadece stenen Bilgileri Yazdr  */
    putchar('\n');
    
    // 1. OS
    print_str_color(" OS:        ", VGA_ATTR(COL_LCYAN, COL_BLACK));
    print_str("BlexOS x86_64 v2.0\n");

    // 2. KERNEL
    print_str_color(" KERNEL:    ", VGA_ATTR(COL_LCYAN, COL_BLACK));
    print_str("Blex-Core x86_64 2.0.0\n");

    // 3. FS
    print_str_color(" FS:        ", VGA_ATTR(COL_LCYAN, COL_BLACK));
    print_str("ES1 (Embed File System 1)  nodes: ");
    print_int((int)es1_sb.used_nodes); print_str("/"); print_int(ES1_MAX_NODES);
    putchar('\n');

    // 4. CPU (lemci)
    print_str_color(" CPU:       ", VGA_ATTR(COL_LCYAN, COL_BLACK));
    print_str(cpu_vendor);
    print_str("  family "); print_int(cpu_family);
    print_str("  model ");  print_int(cpu_model);
    putchar('\n');

    // 5. USER
    print_str_color(" USER:      ", VGA_ATTR(COL_LCYAN, COL_BLACK));
    print_str(current_user); print_str("@"); print_str(system_hostname); putchar('\n');

    /*  Renk eridini ve eski hata mesajlarn tamamen kaldrdk  */
    putchar('\n');

    /*  Bytlm Logoyu iz  */
    fetch_draw_png();
}
/*  Sound helpers  */
void play_sound(uint32_t nFrequence) {
    uint32_t Div = 1193180 / nFrequence;
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)(Div));
    outb(0x42, (uint8_t)(Div >> 8));
    uint8_t tmp = inb(0x61);
    if (tmp != (tmp | 3)) outb(0x61, tmp | 3);
}
void nosound(void) { uint8_t tmp = inb(0x61) & 0xFC; outb(0x61, tmp); }

const char* morse_table[] = {
    ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---",
    "-.-", ".-..", "--", "-.", "---", ".--.", "--.-", ".-.", "...", "-",
    "..-", "...-", ".--", "-..-", "-.--", "--..",
    "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----."
};

void cmd_morse(const char *args) {
    if (!args[0]) { print_str("Usage: morse <message>\n"); return; }
    print_str("Morse Code: ");
    for (int i=0; args[i]; i++) {
        char c = args[i];
        const char *code = "";
        if (c>='a' && c<='z') code = morse_table[c-'a'];
        else if (c>='A' && c<='Z') code = morse_table[c-'A'];
        else if (c>='0' && c<='9') code = morse_table[c-'0'+26];
        else if (c==' ') { print_str("  "); continue; }
        else continue;
        print_str(code); print_str(" ");
        for (int j=0; code[j]; j++) {
            play_sound(800);
            int dur = (code[j]=='.') ? 80000 : 240000;
            for (volatile int d=0; d<dur; d++);
            nosound();
            for (volatile int d=0; d<80000; d++);
        }
    }
    putchar('\n');
}

void cmd_beep(const char *args) {
    (void)args;
    print_str("Beep Mode: SPACE=beep, C=exit\n");
    uint8_t last=0;
    while(1) {
        uint8_t sc = inb(0x60);
        if (sc != last) {
            if (sc == 0x39) play_sound(750);
            else if (sc == 0xB9) nosound();
            else if (sc == 0x2E) { nosound(); print_str("\nExiting beep mode.\n"); break; }
            last = sc;
        }
        for (volatile int i=0; i<10000; i++);
    }
}

/*  Command table  */
command_t command_table[] = {
    /* filesystem */
    {"ls",       cmd_ls,       "List directory contents"},
    {"cd",       cmd_cd,       "Change directory"},
    {"pwd",      cmd_pwd,      "Print working directory"},
    {"mkdir",    cmd_mkdir,    "Create directory"},
    {"rmdir",    cmd_rmdir,    "Remove empty directory"},
    {"touch",    cmd_touch,    "Create empty file"},
    {"rm",       cmd_rm,       "Remove file"},
    {"cp",       cmd_cp,       "Copy file"},
    {"mv",       cmd_mv,       "Move/rename file"},
    {"cat",      cmd_cat,      "Print file contents"},
    {"write",    cmd_write,    "Write text to file"},
    {"stat",     cmd_stat,     "Show file info"},
    {"edit",     cmd_edit,     "Full-screen text editor (TTY, ^S=save ^Q=quit)"},
    /* hardware */
    {"lsdev",    cmd_lsdev,    "List all devices (PCI + fixed)"},
    {"lspci",    cmd_lspci,    "List PCI devices"},
    {"lsusb",    cmd_lsusb,    "List USB controllers"},
    {"lssata",   cmd_lssata,   "List SATA/ATA drives"},
    {"cpuinfo",  cmd_cpuinfo,  "CPU information (CPUID)"},
    {"meminfo",  cmd_meminfo,  "Memory and ES1 info"},
    {"partinfo", cmd_partinfo, "Drive/partition info"},
    /* system */
    {"help",     cmd_help,     "Show command list"},
    {"man",      cmd_man,      "Show manual page"},
    {"date",     cmd_date,     "Show RTC date/time (CMOS)"},
    {"uptime",   cmd_uptime,   "System uptime (approximate) + PIT counter"},
    {"uname",    cmd_uname,    "Kernel info"},
    {"free",     cmd_free,     "Memory info"},
    {"fetch",    cmd_fetch,    "System info + PNG image from /initramfs/etc/fetchpng/"},
    {"reboot",   cmd_reboot,   "Reboot (PS/2 controller reset)"},
    {"shutdown", cmd_shutdown, "Power off (ACPI S5)"},
    {"clear",    cmd_clear,    "Clear terminal"},
    {"cls",      cmd_clear,    "Clear terminal"},
    {"history",  cmd_history,  "Show command history"},
    {"echo",     cmd_echo,     "Print arguments"},
    {"exit",     cmd_exit,     "Halt system"},
    {"ver",      cmd_ver,      "Version string"},
    {"sync",     cmd_sync,     "Sync filesystems"},
    {"brun",     cmd_brun,     "Run .brun binary"},
    {"beep",     cmd_beep,     "PC speaker beep mode"},
    {"morse",    cmd_morse,    "Morse code via PC speaker"},
};

int command_count = 38;
