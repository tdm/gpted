#define _LARGEFILE64_SOURCE

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mount.h>

#include "gpt.h"

#include "util.h"
#include "crc32.h"

#define DEFAULT_LBSIZE  512
#define MIN_LBSIZE      512
#define MAX_LBSIZE      4096

#if defined(ANDROID) && defined(QCOM)
#define IS_PART_RO(gpt,n) ((n) <= (gpt->pad_idx))
#else
#define IS_PART_RO(gpt,n) (0)
#endif

static const byte gpt_header_signature[8] = {
    0x45, 0x46, 0x49, 0x20, 0x50, 0x41, 0x52, 0x54
};

static void u16_to_ascii(const byte *u, char *s)
{
    uint32_t n;
    for (n = 0; n < 72/2; ++n) {
        s[n] = u[n*2];
    }
    s[72/2] = '\0';
}

static void guid_to_ascii(const byte *guid, char *s)
{
    uint32_t p1;
    uint16_t p2;
    uint16_t p3;
    unsigned char p4[8];

    memcpy(&p1, guid + 0, 4);
    memcpy(&p2, guid + 4, 2);
    memcpy(&p3, guid + 6, 2);
    memcpy(p4, guid + 8, 8);

    sprintf(s, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            p1, p2, p3, p4[0], p4[1],
            p4[2], p4[3], p4[4], p4[5], p4[6], p4[7]);
}

static void gpt_header_show(const char *msg, const struct gpt_header *header)
{
    char guidstr[80];

    guid_to_ascii(header->disk_guid, guidstr);

    printf("%s:"
           "  size=%lu\n"
           "  current_lba=%llu\n"
           "  backup_lba=%llu\n"
           "  first_usable_lba=%llu\n"
           "  last_usable_lba=%llu\n"
           "  guid=%s\n"
           "  ptbl_lba=%llu\n"
           "  ptbl_count=%lu\n"
           "  ptbl_entry_size=%lu\n",
           msg,
           (unsigned long)header->size,
           (unsigned long long)header->current_lba,
           (unsigned long long)header->backup_lba,
           (unsigned long long)header->first_usable_lba,
           (unsigned long long)header->last_usable_lba,
           guidstr,
           (unsigned long long)header->ptbl_lba,
           (unsigned long)header->ptbl_count,
           (unsigned long)header->ptbl_entry_size);
}

static void gpt_part_show(uint32_t idx, const struct gpt_partition *part)
{
    char name[72+1];
    uint64_t start, end, size;

    u16_to_ascii(part->name, name);
    start = part->first_lba;
    end = part->last_lba;
    size = end - start + 1;
    printf("  p%-2u: [%8llu..%8llu] size=%8llu name=%s\n",
           idx,
           (unsigned long long)start,
           (unsigned long long)end,
           (unsigned long long)size,
           name);
}

static int gpt_header_is_valid(struct gpt_header *header, uint32_t lbsize)
{
    uint32_t read_crc, calc_crc;

    if (memcmp(header->signature, gpt_header_signature, sizeof(gpt_header_signature)) != 0) {
        return -1;
    }
    if (header->size < GPT_HDR_SIZE || header->size > lbsize) {
        return -1;
    }
    if (header->revision != 0x00010000) {
        return -1;
    }
    read_crc = header->crc;
    header->crc = 0;
    calc_crc = crc32(0, header, header->size);
    if (read_crc != calc_crc) {
        return -1;
    }

    if (header->ptbl_count < GPT_MIN_PARTITIONS || header->ptbl_count > GPT_MAX_PARTITIONS) {
        return -1;
    }

    if (header->ptbl_entry_size < GPT_PART_SIZE || header->ptbl_entry_size > MAX_LBSIZE) {
        return -1;
    }

    return 0;
}

int gpt_open(struct gpt *gpt, const char *dev)
{
    int rc;
    off64_t off;
    byte buf[MAX_LBSIZE];
    uint32_t calc_crc;
    uint64_t next_lba;
    uint32_t n;
    struct stat st;
    int primary_header_valid = 0;
    int backup_header_valid = 0;

    memset(gpt, 0, sizeof(struct gpt));

    gpt->fd = open(dev, O_RDWR);
    if (gpt->fd < 0) {
        perror("open");
        return -1;
    }

    rc = fstat(gpt->fd, &st);
    if (rc != 0) {
        perror("fstat");
        return -1;
    }

    gpt->lbsize = DEFAULT_LBSIZE;
    if (S_ISBLK(st.st_mode)) {
        unsigned long blklen;
        /* XXX: Linux LB size is always 512? */
        if (ioctl(gpt->fd, BLKGETSIZE, &blklen, sizeof(blklen)) == 0) {
            gpt->lblen = blklen;
        }
    }

    off = lseek64(gpt->fd, 1*gpt->lbsize, SEEK_SET);
    if (off < 0) {
        perror("lseek");
        close(gpt->fd);
        return -1;
    }
    rc = read(gpt->fd, buf, gpt->lbsize);
    if (rc != (ssize_t)gpt->lbsize) {
        fprintf(stderr, "bad read\n");
        close(gpt->fd);
        return -1;
    }
    memcpy(&gpt->header, buf, sizeof(struct gpt_header));
    if (gpt_header_is_valid(&gpt->header, gpt->lbsize) != 0) {
        fprintf(stderr, "bad gpt header\n");
        close(gpt->fd);
        return -1;
    }
#if defined(ANDROID) && defined(QCOM)
    if (gpt->header.current_lba != 1 ||
            gpt->header.backup_lba != gpt->lblen - 1 ||
            gpt->header.first_usable_lba != 34 ||
            gpt->header.last_usable_lba <= gpt->header.first_usable_lba ||
            gpt->header.last_usable_lba >= gpt->lblen - 1 ||
            gpt->header.ptbl_lba != 2 ||
            gpt->header.ptbl_count < GPT_MIN_PARTITIONS ||
            gpt->header.ptbl_count > GPT_MAX_PARTITIONS ||
            gpt->header.ptbl_entry_size > gpt->lbsize) {
        fprintf(stderr, "W: bad primary gpt\n");
        goto primary_header_out;
    }
#else
    if (gpt->header.current_lba != 1 ||
            gpt->header.backup_lba >= gpt->lblen ||
            gpt->header.first_usable_lba < 2 ||
            gpt->header.first_usable_lba >= gpt->lblen ||
            gpt->header.last_usable_lba <= gpt->header.first_usable_lba ||
            gpt->header.last_usable_lba >= gpt->lblen ||
            gpt->header.ptbl_lba >= gpt->lblen ||
            gpt->header.ptbl_count < GPT_MIN_PARTITIONS ||
            gpt->header.ptbl_count > GPT_MAX_PARTITIONS ||
            gpt->header.ptbl_entry_size > gpt->lbsize) {
        fprintf(stderr, "W: bad primary gpt\n");
        goto primary_header_out;
    }
#endif
    primary_header_valid = 1;
primary_header_out:

    /* Validate backup GPT for block devices */
    if (S_ISBLK(st.st_mode) &&
            gpt->header.backup_lba > 2 &&
            gpt->header.backup_lba < gpt->lblen) {
        off = lseek64(gpt->fd, gpt->header.backup_lba * gpt->lbsize, SEEK_SET);
        if (off < 0) {
            fprintf(stderr, "bad backup seek\n");
            goto backup_header_out;
        }
        rc = read(gpt->fd, buf, gpt->lbsize);
        if (rc != (ssize_t)gpt->lbsize) {
            fprintf(stderr, "bad backup read\n");
            goto backup_header_out;
        }
        memcpy(&gpt->backup_header, buf, sizeof(struct gpt_header));
        if (gpt_header_is_valid(&gpt->backup_header, gpt->lbsize) != 0) {
            fprintf(stderr, "bad backup header\n");
            goto backup_header_out;
        }
#if defined(ANDROID) && defined(QCOM)
        if (gpt->backup_header.current_lba != gpt->header.backup_lba ||
                gpt->backup_header.backup_lba != 1 ||
                gpt->backup_header.first_usable_lba != gpt->header.first_usable_lba ||
                gpt->backup_header.last_usable_lba != gpt->header.last_usable_lba ||
                memcmp(gpt->backup_header.disk_guid, gpt->header.disk_guid, 16) != 0 ||
                gpt->backup_header.ptbl_lba >= gpt->lblen ||
                gpt->backup_header.ptbl_count != gpt->header.ptbl_count ||
                gpt->backup_header.ptbl_entry_size != gpt->header.ptbl_entry_size) {
            fprintf(stderr, "W: bad backup gpt\n");
            goto backup_header_out;
        }
#else
        if (gpt->backup_header.current_lba != gpt->header.backup_lba ||
                gpt->backup_header.backup_lba != 1 ||
                gpt->backup_header.first_usable_lba != gpt->header.first_usable_lba ||
                gpt->backup_header.last_usable_lba != gpt->header.last_usable_lba ||
                memcmp(gpt->backup_header.disk_guid, gpt->header.disk_guid, 16) != 0 ||
                gpt->backup_header.ptbl_lba >= gpt->lblen ||
                gpt->backup_header.ptbl_count != gpt->header.ptbl_count ||
                gpt->backup_header.ptbl_entry_size != gpt->header.ptbl_entry_size) {
            fprintf(stderr, "W: bad backup gpt\n");
            goto backup_header_out;
        }
#endif
        backup_header_valid = 1;
    }
backup_header_out:

    next_lba = gpt->header.first_usable_lba;

    if (primary_header_valid) {
        off = lseek64(gpt->fd, gpt->header.ptbl_lba * gpt->lbsize, SEEK_SET);
        if (off < 0) {
            perror("lseek\n");
            close(gpt->fd);
            return -1;
        }

        calc_crc = 0;
        for (n = 0; n < gpt->header.ptbl_count; ++n) {
            gpt->partitions[n] = (struct gpt_partition *)malloc(sizeof(struct gpt_partition));
            memset(gpt->partitions[n], 0, sizeof(struct gpt_partition));
            rc = read(gpt->fd, gpt->partitions[n], gpt->header.ptbl_entry_size);
            if (rc < 0 || (uint32_t)rc != gpt->header.ptbl_entry_size) {
                fprintf(stderr, "failed to read partition entry %u\n", n);
                close(gpt->fd);
                return -1;
            }
            calc_crc = crc32(calc_crc, gpt->partitions[n], gpt->header.ptbl_entry_size);
            if (gpt->partitions[n]->first_lba == 0 && gpt->partitions[n]->last_lba == 0) {
                continue;
            }
            if (gpt->partitions[n]->first_lba < next_lba ||
                    gpt->partitions[n]->last_lba < gpt->partitions[n]->first_lba ||
                    gpt->partitions[n]->last_lba > gpt->header.last_usable_lba) {
                fprintf(stderr, "bad lba in partition entry %u\n", n);
                close(gpt->fd);
                return -1;
            }
            gpt->last_used_idx = n;
        }

        if (gpt->header.ptbl_crc != calc_crc) {
            fprintf(stderr, "bad ptbl crc\n");
            close(gpt->fd);
            return -1;
        }
    }

    if (backup_header_valid) {
        int warned = 0;
        off = lseek64(gpt->fd, gpt->backup_header.ptbl_lba * gpt->lbsize, SEEK_SET);
        if (off < 0) {
            perror("lseek\n");
            close(gpt->fd);
            return -1;
        }
        calc_crc = 0;
        for (n = 0; n < gpt->backup_header.ptbl_count; ++n) {
            struct gpt_partition backup_part;
            rc = read(gpt->fd, &backup_part, gpt->backup_header.ptbl_entry_size);
            if (rc < 0 || (uint32_t)rc != gpt->backup_header.ptbl_entry_size) {
                fprintf(stderr, "failed to read backup partition entry %u\n", n);
                close(gpt->fd);
                return -1;
            }
            if (memcmp(gpt->partitions[n], &backup_part, sizeof(struct gpt_partition)) != 0) {
                if (!warned) {
                    fprintf(stderr, "mismatched backup partition entry %u\n", n);
                    warned = 1;
                }
            }
        }
    }

#if defined(ANDROID) && defined(QCOM)
    gpt->pad_idx = gpt_part_find(gpt, "pad");
    if (gpt->pad_idx == GPT_PART_INVALID) {
        fprintf(stderr, "no pad found\n");
        close(gpt->fd);
        return -1;
    }
#endif

    return 0;
}

int gpt_write(const struct gpt *gpt)
{
    int rc;
    off64_t off;
    struct gpt_header hdr;
    uint32_t n;

    memcpy(&hdr, &gpt->header, sizeof(hdr));

    hdr.ptbl_crc = 0;
    for (n = 0; n < gpt->header.ptbl_count; ++n) {
        hdr.ptbl_crc = crc32(hdr.ptbl_crc, gpt->partitions[n], gpt->header.ptbl_entry_size);
    }

    hdr.crc = 0;
    hdr.crc = crc32(0, &hdr, hdr.size);

    off = lseek64(gpt->fd, hdr.current_lba * gpt->lbsize, SEEK_SET);
    if (off < 0) {
        perror("lseek");
        return -1;
    }
    rc = write(gpt->fd, &hdr, hdr.size);
    if (rc < 0 || (uint32_t)rc != hdr.size) {
        fprintf(stderr, "bad primary header write\n");
        return -1;
    }

    off = lseek64(gpt->fd, hdr.ptbl_lba * gpt->lbsize, SEEK_SET);
    if (off < 0) {
        perror("lseek\n");
        return -1;
    }

    for (n = 0; n < hdr.ptbl_count; ++n) {
        rc = write(gpt->fd, gpt->partitions[n], hdr.ptbl_entry_size);
        if (rc < 0 || (uint32_t)rc != hdr.ptbl_entry_size) {
            fprintf(stderr, "bad primary partition write\n");
            return -1;
        }
    }

    hdr.current_lba = gpt->header.backup_lba;
    hdr.backup_lba = 1;
    hdr.ptbl_lba = gpt->lblen -
            ROUNDUP(hdr.ptbl_count * hdr.ptbl_entry_size, gpt->lbsize) / gpt->lbsize - 1;

    hdr.crc = 0;
    hdr.crc = crc32(0, &hdr, hdr.size);

    off = lseek64(gpt->fd, hdr.current_lba * gpt->lbsize, SEEK_SET);
    if (off < 0) {
        perror("lseek");
        return -1;
    }
    rc = write(gpt->fd, &hdr, hdr.size);
    if (rc < 0 || (uint32_t)rc != hdr.size) {
        fprintf(stderr, "bad backup header write\n");
        return -1;
    }

    off = lseek64(gpt->fd, hdr.ptbl_lba * gpt->lbsize, SEEK_SET);
    if (off < 0) {
        perror("lseek\n");
        return -1;
    }

    for (n = 0; n < hdr.ptbl_count; ++n) {
        rc = write(gpt->fd, gpt->partitions[n], hdr.ptbl_entry_size);
        if (rc < 0 || (uint32_t)rc != hdr.ptbl_entry_size) {
            fprintf(stderr, "bad backup partition write\n");
            return -1;
        }
    }

    return 0;
}

int gpt_close(struct gpt *gpt)
{
    int rc;

    rc = close(gpt->fd);
    gpt->fd = -1;

    return rc;
}

void gpt_show(const struct gpt *gpt)
{
    uint32_t n;

    gpt_header_show("Primary GPT", &gpt->header);

    if (gpt->backup_header.size != 0) {
        gpt_header_show("Backup GPT", &gpt->backup_header);
    }

    printf("Partition table: count=%u\n", (unsigned int)gpt->last_used_idx);
    for (n = 0; n < gpt->header.ptbl_count; ++n) {
        if (gpt->partitions[n]->first_lba == 0 && gpt->partitions[n]->last_lba == 0) {
            continue;
        }
        gpt_part_show(n, gpt->partitions[n]);
    }
}

uint32_t gpt_part_find(const struct gpt *gpt, const char *name)
{
    uint32_t n;
    char curname[72/2+1];

    for (n = 0; n < gpt->header.ptbl_count; ++n) {
        u16_to_ascii(gpt->partitions[n]->name, curname);
        if (!strcmp(name, curname)) {
            return n;
        }
    }

    return GPT_PART_INVALID;
}

int gpt_part_name(const struct gpt *gpt, uint32_t idx, char *name)
{
    u16_to_ascii(gpt->partitions[idx]->name, name);
    return 0;
}

uint64_t gpt_part_size(const struct gpt *gpt, uint32_t idx)
{
    uint64_t lbsize;

    lbsize = gpt->partitions[idx]->last_lba - gpt->partitions[idx]->first_lba + 1;
    return (lbsize * gpt->lbsize);
}

int gpt_part_add(struct gpt *gpt, uint32_t idx, const struct gpt_partition *part, int follow)
{
    uint64_t lba_min, lba_max;
    uint32_t n;

    if (IS_PART_RO(gpt, idx) || idx > gpt->last_used_idx+1 ||
            gpt->last_used_idx >= gpt->header.ptbl_count) {
        return -1;
    }

    lba_min = (idx == 0 ?
               gpt->header.first_usable_lba :
               gpt->partitions[idx-1]->last_lba + 1);
    lba_max = (idx == gpt->header.ptbl_count ?
               gpt->header.last_usable_lba :
               gpt->partitions[idx]->first_lba - 1);

    if (part->first_lba < lba_min || part->last_lba > lba_max) {
        return -1;
    }

    for (n = gpt->header.ptbl_count; n > idx; --n) {
        gpt->partitions[n] = gpt->partitions[n-1];
    }
    gpt->partitions[n] = (struct gpt_partition *)malloc(sizeof(struct gpt_partition));
    memcpy(gpt->partitions[n], part, sizeof(struct gpt_partition));

    gpt->last_used_idx++;

    return 0;
}

int gpt_part_del(struct gpt *gpt, uint32_t idx, int follow)
{
    uint32_t n;
    uint64_t len, delta;

    if (IS_PART_RO(gpt, idx) || idx > gpt->last_used_idx) {
        return -1;
    }

    len = gpt->partitions[idx]->last_lba - gpt->partitions[idx]->first_lba + 1;
    delta = 0 - len;

    for (n = idx; n < gpt->last_used_idx; ++n) {
        gpt->partitions[n] = gpt->partitions[n+1];
        if (follow) {
            gpt->partitions[n]->first_lba += delta;
            gpt->partitions[n]->last_lba += delta;
        }
    }

    gpt->last_used_idx--;

    return 0;
}

int gpt_part_move(struct gpt *gpt, uint32_t idx, uint64_t lba, int follow)
{
    uint32_t n;
    int64_t lbdelta;

    if (IS_PART_RO(gpt, idx) || idx > gpt->last_used_idx) {
        return -1;
    }

    lbdelta = (int64_t)lba - (int64_t)gpt->partitions[idx]->first_lba;
    if (lbdelta == 0) {
        return 0;
    }

    if (lbdelta < 0) {
        /*
         * Figure out the minimum lba for the partition.  For now, we will
         * just use the end of the previous partition.  Later, we could
         * try to do something like "follow" in reverse.
         */
        uint64_t lba_min = gpt->partitions[idx-1]->last_lba + 1;
        if (lba < lba_min) {
            return -1;
        }
    }
    else {
        /*
         * Figure out the maximum lba delta:
         *  - For the last partition, limit is last usable LBA less end LBA.
         *  - If follow is set, limit is same as last partition.
         *  - If follow is unset, limit is start of next partition less this partition size.
         */
        uint64_t lbamaxdelta;
        if (idx == gpt->last_used_idx || follow) {
            uint32_t lui = gpt->last_used_idx;
            lbamaxdelta = gpt->header.last_usable_lba - gpt->partitions[lui]->last_lba;
        }
        else {
            lbamaxdelta = gpt->partitions[idx+1]->first_lba - gpt->partitions[idx]->last_lba - 1;
        }
        if (lbdelta > (int64_t)lbamaxdelta) {
            return -1;
        }
    }

    gpt->partitions[idx]->first_lba += lbdelta;
    gpt->partitions[idx]->last_lba += lbdelta;

    if (follow) {
        for (n = idx+1; n <= gpt->header.ptbl_count; ++n) {
            gpt->partitions[n]->first_lba += lbdelta;
            gpt->partitions[n]->last_lba += lbdelta;
        }
    }

    return 0;
}

int gpt_part_resize(struct gpt *gpt, uint32_t idx, uint64_t size, int follow)
{
    uint32_t n;
    int64_t lbdelta;

    if (IS_PART_RO(gpt, idx) || idx > gpt->last_used_idx) {
        return -1;
    }
    if (size & (gpt->lbsize - 1)) {
        return -1;
    }
    lbdelta = ((int64_t)size - (int64_t)gpt_part_size(gpt, idx)) / gpt->lbsize;
    if (lbdelta == 0) {
        return 0;
    }

    if (lbdelta > 0) {
        /*
         * Figure out how much the partition may expand:
         *  - For the last partition, limit is last usable LBA.
         *  - If follow is set, limit is same as last partition.
         *  - If follow is unset, limit is start of next partition.
         */
        uint64_t lbamaxdelta;
        if (idx == gpt->last_used_idx || follow) {
            uint32_t lui = gpt->last_used_idx;
            lbamaxdelta = gpt->header.last_usable_lba - gpt->partitions[lui]->last_lba;
        }
        else {
            lbamaxdelta  = gpt->partitions[idx+1]->first_lba - gpt->partitions[idx]->last_lba - 1;
        }
        if (lbdelta > (int64_t)lbamaxdelta) {
            return -1;
        }
    }

    gpt->partitions[idx]->last_lba += lbdelta;

    if (follow) {
        for (n = idx+1; n <= gpt->last_used_idx; ++n) {
            gpt->partitions[n]->first_lba += lbdelta;
            gpt->partitions[n]->last_lba += lbdelta;
        }
    }

    return 0;
}

int gpt_part_save(struct gpt *gpt, uint32_t idx, const char *filename)
{
    uint64_t remain;
    int fd;
    char buf[4096];

    if (lseek64(gpt->fd,
            gpt->partitions[idx]->first_lba * gpt->lbsize,
            SEEK_SET) < 0) {
        return -1;
    }
    remain = gpt_part_size(gpt, idx);

    fd = open(filename, O_WRONLY | O_CREAT, 0666);
    if (fd < 0) {
        return -1;
    }

    while (remain > 0) {
        size_t toread;
        ssize_t nread, nwritten;
        toread = (remain > sizeof(buf) ? sizeof(buf) : remain);
        nread = read(gpt->fd, buf, toread);
        if (nread <= 0) {
            goto out_err;
        }
        nwritten = write(fd, buf, nread);
        if (nwritten != nread) {
            goto out_err;
        }
        remain -= nread;
    }
    close(fd);

    return 0;

out_err:
    close(fd);
    return -1;
}

int gpt_part_load(struct gpt *gpt, uint32_t idx, const char *filename)
{
    uint64_t remain;
    int fd;
    struct stat st;
    char buf[4096];

    if (lseek64(gpt->fd,
            gpt->partitions[idx]->first_lba * gpt->lbsize,
            SEEK_SET) < 0) {
        return -1;
    }
    remain = gpt_part_size(gpt, idx);

    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    if (fstat(fd, &st) != 0) {
        perror("fstat");
        goto out_err;
    }
    if ((uint64_t)st.st_size != gpt_part_size(gpt, idx)) {
        fprintf(stderr, "E: file %s has incorrect size\n", filename);
        goto out_err;
    }

    while (remain > 0) {
        size_t toread;
        ssize_t nread, nwritten;
        toread = (remain > sizeof(buf) ? sizeof(buf) : remain);
        nread = read(fd, buf, toread);
        if (nread <= 0) {
            goto out_err;
        }
        nwritten = write(gpt->fd, buf, nread);
        if (nwritten != nread) {
            goto out_err;
        }
        remain -= nread;
    }
    close(fd);

    return 0;

out_err:
    close(fd);
    return -1;
}
