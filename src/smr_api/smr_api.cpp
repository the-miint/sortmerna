/*
 * smr_api.cpp -- SortMeRNA reentrant C API implementation
 */

#include "smr_api.h"
#include "version.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <sys/stat.h>

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

static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static bool file_is_empty(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return true;
    return st.st_size == 0;
}

int smr_run(smr_context_t *ctx,
            const char **ref_paths, int32_t num_refs,
            const char **read_paths, int32_t num_reads,
            smr_output_t **out,
            smr_stats_t *stats) {
    if (!ctx) return SMR_ERR_INVALID_CONFIG;

    /* validate inputs */
    if (!ref_paths || num_refs <= 0) {
        set_error(ctx, SMR_ERR_INVALID_CONFIG, "ref_paths is NULL or num_refs <= 0");
        return SMR_ERR_INVALID_CONFIG;
    }
    if (!read_paths || num_reads <= 0) {
        set_error(ctx, SMR_ERR_INVALID_CONFIG, "read_paths is NULL or num_reads <= 0");
        return SMR_ERR_INVALID_CONFIG;
    }

    /* validate file existence */
    for (int32_t i = 0; i < num_refs; i++) {
        if (!ref_paths[i] || !file_exists(ref_paths[i])) {
            set_error(ctx, SMR_ERR_IO, "reference file not found: %s",
                      ref_paths[i] ? ref_paths[i] : "(null)");
            return SMR_ERR_IO;
        }
        if (file_is_empty(ref_paths[i])) {
            set_error(ctx, SMR_ERR_IO, "reference file is empty: %s", ref_paths[i]);
            return SMR_ERR_IO;
        }
    }
    for (int32_t i = 0; i < num_reads; i++) {
        if (!read_paths[i] || !file_exists(read_paths[i])) {
            set_error(ctx, SMR_ERR_IO, "reads file not found: %s",
                      read_paths[i] ? read_paths[i] : "(null)");
            return SMR_ERR_IO;
        }
        if (file_is_empty(read_paths[i])) {
            set_error(ctx, SMR_ERR_IO, "reads file is empty: %s", read_paths[i]);
            return SMR_ERR_IO;
        }
    }

    (void)out; (void)stats;
    set_error(ctx, SMR_ERR_NOT_IMPLEMENTED, "smr_run pipeline not yet implemented");
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
