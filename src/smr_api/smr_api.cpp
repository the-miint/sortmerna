/*
 * smr_api.cpp -- SortMeRNA reentrant C API implementation
 */

#include "smr_api.h"
#include "version.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>

/* stringification helpers for version macros */
#define SMR_STRINGIFY2(x) #x
#define SMR_STRINGIFY(x) SMR_STRINGIFY2(x)

/* --- Internal context definition (opaque to callers) --- */

struct smr_context {
    smr_config_t config;
    char last_error[1024];
    int last_error_code;
};

static void set_error(smr_context *ctx, int code, const char *fmt, ...) {
    if (!ctx) return;
    ctx->last_error_code = code;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ctx->last_error, sizeof(ctx->last_error), fmt, ap);
    va_end(ap);
}

static void ctx_log(const smr_context *ctx, int level, const char *fmt, ...) {
    if (!ctx || !ctx->config.log_callback) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ctx->config.log_callback(level, buf, ctx->config.log_user_data);
}

/* --- Configuration --- */

void smr_config_init(smr_config_t *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->struct_size = sizeof(*cfg);

    /* threading */
    cfg->num_threads = 2;          /* Runopts::num_proc_thread */

    /* alignment scoring -- matches Runopts defaults */
    cfg->match    =  2;            /* Runopts::match */
    cfg->mismatch = -3;            /* Runopts::mismatch */
    cfg->gap_open =  5;            /* Runopts::gap_open */
    cfg->gap_ext  =  2;            /* Runopts::gap_extension */
    cfg->score_N  =  0;            /* Runopts::score_N */
    cfg->evalue   = -1.0;          /* Runopts::evalue (-1 = unset) */

    /* indexing */
    cfg->seed_win_len   = 18;      /* Runopts::seed_win_len */
    cfg->num_alignments =  1;      /* Runopts::num_alignments */

    /* boolean flags */
    cfg->best = 1;                 /* Runopts::is_best */
    /* paired, forward_only, reverse_only, full_search default to 0 (from memset) */
    /* fastx, sam, blast, otu_map, denovo default to 0 (from memset) */
}

/* --- Context lifecycle --- */

smr_context_t *smr_ctx_create(const smr_config_t *cfg) {
    if (!cfg) return nullptr;
    if (cfg->struct_size != sizeof(smr_config_t)) return nullptr;

    auto *ctx = static_cast<smr_context_t *>(malloc(sizeof(smr_context_t)));
    if (!ctx) return nullptr;

    memset(ctx, 0, sizeof(*ctx));
    memcpy(&ctx->config, cfg, sizeof(smr_config_t));

    ctx_log(ctx, SMR_LOG_INFO, "context created");
    return ctx;
}

void smr_ctx_destroy(smr_context_t *ctx) {
    if (!ctx) return; /* NULL is always safe -- contract */
    /* Log before any resource teardown so callback fires against a live context */
    ctx_log(ctx, SMR_LOG_INFO, "context destroyed");
    free(ctx);
}

/* --- Error reporting --- */

const char *smr_strerror(int code) {
    switch (code) {
    case SMR_OK:                  return "Success";
    case SMR_ERR_INVALID_CONFIG:  return "Invalid configuration";
    case SMR_ERR_ALLOC:           return "Memory allocation failed";
    case SMR_ERR_IO:              return "I/O error";
    case SMR_ERR_INDEX:           return "Index error";
    case SMR_ERR_ALIGN:           return "Alignment error";
    case SMR_ERR_NOT_IMPLEMENTED: return "Not implemented";
    default:                      return "Unknown error";
    }
}

const char *smr_last_error(const smr_context_t *ctx) {
    if (!ctx) return "";
    return ctx->last_error;
}

int smr_last_error_code(const smr_context_t *ctx) {
    if (!ctx) return SMR_ERR_INVALID_CONFIG;
    return ctx->last_error_code;
}

/* --- Computation --- */

int smr_run(smr_context_t *ctx,
            const char **ref_paths, int32_t num_refs,
            const char **read_paths, int32_t num_reads,
            smr_output_t **out,
            smr_stats_t *stats) {
    (void)ref_paths; (void)num_refs;
    (void)read_paths; (void)num_reads;
    (void)out; (void)stats;
    if (!ctx) return SMR_ERR_INVALID_CONFIG;
    set_error(ctx, SMR_ERR_NOT_IMPLEMENTED, "smr_run not yet implemented");
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
