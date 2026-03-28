#ifndef KERNEL_UTILS_H
#define KERNEL_UTILS_H

typedef struct {
    char name[32];
    char content[128];
    int size;
} vfs_node_t;

void print_str(const char* str);
void clear_screen();
void putchar(char c);
void print_int(int n);

#endif
