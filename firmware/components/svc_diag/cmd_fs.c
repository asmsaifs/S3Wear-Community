// Console: `sd info|mount|unmount` and `fs ls|df|cat|write|b64|rm` (P1-08, b64: P8-05, cat offset: P5-01).
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bsp_s3w.h"
#include "esp_console.h"
#include "mbedtls/base64.h"
#include "svc_diag_priv.h"
#include "svc_storage.h"

#define CAT_MAX_BYTES 4096

static void print_usage(const char *path)
{
    uint64_t total = 0;
    uint64_t used = 0;
    if (svc_storage_usage(path, &total, &used) == ESP_OK) {
        printf("%-7s %8llu KB used of %8llu KB (%llu%%)\n", path, used >> 10, total >> 10,
               total ? used * 100 / total : 0);
    } else {
        printf("%-7s not mounted\n", path);
    }
}

static int sd_info(void)
{
    if (!svc_storage_sd_check()) {
        printf("no SD card mounted (try `sd mount`)\n");
        return 1;
    }
    const sdmmc_card_t *c = bsp_sdcard();
    const uint64_t bytes = (uint64_t)c->csd.capacity * c->csd.sector_size;
    printf("name %s, %s, %llu MB, %d kHz\n", c->cid.name,
           c->ocr & (1 << 30) ? "SDHC/SDXC" : "SDSC", bytes >> 20, c->real_freq_khz);
    print_usage(SVC_STORAGE_SD_PATH);
    return 0;
}

static int cmd_sd(int argc, char **argv)
{
    const char *sub = argc >= 2 ? argv[1] : "info";
    if (strcmp(sub, "info") == 0) {
        return sd_info();
    }
    if (strcmp(sub, "mount") == 0) {
        const esp_err_t err = svc_storage_sd_mount();
        printf("mount: %s\n", esp_err_to_name(err));
        return err == ESP_OK ? sd_info() : 1;
    }
    if (strcmp(sub, "unmount") == 0) {
        return svc_storage_sd_unmount() == ESP_OK ? 0 : 1;
    }
    printf("usage: sd info | mount | unmount\n");
    return 1;
}

static int fs_ls(const char *path)
{
    DIR *d = opendir(path);
    if (!d) {
        printf("cannot open %s\n", path);
        return 1;
    }
    int n = 0;
    for (struct dirent *e; (e = readdir(d)) != NULL; n++) {
        char full[300];
        if (snprintf(full, sizeof full, "%s/%s", path, e->d_name) >= (int)sizeof full) {
            printf("  %-32s (path too long)\n", e->d_name);
            continue;
        }
        struct stat st;
        const bool have = stat(full, &st) == 0;
        if (have && S_ISDIR(st.st_mode)) {
            printf("  %-32s <dir>\n", e->d_name);
        } else {
            printf("  %-32s %ld\n", e->d_name, have ? (long)st.st_size : -1L);
        }
    }
    closedir(d);
    printf("%d entr%s\n", n, n == 1 ? "y" : "ies");
    return 0;
}

// At most CAT_MAX_BYTES from offset (longer files: call again with the next offset).
static int fs_cat(const char *path, long offset)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("cannot open %s\n", path);
        return 1;
    }
    if (offset > 0 && fseek(f, offset, SEEK_SET) != 0) {
        fclose(f);
        printf("cannot seek to %ld\n", offset);
        return 1;
    }
    char buf[128];
    size_t total = 0;
    size_t n;
    while (total < CAT_MAX_BYTES && (n = fread(buf, 1, sizeof buf, f)) > 0) {
        fwrite(buf, 1, n, stdout);
        total += n;
    }
    fclose(f);
    printf("\n");
    return 0;
}

static int fs_write(const char *path, int argc, char **argv)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        printf("cannot create %s\n", path);
        return 1;
    }
    for (int i = 0; i < argc; i++) {
        fprintf(f, "%s%s", i ? " " : "", argv[i]);
    }
    fputc('\n', f);
    const bool ok = fclose(f) == 0;
    printf("%s %s\n", ok ? "wrote" : "write failed:", path);
    return ok ? 0 : 1;
}

// Appends base64 data to a file: binary files from the host over the console, a line at a time
// (P8-05: app packages for `app install`; the s3w CLI's USB install replaces it, P8-06).
static int fs_b64(const char *path, const char *b64)
{
    unsigned char bin[192]; // a 256-byte command line holds < 256 base64 characters
    size_t n = 0;
    if (mbedtls_base64_decode(bin, sizeof bin, &n, (const unsigned char *)b64, strlen(b64)) != 0) {
        printf("bad base64\n");
        return 1;
    }
    FILE *f = fopen(path, "ab");
    const bool ok = f && fwrite(bin, 1, n, f) == n;
    if (f && fclose(f) != 0) {
        printf("write failed: %s\n", path);
        return 1;
    }
    printf(ok ? "+%u\n" : "write failed\n", (unsigned)n);
    return ok ? 0 : 1;
}

static int cmd_fs(int argc, char **argv)
{
    const char *sub = argc >= 2 ? argv[1] : "";
    if (strcmp(sub, "ls") == 0) {
        return fs_ls(argc >= 3 ? argv[2] : SVC_STORAGE_FLASH_PATH);
    }
    if (strcmp(sub, "df") == 0) {
        print_usage(SVC_STORAGE_FLASH_PATH);
        print_usage(SVC_STORAGE_SD_PATH);
        return 0;
    }
    if (strcmp(sub, "cat") == 0 && argc >= 3) {
        return fs_cat(argv[2], argc >= 4 ? atol(argv[3]) : 0);
    }
    if (strcmp(sub, "write") == 0 && argc >= 4) {
        return fs_write(argv[2], argc - 3, argv + 3);
    }
    if (strcmp(sub, "b64") == 0 && argc == 4) {
        return fs_b64(argv[2], argv[3]);
    }
    if (strcmp(sub, "rm") == 0 && argc >= 3) {
        const bool ok = unlink(argv[2]) == 0;
        printf("%s\n", ok ? "removed" : "remove failed");
        return ok ? 0 : 1;
    }
    printf("usage: fs ls [path] | df | cat <file> [offset] | write <file> <text...> | b64 <file> <base64> | rm <file>\n");
    return 1;
}

esp_err_t diag_register_fs(void)
{
    const esp_console_cmd_t sd = {
        .command = "sd",
        .help = "TF card: info (default), mount, unmount",
        .hint = "info|mount|unmount",
        .func = cmd_sd,
    };
    const esp_console_cmd_t fs = {
        .command = "fs",
        .help = "Files on /flash (LittleFS) and /sd (FAT): ls [path], df, cat <f> [offset] (4 KB), write <f> <text>, b64 <f> <data> "
                "(append), rm <f>",
        .hint = "ls|df|cat|write|b64|rm",
        .func = cmd_fs,
    };
    esp_err_t err = esp_console_cmd_register(&sd);
    return err == ESP_OK ? esp_console_cmd_register(&fs) : err;
}
