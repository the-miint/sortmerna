/*
 * smr_api.h -- SortMeRNA reentrant C API
 *
 * Public header for the SortMeRNA library. All functions are safe to call
 * from multiple threads with independent contexts. The library never calls
 * exit(), abort(), or assert(), and never writes to stdout/stderr.
 */
#ifndef SMR_API_H
#define SMR_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------
 * Error codes
 * -------------------------------------------------------------------- */

#define SMR_OK                    0
#define SMR_ERR_INVALID_CONFIG   -1
#define SMR_ERR_ALLOC            -2
#define SMR_ERR_IO               -3
#define SMR_ERR_INDEX            -4
#define SMR_ERR_ALIGN            -5
#define SMR_ERR_NOT_IMPLEMENTED  -99

/* --------------------------------------------------------------------
 * Log levels (for log_callback)
 * -------------------------------------------------------------------- */

#define SMR_LOG_DEBUG  0
#define SMR_LOG_INFO   1
#define SMR_LOG_WARN   2
#define SMR_LOG_ERROR  3

/* --------------------------------------------------------------------
 * Configuration
 * -------------------------------------------------------------------- */

typedef struct smr_config {
    /* ABI version detection -- MUST be first field */
    size_t struct_size;

    /* Threading */
    int32_t num_threads;

    /* Alignment scoring parameters.
     * gap_open and gap_ext are int32_t here; Runopts uses long internally.
     * Realistic values are small positive integers so no truncation risk. */
    int32_t match;
    int32_t mismatch;
    int32_t gap_open;
    int32_t gap_ext;
    int32_t score_N;        /* penalty for ambiguous bases (N) */
    double  evalue;
    uint32_t seed_win_len;
    uint32_t num_alignments;

    /* Boolean flags (int32_t per guidance, not bool) */
    int32_t best;
    int32_t paired;         /* sets Runopts::is_paired; paired_in/paired_out
                               are controlled by output format, not this flag */
    int32_t forward_only;
    int32_t reverse_only;
    int32_t full_search;

    /* Output control */
    int32_t fastx;
    int32_t sam;
    int32_t blast;
    int32_t otu_map;
    int32_t denovo;

    /* Paths (caller-owned, must outlive context) */
    const char *workdir;

    /* Logging callback (optional, may be NULL for silent operation) */
    void (*log_callback)(int level, const char *msg, void *user_data);
    void *log_user_data;

} smr_config_t;

void smr_config_init(smr_config_t *cfg);

/* --------------------------------------------------------------------
 * Context lifecycle
 * -------------------------------------------------------------------- */

typedef struct smr_context smr_context_t;

smr_context_t *smr_ctx_create(const smr_config_t *cfg);
void           smr_ctx_destroy(smr_context_t *ctx);

/* --------------------------------------------------------------------
 * Error reporting
 * -------------------------------------------------------------------- */

const char *smr_strerror(int code);
const char *smr_last_error(const smr_context_t *ctx);

/* --------------------------------------------------------------------
 * Computation
 * -------------------------------------------------------------------- */

/*
 * smr_output_t -- alignment results returned by smr_run().
 *
 * Ownership: the library allocates this struct and all inner arrays/strings.
 * The caller MUST NOT free individual fields. Call smr_output_free() once
 * when done; passing NULL is safe. The output is independent of the context
 * and remains valid after smr_ctx_destroy().
 *
 * All arrays are indexed [0 .. num_reads-1]. One entry per input read.
 * Coordinates (ref_start, ref_end) are 1-based, matching BLAST/SAM convention.
 */
typedef struct smr_output {
    uint64_t    num_reads;
    uint64_t    num_aligned;
    const char **read_ids;     /* library-owned strings */
    int32_t    *aligned;       /* 0 or 1 per read */
    int32_t    *ref_index;     /* -1 if unaligned */
    double     *e_value;
    double     *identity;      /* percent identity, 0-100 */
    double     *coverage;      /* query coverage, 0-100 */
    int32_t    *ref_start;     /* 1-based start on reference */
    int32_t    *ref_end;       /* 1-based end on reference */
    const char **cigar;        /* CIGAR string, NULL if unaligned */
} smr_output_t;

typedef struct smr_stats {
    uint64_t total_reads;
    uint64_t total_aligned;
    uint64_t total_id_cov_pass;
    uint64_t total_denovo;
    uint32_t min_read_len;
    uint32_t max_read_len;
    double   wall_time_sec;
} smr_stats_t;

int smr_run(smr_context_t *ctx,
            const char **ref_paths, int32_t num_refs,
            const char **read_paths, int32_t num_reads,
            smr_output_t **out,
            smr_stats_t *stats);

void smr_output_free(smr_output_t *out);

/* --------------------------------------------------------------------
 * Version
 * -------------------------------------------------------------------- */

const char *smr_version(void);

#ifdef __cplusplus
}
#endif

#endif /* SMR_API_H */
