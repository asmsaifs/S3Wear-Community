// Factory test results and their JSON form (pure C, host-tested).
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { FT_PENDING = 0, FT_RUNNING, FT_PASS, FT_FAIL, FT_SKIP } ft_status_t;

#define FT_DETAIL_LEN 64

typedef struct {
    const char *name;
    ft_status_t status;
    char detail[FT_DETAIL_LEN];
} ft_result_t;

const char *ft_status_str(ft_status_t s);

/** True if nothing failed and nothing is still pending/running (SKIP is allowed). */
bool ft_overall_pass(const ft_result_t *r, size_t n);

/**
 * One-line JSON:
 * {"factory_test":{"fw":"..","pass":true,"skipped":0,"results":[{"name":"..","status":"PASS","detail":".."}]}}
 * Same contract as snprintf: returns the length it needed (excluding NUL), writes at
 * most cap bytes, always NUL-terminates when cap > 0.
 */
size_t ft_json_write(char *buf, size_t cap, const char *fw_version, const ft_result_t *r, size_t n);

#ifdef __cplusplus
}
#endif
