#ifndef KERNEL_UTILS_H
#define KERNEL_UTILS_H

/* --- VFS YAPISI (GELİŞMİŞ) --- */
typedef struct {
    char name[32];      // Dosya veya dizin adı
    char content[512];   // Dosya içeriği (Nano için 512 byte yapıldı)
    int size;           // Dosya boyutu
    int is_directory;   // 1 ise dizin, 0 ise dosya
    char owner[16];     // Dosya sahibi (root, Blex vb.)
} vfs_node_t;

/* --- EKRAN VE I/O FONKSİYONLARI --- */
void print_str(const char* str);
void clear_screen();
void putchar(char c);
void print_int(int n);
void update_cursor(int x, int y);

/* --- DONANIM PORT I/O --- */
unsigned char inb(unsigned short port);
// outb genelde inline asm olarak kullanıldığı için burada tanımı yeterlidir

#endif