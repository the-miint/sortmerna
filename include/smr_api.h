/*
 * smr_api.h -- SortMeRNA reentrant C API
 *
 * Public header for the SortMeRNA library. The library never calls exit(),
 * abort(), or assert(), and suppresses stdout/stderr output (routing
 * through the log callback instead). smr_run() is serialized by a
 * process-level mutex, making it safe to call from multiple threads
 * with independent contexts.
 */
#ifndef SMR_API_H
#define SMR_API_H

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

/* Sentinel value for smr_config_t.evalue: disables E-value filtering */
#define SMR_EVALUE_OFF  (-1.0)

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
    /* ABI version detection -- MUST be first field.
     * uint32_t (not size_t) for consistent width across 32/64-bit platforms. */
    uint32_t struct_size;

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

    /* Logging callback (optional, may be NULL for silent operation).
     * log_user_data is copied by value into the context; the pointee
     * must outlive the context (same lifetime contract as workdir). */
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
int         smr_last_error_code(const smr_context_t *ctx);

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
 * num_reads and num_aligned are always populated.
 * Per-read arrays are indexed [0 .. num_reads-1] in input order.
 * Coordinates (ref_start, ref_end) are 1-based, matching BLAST/SAM convention.
 */
typedef struct smr_output {
    uint64_t    num_reads;
    uint64_t    num_aligned;
    const char **read_ids;     /* library-owned; smr_output_free casts away const */
    int32_t    *aligned;       /* 0 or 1 per read */
    int32_t    *ref_index;     /* -1 if unaligned */
    double     *e_value;
    double     *identity;      /* percent identity, 0-100 */
    double     *coverage;      /* query coverage, 0-100 */
    int32_t    *ref_start;     /* 1-based start on reference */
    int32_t    *ref_end;       /* 1-based end on reference */
    const char **cigar;        /* CIGAR string, NULL if unaligned;
                                  library-owned, smr_output_free casts away const */
    const char **ref_name;     /* reference sequence ID, NULL if unaligned;
                                  library-owned, smr_output_free casts away const */
    int32_t    *strand;        /* 1=forward, 0=reverse-complement; -1 if unaligned */
    int32_t    *score;         /* Smith-Waterman alignment score; -1 if unaligned */
    int32_t    *edit_distance; /* mismatches + gaps; -1 if unaligned */
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

/*
 * smr_seq_t -- a single input sequence for smr_run_seqs().
 * All pointers are caller-owned and must remain valid for the duration
 * of the smr_run_seqs() call.
 */
typedef struct smr_seq {
    const char *id;       /* identifier (without > or @) */
    const char *sequence; /* nucleotide sequence */
    const char *quality;  /* quality string, or NULL for FASTA */
} smr_seq_t;

/*
 * In-memory variant of smr_run(). Accepts sequences directly instead of
 * file paths. No temporary read files are created; sequences are served
 * to the pipeline through an in-memory readfeed.
 *
 * Reference databases are still loaded from files (index building requires
 * file paths). Only the query sequences are in-memory.
 */
int smr_run_seqs(smr_context_t *ctx,
                 const char **ref_paths, int32_t num_refs,
                 const smr_seq_t *seqs, int32_t num_seqs,
                 smr_output_t **out,
                 smr_stats_t *stats);

/* --------------------------------------------------------------------
 * Pre-loaded index (streaming) API
 *
 * For callers that align many query batches against the same references,
 * smr_index_load() pays the reference / index load cost once, and
 * smr_run_seqs_with_index() runs each batch against the cached index.
 * smr_run_seqs() is a compatibility wrapper that calls load + run + free.
 *
 * Lifetime:
 *   - The handle is owned by the caller. It holds a pointer to ctx; ctx
 *     must outlive the handle.
 *   - The handle is NOT reusable after smr_index_free().
 *   - Multiple handles may coexist; each may be bound to a different ctx.
 *
 * Thread-safety:
 *   - Serialized by the same process-wide mutex as smr_run(). Concurrent
 *     calls from multiple threads on the same (or different) handles are
 *     safe and run serially.
 *
 * Caller contract:
 *   - Read IDs must be unique across every batch submitted to a given
 *     handle. Re-using an ID in a later batch is a caller bug — results
 *     for that read become undefined. (See README "Streaming" section.)
 *
 * E-value semantics:
 *   - smr_run_seqs_with_index() computes each alignment's e-value using
 *     the textbook per-query Karlin-Altschul form:
 *         E = K * m * n_read * exp(-lambda * S)
 *     where n_read is that read's own length and m is the uncorrected
 *     database length from the reference .stats file. This differs from
 *     the CLI (sortmerna binary) in two ways, both intentional:
 *       1. n: per-read (library) vs summed query space (CLI).
 *          Library e-values are smaller (more significant-looking) than
 *          CLI e-values by roughly a factor of the batch size.
 *       2. m: uncorrected DB length (library) vs edge-corrected (CLI).
 *          Library e-values are smaller by a further ~1-3% for
 *          SILVA-scale DBs.
 *     Both changes are required for batch-splitting invariance:
 *     submitting the same reads as one batch or many batches on the same
 *     handle produces byte-identical per-read output. Callers filtering
 *     on e-value thresholds should calibrate against library output, not
 *     CLI output. See README for details.
 *
 *   - The library also disables the SW-score threshold filter used by
 *     the CLI (which otherwise discards hits below a minimum SW score
 *     derived from the target e-value). That filter's threshold is
 *     batch-dependent, so the library zeroes it and lets callers filter
 *     on e-value post-hoc. Net effect: every positive SW hit is returned;
 *     callers drop low-significance rows themselves.
 *
 * Workdir usage:
 *   - The library writes a small placeholder reads file (placeholder_reads.fa
 *     or .fq) into the handle's workdir to satisfy internal option parsing.
 *     If ctx->config.workdir is NULL, a temp directory is created and
 *     removed by smr_index_free(). If the caller supplies a workdir, the
 *     placeholder file persists there; the caller owns cleanup.
 *
 * Input format:
 *   - Each batch's format is derived from its own smr_seq_t[] contents: a
 *     non-NULL seqs[i].quality in any element means FASTQ, otherwise FASTA.
 *     All sequences within a single batch must agree. Different batches on
 *     the same handle may mix FASTA and FASTQ.
 * -------------------------------------------------------------------- */

typedef struct smr_index smr_index_t;

/*
 * Load references and build/verify the cached index.
 * The handle captures a pointer to ctx; ctx must outlive the handle.
 * Returns NULL on failure; call smr_last_error()/smr_last_error_code()
 * on ctx to retrieve details.
 */
smr_index_t *smr_index_load(smr_context_t *ctx,
                            const char **ref_paths, int32_t num_refs);

/*
 * Align an in-memory batch of query sequences against a pre-loaded index.
 * Error state and log callbacks are routed through the ctx that was
 * supplied to smr_index_load(); no separate ctx parameter is accepted
 * to avoid the ambiguity of error routing to a mismatched context.
 */
int smr_run_seqs_with_index(smr_index_t *idx,
                            const smr_seq_t *seqs, int32_t num_seqs,
                            smr_output_t **out,
                            smr_stats_t *stats);

/* Free a handle returned by smr_index_load(). Passing NULL is safe.
 * The handle is not reusable after this call. */
void smr_index_free(smr_index_t *idx);

void smr_output_free(smr_output_t *out);

/* --------------------------------------------------------------------
 * Version
 * -------------------------------------------------------------------- */

const char *smr_version(void);

#ifdef __cplusplus
}
#endif

#endif /* SMR_API_H */
