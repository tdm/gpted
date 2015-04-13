#ifndef GPT_H
#define GPT_H

#include "types.h"

#define GPT_HDR_SIZE 92
struct gpt_header
{
    byte        signature[8];
    uint32_t    revision;
    uint32_t    size;
    uint32_t    crc;
    uint32_t    reserved;
    uint64_t    current_lba;
    uint64_t    backup_lba;
    uint64_t    first_usable_lba;
    uint64_t    last_usable_lba;
    byte        disk_guid[16];
    uint64_t    ptbl_lba;
    uint32_t    ptbl_count;
    uint32_t    ptbl_entry_size;
    uint32_t    ptbl_crc;
    byte        padding[512-GPT_HDR_SIZE];
} __attribute__((packed));

#define GPT_PART_SIZE 128
struct gpt_partition
{
    byte        type_guid[16];
    byte        part_guid[16]; 
    uint64_t    first_lba;
    uint64_t    last_lba;
    byte        flags[8];
    byte        name[72];
    byte        padding[512-GPT_PART_SIZE];
} __attribute__((packed));

#if defined(ANDROID) && defined(QCOM)
#define GPT_MIN_PARTITIONS      8
#define GPT_MAX_PARTITIONS      128
#else
#define GPT_MIN_PARTITIONS      4
#define GPT_MAX_PARTITIONS      256
#endif

#define GPT_PART_INVALID        (uint32_t)(~0)

struct gpt
{
    int                         fd;
    uint32_t                    lbsize;
    uint32_t                    lblen;
    struct gpt_header           header;
    struct gpt_header           backup_header;
#if defined(ANDROID) && defined(QCOM)
    uint32_t                    pad_idx;
#endif
    uint32_t                    last_used_idx;
    struct gpt_partition*       partitions[GPT_MAX_PARTITIONS];
};

int gpt_open(struct gpt *gpt, const char *dev);
int gpt_write(const struct gpt *gpt);
int gpt_close(struct gpt *gpt);
void gpt_show(const struct gpt *gpt);

/*
 * gpt_part_find: Find a partition
 *   name: ASCII encoded name to find.
 * Returns index if found.
 * Returns GPT_PART_INVALID on error.
 */
uint32_t gpt_part_find(const struct gpt *gpt, const char *name);

#define GPT_PART_NAMELEN (72/2+1)
int gpt_part_name(const struct gpt *gpt, uint32_t idx, char *name);
uint64_t gpt_part_size(const struct gpt *gpt, uint32_t idx);

/*
 * gpt_part_add: Add a partititon
 *   idx   : Index of partition.
 *   part  : Partition data to add.
 *   follow: Whether to follow/expand.
 *
 * If follow is zero, the partition at 'idx' must be empty, and there must be
 * enough room to insert it (either it must be the last entry, or the
 * following entry must start after the end of the new entry).
 *
 * If follow is nonzero, existing partitions starting at 'idx' are pushed up
 * to make room for the new partition.  The total partition table must not
 * expand past the usable lba space.
 *
 * Returns zero if successful, nonzero on error.
 */
int gpt_part_add(struct gpt *gpt, uint32_t idx, const struct gpt_partition *part, int follow);

/*
 * gpt_part_del: Delete a partititon
 *   idx   : Index of partition.
 *   follow: Whether to follow/contract.
 *
 * If follow is zero, the partition at 'idx' is zeroed.
 *
 * If follow is nonzero, existing partitions starting at 'idx' are pushed down
 * to fill in the unused space.
 *
 * Returns zero if successful, nonzero on error.
 */
int gpt_part_del(struct gpt *gpt, uint32_t idx, int follow);

/*
 * gpt_part_move: Move a partititon
 *   idx   : Index of partition.
 *   lba   : New starting LBA.
 *   follow: Whether to follow.
 *
 * If follow is zero, the partition is moved and no other action is taken.
 * The destination must be within the current allowed space.
 *
 * If follow is nonzero, existing partitions starting at 'idx+1' are moved
 * in the same amount (up or down) as the specified partition.  The
 * destination must be above the previous partititon and the total partition
 * table must not expand past the usable space.
 *
 * Returns zero if successful, nonzero on error.
 */
int gpt_part_move(struct gpt *gpt, uint32_t idx, uint64_t lba, int follow);

/*
 * gpt_part_resize: Resize a partititon
 *   idx   : Index of partition.
 *   size  : New size.
 *   follow: Whether to follow.
 *
 * If follow is zero, the partition is resized and no other action is taken.
 * The destination must be within the current allowed space.
 *
 * If follow is nonzero, existing partitions starting at 'idx+1' are moved
 * in the same amount (up or down) as the specified partition delta.  The
 * total partition table must not expand past the usable space.
 *
 * Returns zero if successful, nonzero on error.
 */
int gpt_part_resize(struct gpt *gpt, uint32_t idx, uint64_t size, int follow);

/*
 * gpt_part_save: Save a partition to a file
 *   idx     : Index of partition.
 *   filename: Filename to save data.
 *
 * Returns zero if successful, nonzero on error.
 */
int gpt_part_save(struct gpt *gpt, uint32_t idx, const char *filename);

/*
 * gpt_part_load: Load a partition from a file
 *   idx     : Index of partition.
 *   filename: Filename to load data.
 *
 * Returns zero if successful, nonzero on error.
 */
int gpt_part_load(struct gpt *gpt, uint32_t idx, const char *filename);

#endif
