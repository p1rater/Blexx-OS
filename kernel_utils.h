/*
 * kernel_utils.h – BlexOS x86_64
 * Copyright (C) 2026 Blex – BOSL License
 *
 * Shared types, constants and function prototypes for:
 *   - ES1 (Embed File System 1) in-memory filesystem
 *   - SATA/ATA PIO driver
 *   - BRUN executable format
 *   - VGA colour constants (kept for WM compatibility)
 */

#ifndef KERNEL_UTILS_H
#define KERNEL_UTILS_H

#include <stdint.h>

/* =========================================================
 * ES1 – Embed File System 1
 *
 * A flat, in-memory filesystem designed for embedded kernels.
 * Every node is a fixed-size record; no dynamic allocation needed.
 *
 * Layout:
 *   es1_superblock_t   (64 bytes, at index 0 of the node array)
 *   es1_node_t[ES1_MAX_NODES]  (512 bytes each)
 *
 * Node flags:
 *   ES1_FL_USED  – slot is in use
 *   ES1_FL_DIR   – node is a directory
 *   ES1_FL_EXEC  – executable bit
 *
 * Paths are stored as full absolute paths (e.g. "/home/user/file.txt").
 * Content is stored inline for files ≤ ES1_INLINE_SIZE bytes.
 * Larger content is stored in es1_extent_t blocks (up to ES1_MAX_EXTENTS).
 * ========================================================= */

#define ES1_MAGIC        0x31534500u   /* "ES1\0" little-endian */
#define ES1_VERSION      1
#define ES1_MAX_NODES    32
#define ES1_NAME_LEN     64
#define ES1_INLINE_SIZE  480
#define ES1_MAX_EXTENTS  8
#define ES1_EXTENT_SIZE  4096

#define ES1_FL_USED  (1u << 0)
#define ES1_FL_DIR   (1u << 1)
#define ES1_FL_EXEC  (1u << 2)

typedef struct {
    uint32_t magic;          /* ES1_MAGIC                  */
    uint32_t version;        /* ES1_VERSION                */
    uint32_t node_count;     /* total node slots           */
    uint32_t used_nodes;     /* nodes currently allocated  */
    uint32_t total_bytes;    /* sum of all file sizes      */
    char     label[32];      /* volume label               */
    uint8_t  _pad[12];
} __attribute__((packed)) es1_superblock_t;   /* 64 bytes */

typedef struct {
    uint32_t flags;                    /* ES1_FL_* bitmask          */
    char     path[ES1_NAME_LEN];       /* full absolute path        */
    char     owner[16];                /* owning username           */
    char     perms[10];                /* rwxr-xr-x style           */
    uint32_t size;                     /* byte count of content     */
    uint32_t mtime;                    /* modification tick         */
    char     inline_data[ES1_INLINE_SIZE]; /* inline content        */
    uint8_t  _pad[2];
} __attribute__((packed)) es1_node_t;         /* 608 bytes */

/* Superblock lives as a special struct; nodes are the array below */
extern es1_superblock_t es1_sb;
extern es1_node_t       es1_nodes[ES1_MAX_NODES];

/* ES1 API */
es1_node_t *es1_find(const char *path);
es1_node_t *es1_alloc(const char *path, uint32_t flags);
int         es1_free(const char *path);
int         es1_write(const char *path, const char *data, uint32_t len);
int         es1_read(const char *path, char *buf, uint32_t bufsz);
void        es1_init(void);

/* Alias old VFS type so command_logic.c still compiles */
typedef es1_node_t vfs_node_t;
#define ram_disk es1_nodes
/* map old field names to ES1 names */
// #define name         path
#define content      inline_data
#define is_directory flags   /* checked via ES1_FL_DIR bit */

/* =========================================================
 * SATA / ATA PIO driver
 *
 * Supports up to 4 drives (primary master/slave,
 * secondary master/slave) via legacy ATA I/O ports.
 * ========================================================= */

#define ATA_PRIMARY_BASE    0x1F0
#define ATA_PRIMARY_CTRL    0x3F6
#define ATA_SECONDARY_BASE  0x170
#define ATA_SECONDARY_CTRL  0x376

#define ATA_REG_DATA        0x00
#define ATA_REG_ERROR       0x01
#define ATA_REG_FEATURES    0x01
#define ATA_REG_SECCOUNT    0x02
#define ATA_REG_LBA_LO      0x03
#define ATA_REG_LBA_MID     0x04
#define ATA_REG_LBA_HI      0x05
#define ATA_REG_DRIVE       0x06
#define ATA_REG_STATUS      0x07
#define ATA_REG_COMMAND     0x07

#define ATA_STATUS_BSY      0x80
#define ATA_STATUS_DRQ      0x08
#define ATA_STATUS_ERR      0x01

#define ATA_CMD_IDENTIFY    0xEC
#define ATA_CMD_READ_PIO    0x20
#define ATA_CMD_WRITE_PIO   0x30

typedef struct {
    int      present;       /* 1 = drive found           */
    int      base;          /* I/O base port             */
    int      ctrl;          /* control port              */
    int      slave;         /* 0=master, 1=slave         */
    uint32_t sectors;       /* total 512-byte sectors    */
    char     model[41];     /* model string from IDENTIFY*/
    int      is_sata;       /* 1 if SATA (via AHCI PCI) */
} ata_drive_t;

#define ATA_MAX_DRIVES 4
extern ata_drive_t ata_drives[ATA_MAX_DRIVES];

/* SATA/ATA API */
void ata_init(void);
int  ata_read_sectors(int drive, uint32_t lba, uint8_t count, uint16_t *buf);
int  ata_write_sectors(int drive, uint32_t lba, uint8_t count, const uint16_t *buf);

/* =========================================================
 * BRUN EXECUTABLE FORMAT
 * ========================================================= */

#define BRUN_MAGIC    0x4E555242u   /* "BRUN" */
#define BRUN_VERSION  1

typedef struct {
    uint32_t  magic;
    uint8_t   version;
    uint8_t   flags;
    uint16_t  code_size;
    uint16_t  data_size;
    uint16_t  entry_off;
    char      name[16];
    uint32_t  checksum;
} __attribute__((packed)) brun_header_t;   /* 32 bytes */

/* =========================================================
 * SCREEN / I/O FUNCTIONS
 * ========================================================= */
void print_str(const char* str);
void print_str_color(const char* str, char color);
void clear_screen(void);
void putchar(char c);
void print_int(int n);
void update_cursor(int x, int y);

extern int  cursor_x;
extern int  cursor_y;
extern char current_color;

/* =========================================================
 * HARDWARE PORT I/O  (inline for x86_64)
 * ========================================================= */
static inline unsigned char inb(unsigned short port) {
    unsigned char ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline void outw(unsigned short port, unsigned short val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint32_t inl(unsigned short port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void outl(unsigned short port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline void insw(unsigned short port, void *buf, uint32_t count) {
    __asm__ volatile ("rep insw" : "+D"(buf), "+c"(count) : "d"(port) : "memory");
}
static inline void outsw(unsigned short port, const void *buf, uint32_t count) {
    __asm__ volatile ("rep outsw" : "+S"(buf), "+c"(count) : "d"(port));
}

void play_sound(uint32_t nfreq);
void nosound(void);

/* =========================================================
 * PCI helpers
 * ========================================================= */
static inline uint32_t pci_read_config(uint8_t bus, uint8_t slot,
                                        uint8_t func, uint8_t offset) {
    uint32_t addr = 0x80000000u |
                    ((uint32_t)bus  << 16) |
                    ((uint32_t)slot << 11) |
                    ((uint32_t)func <<  8) |
                    (offset & 0xFC);
    outl(0xCF8, addr);
    return inl(0xCFC);
}

/* =========================================================
 * VGA / framebuffer colour constants (WM palette)
 * ========================================================= */
#define COL_BLACK     0x0
#define COL_BLUE      0x1
#define COL_GREEN     0x2
#define COL_CYAN      0x3
#define COL_RED       0x4
#define COL_MAGENTA   0x5
#define COL_BROWN     0x6
#define COL_LGRAY     0x7
#define COL_DGRAY     0x8
#define COL_LBLUE     0x9
#define COL_LGREEN    0xA
#define COL_LCYAN     0xB
#define COL_LRED      0xC
#define COL_LMAGENTA  0xD
#define COL_YELLOW    0xE
#define COL_WHITE     0xF

#define VGA_ATTR(fg,bg)  (char)(((bg) << 4) | (fg))

#endif /* KERNEL_UTILS_H */
