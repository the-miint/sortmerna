/*
 * smr_api.cpp -- SortMeRNA reentrant C API implementation
 */

#include "smr_api.h"
#include "version.h"
#include <cstring>
#include <cstdio>

/* stringification helpers for version macros */
#define SMR_STRINGIFY2(x) #x
#define SMR_STRINGIFY(x) SMR_STRINGIFY2(x)

/* --- Configuration --- */

void smr_config_init(smr_config_t *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->struct_size = sizeof(*cfg);
}

/* --- Context lifecycle --- */

smr_context_t *smr_ctx_create(const smr_config_t *cfg) {
    (void)cfg;
    return nullptr; /* stub -- Phase 2 will implement */
}

void smr_ctx_destroy(smr_context_t *ctx) {
    if (!ctx) return; /* NULL is always safe -- contract */
    /* stub -- Phase 2 will implement */
}

/* --- Error reporting --- */

const char *smr_strerror(int code) {
    (void)code;
    return "Not implemented";
}

const char *smr_last_error(const smr_context_t *ctx) {
    (void)ctx;
    return "Not implemented";
}

/* --- Computation --- */

int smr_run(smr_context_t *ctx,
            const char **ref_paths, int32_t num_refs,
            const char **read_paths, int32_t num_reads,
            smr_output_t **out,
            smr_stats_t *stats) {
    (void)ctx; (void)ref_paths; (void)num_refs;
    (void)read_paths; (void)num_reads;
    (void)out; (void)stats;
    return SMR_ERR_NOT_IMPLEMENTED;
}

void smr_output_free(smr_output_t *out) {
    if (!out) return; /* NULL is always safe -- contract */
    /* stub -- Phase 5 will implement */
}

/* --- Version --- */

const char *smr_version(void) {
    return SMR_STRINGIFY(SORTMERNA_MAJOR) "."
           SMR_STRINGIFY(SORTMERNA_MINOR) "."
           SMR_STRINGIFY(SORTMERNA_PATCH);
}
