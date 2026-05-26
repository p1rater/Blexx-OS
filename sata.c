/*
 * sata.c – ATA/SATA PIO driver for BlexOS x86_64
 * Copyright (C) 2026 Blex – BOSL License
 *
 * Supports legacy ATA PIO (28-bit LBA) on primary and secondary channels.
 * Also detects AHCI-class SATA controllers via PCI class 0x01/0x06.
 *
 * Usage:
 *   ata_init()                         – probe all drives, fill ata_drives[]
 *   ata_read_sectors(drive,lba,n,buf)  – read n sectors into buf
 *   ata_write_sectors(drive,lba,n,buf) – write n sectors from buf
 */

#include "kernel_utils.h"
#include <stdint.h>

ata_drive_t ata_drives[ATA_MAX_DRIVES];

/* ── Low-level helpers ───────────────────────────────── */
static void ata_wait_busy(int base) {
    /* poll until BSY clears; up to ~30 ms */
    for (int i = 0; i < 0xFFFFF; i++) {
        if (!(inb((uint16_t)(base + ATA_REG_STATUS)) & ATA_STATUS_BSY)) return;
    }
}

static void ata_400ns(int base) {
    /* read alternate status 4 times ≈ 400 ns */
    inb((uint16_t)(base + ATA_REG_STATUS));
    inb((uint16_t)(base + ATA_REG_STATUS));
    inb((uint16_t)(base + ATA_REG_STATUS));
    inb((uint16_t)(base + ATA_REG_STATUS));
}

/* Soft-reset channel, returns 0 on success */
static int ata_soft_reset(int base, int ctrl) {
    outb((uint16_t)ctrl, 0x04);   /* SRST */
    ata_400ns(base);
    outb((uint16_t)ctrl, 0x00);   /* clear SRST */
    ata_400ns(base);
    ata_wait_busy(base);
    return 0;
}

/* ── IDENTIFY ────────────────────────────────────────── */
static int ata_identify(int base, int ctrl, int slave, ata_drive_t *drv) {
    /* Select drive */
    outb((uint16_t)(base + ATA_REG_DRIVE), (uint8_t)(0xA0 | (slave << 4)));
    ata_400ns(base);

    /* Zero LBA registers */
    outb((uint16_t)(base + ATA_REG_SECCOUNT), 0);
    outb((uint16_t)(base + ATA_REG_LBA_LO),   0);
    outb((uint16_t)(base + ATA_REG_LBA_MID),  0);
    outb((uint16_t)(base + ATA_REG_LBA_HI),   0);

    outb((uint16_t)(base + ATA_REG_COMMAND), ATA_CMD_IDENTIFY);
    ata_400ns(base);

    /* If status == 0, no drive */
    if (inb((uint16_t)(base + ATA_REG_STATUS)) == 0) return 0;

    ata_wait_busy(base);

    /* Check for ATAPI (non-ATA) */
    if (inb((uint16_t)(base + ATA_REG_LBA_MID)) ||
        inb((uint16_t)(base + ATA_REG_LBA_HI)))
        return 0;   /* not plain ATA */

    /* Wait for DRQ or ERR */
    for (int i = 0; i < 0xFFFFF; i++) {
        uint8_t st = inb((uint16_t)(base + ATA_REG_STATUS));
        if (st & ATA_STATUS_ERR) return 0;
        if (st & ATA_STATUS_DRQ) break;
    }

    /* Read 256 words of IDENTIFY data */
    uint16_t id[256];
    for (int i = 0; i < 256; i++)
        id[i] = inb((uint16_t)(base + ATA_REG_DATA)) |
                ((uint16_t)inb((uint16_t)(base + ATA_REG_DATA)) << 8);

    /* Total 28-bit LBA sectors (words 60-61) */
    drv->sectors = ((uint32_t)id[61] << 16) | id[60];

    /* Model string: words 27-46, byte-swapped */
    int m = 0;
    for (int w = 27; w <= 46 && m < 40; w++) {
        drv->model[m++] = (char)((id[w] >> 8) & 0xFF);
        drv->model[m++] = (char)(id[w] & 0xFF);
    }
    drv->model[40] = '\0';
    /* trim trailing spaces */
    for (int i = 39; i >= 0 && drv->model[i] == ' '; i--)
        drv->model[i] = '\0';

    (void)ctrl;
    drv->present = 1;
    drv->base    = base;
    drv->ctrl    = ctrl;
    drv->slave   = slave;
    return 1;
}

/* ── Check for AHCI SATA controller on PCI ───────────── */
static void ata_detect_sata(void) {
    for (int bus = 0; bus < 8; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            uint32_t class_info = pci_read_config((uint8_t)bus, (uint8_t)slot, 0, 0x08);
            uint8_t class_code = (class_info >> 24) & 0xFF;
            uint8_t subclass   = (class_info >> 16) & 0xFF;
            /* 0x01 = Mass Storage, 0x06 = SATA, 0x01 = IDE */
            if (class_code == 0x01 && (subclass == 0x06 || subclass == 0x01)) {
                /* Mark any present ATA drive as SATA-class */
                for (int d = 0; d < ATA_MAX_DRIVES; d++)
                    if (ata_drives[d].present)
                        ata_drives[d].is_sata = 1;
                return;
            }
        }
    }
}

/* ── Public: initialise all drives ───────────────────── */
void ata_init(void) {
    /* Zero all entries */
    for (int i = 0; i < ATA_MAX_DRIVES; i++) {
        ata_drives[i].present = 0;
        ata_drives[i].is_sata = 0;
        ata_drives[i].sectors = 0;
        ata_drives[i].model[0] = '\0';
    }

    /* Reset both channels */
    ata_soft_reset(ATA_PRIMARY_BASE,   ATA_PRIMARY_CTRL);
    ata_soft_reset(ATA_SECONDARY_BASE, ATA_SECONDARY_CTRL);

    int idx = 0;
    int channels[2][2] = {
        { ATA_PRIMARY_BASE,   ATA_PRIMARY_CTRL   },
        { ATA_SECONDARY_BASE, ATA_SECONDARY_CTRL }
    };
    for (int ch = 0; ch < 2 && idx < ATA_MAX_DRIVES; ch++) {
        for (int sl = 0; sl < 2 && idx < ATA_MAX_DRIVES; sl++) {
            ata_drives[idx].base  = channels[ch][0];
            ata_drives[idx].ctrl  = channels[ch][1];
            ata_drives[idx].slave = sl;
            if (ata_identify(channels[ch][0], channels[ch][1],
                             sl, &ata_drives[idx]))
                idx++;
            else
                idx++;   /* advance slot even if empty */
        }
    }

    ata_detect_sata();
}

/* ── LBA28 PIO read ──────────────────────────────────── */
int ata_read_sectors(int drive, uint32_t lba, uint8_t count, uint16_t *buf) {
    if (drive < 0 || drive >= ATA_MAX_DRIVES) return -1;
    ata_drive_t *drv = &ata_drives[drive];
    if (!drv->present) return -2;
    if (lba + count > drv->sectors) return -3;

    int base = drv->base;
    int slave = drv->slave;

    ata_wait_busy(base);

    outb((uint16_t)(base + ATA_REG_DRIVE),
         (uint8_t)(0xE0 | (slave << 4) | ((lba >> 24) & 0x0F)));
    outb((uint16_t)(base + ATA_REG_SECCOUNT), count);
    outb((uint16_t)(base + ATA_REG_LBA_LO),  (uint8_t)(lba));
    outb((uint16_t)(base + ATA_REG_LBA_MID), (uint8_t)(lba >> 8));
    outb((uint16_t)(base + ATA_REG_LBA_HI),  (uint8_t)(lba >> 16));
    outb((uint16_t)(base + ATA_REG_COMMAND), ATA_CMD_READ_PIO);

    for (int s = 0; s < count; s++) {
        ata_wait_busy(base);
        /* wait DRQ */
        for (int i = 0; i < 0xFFFFF; i++) {
            uint8_t st = inb((uint16_t)(base + ATA_REG_STATUS));
            if (st & ATA_STATUS_ERR) return -4;
            if (st & ATA_STATUS_DRQ) break;
        }
        /* read 256 words */
        for (int w = 0; w < 256; w++) {
            uint8_t lo = inb((uint16_t)(base + ATA_REG_DATA));
            uint8_t hi = inb((uint16_t)(base + ATA_REG_DATA));
            buf[s * 256 + w] = (uint16_t)lo | ((uint16_t)hi << 8);
        }
    }
    return 0;
}

/* ── LBA28 PIO write ─────────────────────────────────── */
int ata_write_sectors(int drive, uint32_t lba, uint8_t count,
                      const uint16_t *buf) {
    if (drive < 0 || drive >= ATA_MAX_DRIVES) return -1;
    ata_drive_t *drv = &ata_drives[drive];
    if (!drv->present) return -2;
    if (lba + count > drv->sectors) return -3;

    int base  = drv->base;
    int slave = drv->slave;

    ata_wait_busy(base);

    outb((uint16_t)(base + ATA_REG_DRIVE),
         (uint8_t)(0xE0 | (slave << 4) | ((lba >> 24) & 0x0F)));
    outb((uint16_t)(base + ATA_REG_SECCOUNT), count);
    outb((uint16_t)(base + ATA_REG_LBA_LO),  (uint8_t)(lba));
    outb((uint16_t)(base + ATA_REG_LBA_MID), (uint8_t)(lba >> 8));
    outb((uint16_t)(base + ATA_REG_LBA_HI),  (uint8_t)(lba >> 16));
    outb((uint16_t)(base + ATA_REG_COMMAND), ATA_CMD_WRITE_PIO);

    for (int s = 0; s < count; s++) {
        ata_wait_busy(base);
        for (int i = 0; i < 0xFFFFF; i++) {
            uint8_t st = inb((uint16_t)(base + ATA_REG_STATUS));
            if (st & ATA_STATUS_ERR) return -4;
            if (st & ATA_STATUS_DRQ) break;
        }
        for (int w = 0; w < 256; w++) {
            uint16_t word = buf[s * 256 + w];
            outb((uint16_t)(base + ATA_REG_DATA), (uint8_t)(word & 0xFF));
            outb((uint16_t)(base + ATA_REG_DATA), (uint8_t)(word >> 8));
        }
        /* Cache flush */
        outb((uint16_t)(base + ATA_REG_COMMAND), 0xE7);
        ata_wait_busy(base);
    }
    return 0;
}
