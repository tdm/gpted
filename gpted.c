#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <unistd.h>

#include <readline/readline.h>
#include <readline/history.h>

#include "util.h"

#include "gpt.h"

#define MAX_ARGS 8

static int cmd_help(struct gpt *gpt, unsigned int argc, const char **argv)
{
    return 0;
}

static int cmd_quit(struct gpt *gpt, unsigned int argc, const char **argv)
{
    return 1;
}

static int cmd_show(struct gpt *gpt, unsigned int argc, const char **argv)
{
    gpt_show(gpt);
    return 0;
}

static int cmd_write(struct gpt *gpt, unsigned int argc, const char **argv)
{
    int rc;

    rc = gpt_write(gpt);
    if (rc != 0) {
        printf("E: write failed\n");
        return 0;
    }

    return 0;
}

#if defined(ANDROID) && defined(QCOM)
static const char *non_firmware[] = {
    "recovery", "boot",
    "system", "userdata", "cache", "sdcard",
    /* persist? */
    NULL
};

static int cmd_firmware_save(struct gpt *gpt, unsigned int argc, const char **argv)
{
    int all = 0;
    uint32_t startidx, idx;

    if (argc > 1) {
        all = !strcmp(argv[1], "all");
    }

    startidx = (all ? 0 : gpt->pad_idx+1);
    for (idx = startidx; idx <= gpt->last_used_idx; ++idx) {
        char name[72/2+1];
        char filename[72/2+4+1];
        int skip = 0;
        const char **entry;

        gpt_part_name(gpt, idx, name);
        for (entry = non_firmware; *entry; ++entry) {
            if (!strcmp(name, *entry)) {
                skip = 1;
            }
        }
        if (skip) {
            printf("Skip %s\n", name);
        }
        else {
            printf("Save %s\n", name);
            sprintf(filename, "%s.img", name);
            gpt_part_save(gpt, idx, filename);
        }
    }

    return 0;
}

static int cmd_firmware_load(struct gpt *gpt, unsigned int argc, const char **argv)
{
    int all = 0;
    uint32_t startidx, idx;

    if (argc > 1) {
        all = !strcmp(argv[1], "all");
    }

    startidx = (all ? 0 : gpt->pad_idx+1);
    for (idx = startidx; idx <= gpt->last_used_idx; ++idx) {
        char name[72/2+1];
        char filename[72/2+4+1];
        int skip = 0;
        const char **entry;

        gpt_part_name(gpt, idx, name);
        for (entry = non_firmware; *entry; ++entry) {
            if (!strcmp(name, *entry)) {
                skip = 1;
            }
        }
        if (skip) {
            printf("Skip %s\n", name);
        }
        else {
            printf("Load %s\n", name);
            sprintf(filename, "%s.img", name);
            gpt_part_load(gpt, idx, filename);
        }
    }

    return 0;
}
#endif

static int cmd_part_add(struct gpt *gpt, unsigned int argc, const char **argv)
{
    printf("E: not implemented\n");
    return 0;
}

static int cmd_part_del(struct gpt *gpt, unsigned int argc, const char **argv)
{
    int rc;
    uint32_t idx;
    int follow;

    if (argc < 2) {
        printf("E: not enough args\n");
        return 0;
    }
    idx = gpt_part_find(gpt, argv[1]);
    if (idx == GPT_PART_INVALID) {
        printf("E: part %s not found\n", argv[1]);
        return 0;
    }
    follow = (argc > 2 && !strcmp(argv[2], "follow"));
    rc = gpt_part_del(gpt, idx, follow);
    if (rc != 0) {
        printf("E: failed\n");
        return 0;
    }
    return 0;
}

static int cmd_part_move(struct gpt *gpt, unsigned int argc, const char **argv)
{
    int rc;
    uint32_t idx;
    uint64_t lb;
    int follow;

    if (argc < 3) {
        printf("E: not enough args\n");
        return 0;
    }
    idx = gpt_part_find(gpt, argv[1]);
    if (idx == GPT_PART_INVALID) {
        printf("E: part %s not found\n", argv[1]);
        return 0;
    }
    lb = strtoull_u(argv[2], NULL, 0);
    follow = (argc > 3 && !strcmp(argv[3], "follow"));
    rc = gpt_part_move(gpt, idx, lb, follow);
    if (rc != 0) {
        printf("E: failed\n");
        return 0;
    }

    return 0;
}

static int cmd_part_resize(struct gpt *gpt, unsigned int argc, const char **argv)
{
    int rc;
    uint32_t idx;
    uint64_t size;
    int follow;

    if (argc < 3) {
        printf("E: not enough args\n");
        return 0;
    }
    idx = gpt_part_find(gpt, argv[1]);
    if (idx == GPT_PART_INVALID) {
        printf("E: part %s not found\n", argv[1]);
        return 0;
    }
    follow = (argc > 3 && !strcmp(argv[3], "follow"));
    if (!strcmp(argv[2], "max")) {
        if (follow) {
            struct gpt_partition *endpart;
            endpart = gpt->partitions[gpt->last_used_idx];
            size = gpt_part_size(gpt, idx) +
                    (gpt->header.last_usable_lba - endpart->last_lba) * gpt->lbsize;
        }
        else {
            size = strtoull_u(argv[2], NULL, 0);
        }
    }
    else {
        size = strtoull_u(argv[2], NULL, 0);
    }
    rc = gpt_part_resize(gpt, idx, size, follow);
    if (rc != 0) {
        printf("E: failed\n");
        return 0;
    }

    return 0;
}

static int cmd_part_load(struct gpt *gpt, unsigned int argc, const char **argv)
{
    int rc;
    uint32_t idx;

    if (argc < 3) {
        printf("E: not enough args\n");
        return 0;
    }
    idx = gpt_part_find(gpt, argv[1]);
    rc = gpt_part_load(gpt, idx, argv[2]);
    if (rc != 0) {
        printf("E: failed\n");
        return 0;
    }

    return 0;
}

static int cmd_part_save(struct gpt *gpt, unsigned int argc, const char **argv)
{
    int rc;
    uint32_t idx;

    if (argc < 3) {
        printf("E: not enough args\n");
        return 0;
    }
    idx = gpt_part_find(gpt, argv[1]);
    rc = gpt_part_save(gpt, idx, argv[2]);
    if (rc != 0) {
        printf("E: failed\n");
        return 0;
    }

    return 0;
}

struct dispatch_entry
{
    const char *cmd;
    int (*func)(struct gpt *gpt, unsigned int argc, const char **argv);
};

static struct dispatch_entry dispatch_table[] = {
    { "help",   cmd_help },
    { "quit",   cmd_quit },
    { "show",   cmd_show },
    { "write",  cmd_write },

#if defined(ANDROID) && defined(QCOM)
    { "firmware-save", cmd_firmware_save },
    { "firmware-load", cmd_firmware_load },
#endif

    { "part-add",    cmd_part_add },
    { "part-del",    cmd_part_del },
    { "part-move",   cmd_part_move },
    { "part-resize", cmd_part_resize },
    { "part-load",   cmd_part_load },
    { "part-save",   cmd_part_save },

    { NULL, NULL }
};

static int dispatch(struct gpt *gpt, char *line)
{
    char *p;
    unsigned int argc;
    const char *argv[MAX_ARGS];
    struct dispatch_entry *entry;

    while (*line == ' ') {
        ++line;
    }
    if (*line == '#' || *line == ';') {
        return 0;
    }

    argc = 0;
    p = strtok(line, " ");
    while (p != NULL) {
        argv[argc++] = p;
        p = strtok(NULL, " ");
    }
    if (argc == 0) {
        return 0;
    }

    for (entry = dispatch_table; entry->cmd != NULL; ++entry) {
        if (!strcmp(argv[0], entry->cmd)) {
            return entry->func(gpt, argc, argv);
        }
    }

    printf("Unknown command %s\n", argv[0]);
    return 0;
}

int main(int argc, char** argv)
{
    int rc;
    struct gpt gpt;
    const char* dev;
    const char *prompt;
    char *line;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <device>\n", argv[0]);
        exit(1);
    }

    dev = argv[1];
    rc = gpt_open(&gpt, dev);
    if (rc != 0) {
        fprintf(stderr, "Failed to read gpt\n");
        exit(1);
    }

    prompt = (isatty(STDIN_FILENO) ? "partedit> " : NULL);
    while ((line = readline(prompt)) != NULL) {
        rc = dispatch(&gpt, line);
        free(line);
        if (rc != 0) {
            break;
        }
    }
    if (prompt) {
        printf("\n");
    }

    gpt_close(&gpt);

    return 0;
}
