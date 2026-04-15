/*
 * test_smoke.c -- main test runner for smr_api
 * Compiled as C to verify smr_api.h is C-linkable.
 *
 * Single-TU design: all test files are #include'd here.
 */

#include "test_harness.h"
#include "smr_api.h"
#include <stddef.h>  /* offsetof */
#include <stdlib.h>  /* mkdtemp, system */
#include <unistd.h>  /* dup, dup2, rmdir */
#include <pthread.h> /* concurrent test */
#include <math.h>    /* fabs */

/*
 * Compare a produced output file against a golden reference file.
 * For SAM files, skip @-header lines (contain workdir paths that vary).
 * Returns 1 if files match, 0 otherwise.
 */
static int files_match(const char *produced, const char *golden, int skip_sam_headers) {
    FILE *fp = fopen(produced, "r");
    FILE *fg = fopen(golden, "r");
    if (!fp || !fg) {
        if (fp) fclose(fp);
        if (fg) fclose(fg);
        return 0;
    }
    char lp[4096], lg[4096];
    int match = 1;
    while (1) {
        char *rp = fgets(lp, sizeof(lp), fp);
        char *rg = fgets(lg, sizeof(lg), fg);
        /* skip SAM header lines in both files */
        if (skip_sam_headers) {
            while (rp && lp[0] == '@') rp = fgets(lp, sizeof(lp), fp);
            while (rg && lg[0] == '@') rg = fgets(lg, sizeof(lg), fg);
        }
        if (!rp && !rg) break; /* both EOF */
        if (!rp || !rg) { match = 0; break; } /* one EOF early */
        if (strcmp(lp, lg) != 0) { match = 0; break; }
    }
    fclose(fp);
    fclose(fg);
    return match;
}

/* ---- Phase 0: Smoke tests ---- */

TEST(test_config_init_sets_struct_size) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    ASSERT_EQ_INT((int)cfg.struct_size, (int)sizeof(smr_config_t));
}

TEST(test_config_struct_size_is_first_field) {
    ASSERT_EQ_INT((int)offsetof(smr_config_t, struct_size), 0);
}

TEST(test_config_init_zeroes_pointers) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    ASSERT_NULL(cfg.workdir);
    ASSERT_NULL(cfg.log_callback);
    ASSERT_NULL(cfg.log_user_data);
}

TEST(test_version_returns_440) {
    const char *v = smr_version();
    ASSERT_NOT_NULL(v);
    ASSERT_STR_EQ(v, "4.4.0");
}

TEST(test_strerror_returns_string) {
    const char *s = smr_strerror(SMR_OK);
    ASSERT_NOT_NULL(s);
}

TEST(test_output_free_null_safe) {
    smr_output_free(NULL);
    ASSERT_TRUE(1);
}

TEST(test_ctx_destroy_null_safe) {
    smr_ctx_destroy(NULL);
    ASSERT_TRUE(1);
}

/* ---- Phase 1: Config defaults ---- */

TEST(test_config_default_threads) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    ASSERT_EQ_INT(cfg.num_threads, 2);
}

TEST(test_config_default_num_alignments) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    ASSERT_EQ_INT(cfg.num_alignments, 1);
}

TEST(test_config_default_match) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    ASSERT_EQ_INT(cfg.match, 2);
}

TEST(test_config_default_mismatch) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    ASSERT_EQ_INT(cfg.mismatch, -3);
}

TEST(test_config_default_gap_open) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    ASSERT_EQ_INT(cfg.gap_open, 5);
}

TEST(test_config_default_gap_ext) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    ASSERT_EQ_INT(cfg.gap_ext, 2);
}

TEST(test_config_default_score_N) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    ASSERT_EQ_INT(cfg.score_N, -3);
}

TEST(test_config_default_evalue) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    /* Runopts default is SMR_EVALUE_OFF (-1.0), IEEE 754 representable */
    ASSERT_TRUE(cfg.evalue == SMR_EVALUE_OFF);
}

TEST(test_config_default_seed_win_len) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    ASSERT_EQ_INT(cfg.seed_win_len, 18);
}

TEST(test_config_default_best) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    ASSERT_EQ_INT(cfg.best, 1);
}

TEST(test_config_default_booleans_off) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    ASSERT_EQ_INT(cfg.paired, 0);
    ASSERT_EQ_INT(cfg.forward_only, 0);
    ASSERT_EQ_INT(cfg.reverse_only, 0);
    ASSERT_EQ_INT(cfg.full_search, 0);
}

TEST(test_config_boolean_type_is_int32) {
    ASSERT_EQ_SZ(sizeof(((smr_config_t *)0)->best), sizeof(int32_t));
    ASSERT_EQ_SZ(sizeof(((smr_config_t *)0)->paired), sizeof(int32_t));
    ASSERT_EQ_SZ(sizeof(((smr_config_t *)0)->forward_only), sizeof(int32_t));
}

TEST(test_config_explicit_width_types) {
    ASSERT_EQ_SZ(sizeof(((smr_config_t *)0)->num_threads), sizeof(int32_t));
    ASSERT_EQ_SZ(sizeof(((smr_config_t *)0)->seed_win_len), sizeof(uint32_t));
    ASSERT_EQ_SZ(sizeof(((smr_config_t *)0)->num_alignments), sizeof(uint32_t));
}

/* ---- Phase 2: Context lifecycle and error reporting ---- */

static void test_log_cb(int level, const char *msg, void *user_data) {
    (void)level; (void)msg;
    int *counter = (int *)user_data;
    if (counter) (*counter)++;
}

TEST(test_ctx_create_returns_non_null) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    smr_context_t *ctx = smr_ctx_create(&cfg);
    ASSERT_NOT_NULL(ctx);
    smr_ctx_destroy(ctx);
}

TEST(test_ctx_create_null_config_returns_null) {
    smr_context_t *ctx = smr_ctx_create(NULL);
    ASSERT_NULL(ctx);
}

TEST(test_ctx_create_bad_struct_size_returns_null) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.struct_size = 0;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    ASSERT_NULL(ctx);
}

TEST(test_ctx_create_multiple_independent) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    smr_context_t *a = smr_ctx_create(&cfg);
    smr_context_t *b = smr_ctx_create(&cfg);
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);
    ASSERT_TRUE(a != b);
    smr_ctx_destroy(a);
    smr_ctx_destroy(b);
}

TEST(test_ctx_last_error_empty_initially) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    smr_context_t *ctx = smr_ctx_create(&cfg);
    ASSERT_NOT_NULL(ctx);
    const char *err = smr_last_error(ctx);
    ASSERT_NOT_NULL(err);
    ASSERT_EQ_INT((int)err[0], 0); /* empty string */
    smr_ctx_destroy(ctx);
}

TEST(test_strerror_success_msg) {
    const char *s = smr_strerror(SMR_OK);
    ASSERT_STR_EQ(s, "Success");
}

TEST(test_strerror_invalid_config_msg) {
    const char *s = smr_strerror(SMR_ERR_INVALID_CONFIG);
    ASSERT_NOT_NULL(s);
    ASSERT_TRUE(s[0] != '\0');
}

TEST(test_strerror_unknown_code) {
    const char *s = smr_strerror(99999);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s, "Unknown error");
}

TEST(test_log_callback_receives_messages) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    int count = 0;
    cfg.log_callback = test_log_cb;
    cfg.log_user_data = &count;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    ASSERT_NOT_NULL(ctx);
    ASSERT_TRUE(count > 0);
    smr_ctx_destroy(ctx);
}

TEST(test_last_error_set_after_smr_run) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    smr_context_t *ctx = smr_ctx_create(&cfg);
    ASSERT_NOT_NULL(ctx);
    int rc = smr_run(ctx, NULL, 0, NULL, 0, NULL, NULL);
    ASSERT_TRUE(rc < 0);
    ASSERT_TRUE(smr_last_error_code(ctx) < 0);
    const char *err = smr_last_error(ctx);
    ASSERT_NOT_NULL(err);
    ASSERT_TRUE(err[0] != '\0');
    smr_ctx_destroy(ctx);
}

TEST(test_last_error_code_initially_zero) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    smr_context_t *ctx = smr_ctx_create(&cfg);
    ASSERT_NOT_NULL(ctx);
    ASSERT_EQ_INT(smr_last_error_code(ctx), SMR_OK);
    smr_ctx_destroy(ctx);
}

TEST(test_last_error_code_null_ctx) {
    ASSERT_EQ_INT(smr_last_error_code(NULL), SMR_ERR_INVALID_CONFIG);
}

/* ---- Phase 3: Global state encapsulation ---- */

TEST(test_two_contexts_create_destroy) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    smr_context_t *a = smr_ctx_create(&cfg);
    smr_context_t *b = smr_ctx_create(&cfg);
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);
    /* destroy in reverse order to test independence */
    smr_ctx_destroy(b);
    smr_ctx_destroy(a);
    /* re-create after destroy to test reuse */
    smr_context_t *c = smr_ctx_create(&cfg);
    ASSERT_NOT_NULL(c);
    smr_ctx_destroy(c);
}

/* ---- Phase 4: Error handling ---- */

TEST(test_run_null_refs_returns_error) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    smr_context_t *ctx = smr_ctx_create(&cfg);
    const char *reads[] = { SMR_DATA_DIR "/test_read.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    int rc = smr_run(ctx, NULL, 0, reads, 1, &out, &stats);
    ASSERT_TRUE(rc < 0);
    smr_ctx_destroy(ctx);
}

TEST(test_run_null_reads_returns_error) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    smr_context_t *ctx = smr_ctx_create(&cfg);
    const char *refs[] = { SMR_DATA_DIR "/test_ref.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    int rc = smr_run(ctx, refs, 1, NULL, 0, &out, &stats);
    ASSERT_TRUE(rc < 0);
    smr_ctx_destroy(ctx);
}

TEST(test_run_nonexistent_ref_returns_error) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    smr_context_t *ctx = smr_ctx_create(&cfg);
    const char *refs[] = { "/nonexistent/ref.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/test_read.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    int rc = smr_run(ctx, refs, 1, reads, 1, &out, &stats);
    ASSERT_EQ_INT(rc, SMR_ERR_IO);
    smr_ctx_destroy(ctx);
}

TEST(test_run_nonexistent_reads_returns_error) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    smr_context_t *ctx = smr_ctx_create(&cfg);
    const char *refs[] = { SMR_DATA_DIR "/test_ref.fasta" };
    const char *reads[] = { "/nonexistent/reads.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    int rc = smr_run(ctx, refs, 1, reads, 1, &out, &stats);
    ASSERT_EQ_INT(rc, SMR_ERR_IO);
    smr_ctx_destroy(ctx);
}

TEST(test_run_empty_ref_returns_error) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    smr_context_t *ctx = smr_ctx_create(&cfg);
    const char *refs[] = { SMR_DATA_DIR "/empty_file.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/test_read.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    int rc = smr_run(ctx, refs, 1, reads, 1, &out, &stats);
    ASSERT_TRUE(rc < 0);
    smr_ctx_destroy(ctx);
}

TEST(test_last_error_descriptive_after_bad_input) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    smr_context_t *ctx = smr_ctx_create(&cfg);
    const char *refs[] = { "/nonexistent/ref.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/test_read.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    smr_run(ctx, refs, 1, reads, 1, &out, &stats);
    const char *err = smr_last_error(ctx);
    ASSERT_NOT_NULL(err);
    ASSERT_TRUE(err[0] != '\0');
    smr_ctx_destroy(ctx);
}

TEST(test_run_bad_input_does_not_crash) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    smr_context_t *ctx = smr_ctx_create(&cfg);
    /* multiple bad calls in sequence — none should crash */
    smr_run(ctx, NULL, 0, NULL, 0, NULL, NULL);
    const char *refs[] = { "/bad" };
    const char *reads[] = { "/bad" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    smr_run(ctx, refs, 1, reads, 1, &out, &stats);
    smr_run(ctx, refs, 1, NULL, 0, &out, &stats);
    /* if we got here, nothing crashed */
    ASSERT_TRUE(1);
    smr_ctx_destroy(ctx);
}

/* ---- Phase 5: smr_run computation ---- */

TEST(test_run_tiny_aligned_count) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    ASSERT_NOT_NULL(ctx);
    const char *refs[] = { SMR_DATA_DIR "/test_ref.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/test_read.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    int rc = smr_run(ctx, refs, 1, reads, 1, &out, &stats);
    ASSERT_EQ_INT(rc, SMR_OK);
    ASSERT_NOT_NULL(out);
    ASSERT_EQ_U64(out->num_reads, 1);
    ASSERT_EQ_U64(out->num_aligned, 1);
    smr_output_free(out);
    smr_ctx_destroy(ctx);
}

TEST(test_run_tiny_stats) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    const char *refs[] = { SMR_DATA_DIR "/test_ref.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/test_read.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    memset(&stats, 0, sizeof(stats));
    smr_run(ctx, refs, 1, reads, 1, &out, &stats);
    ASSERT_EQ_U64(stats.total_reads, 1);
    ASSERT_EQ_U64(stats.total_aligned, 1);
    ASSERT_TRUE(stats.min_read_len > 0);
    ASSERT_TRUE(stats.max_read_len > 0);
    smr_output_free(out);
    smr_ctx_destroy(ctx);
}

TEST(test_run_small_aligned_count) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    const char *refs[] = { SMR_DATA_DIR "/silva-arc-16s-database-id95.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/set7_arc_bac_16S_database_match.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    int rc = smr_run(ctx, refs, 1, reads, 1, &out, &stats);
    ASSERT_EQ_INT(rc, SMR_OK);
    ASSERT_NOT_NULL(out);
    ASSERT_EQ_U64(out->num_reads, 6);
    ASSERT_EQ_U64(out->num_aligned, 4);
    smr_output_free(out);
    smr_ctx_destroy(ctx);
}

TEST(test_run_output_free_after_run) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    const char *refs[] = { SMR_DATA_DIR "/test_ref.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/test_read.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    smr_run(ctx, refs, 1, reads, 1, &out, &stats);
    smr_output_free(out);
    /* if we got here, no crash */
    ASSERT_TRUE(1);
    smr_ctx_destroy(ctx);
}

TEST(test_run_multiple_sequential) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    const char *refs[] = { SMR_DATA_DIR "/test_ref.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/test_read.fasta" };
    smr_output_t *out1 = NULL;
    smr_output_t *out2 = NULL;
    smr_stats_t stats;
    int rc1 = smr_run(ctx, refs, 1, reads, 1, &out1, &stats);
    int rc2 = smr_run(ctx, refs, 1, reads, 1, &out2, &stats);
    ASSERT_EQ_INT(rc1, SMR_OK);
    ASSERT_EQ_INT(rc2, SMR_OK);
    ASSERT_EQ_U64(out1->num_aligned, out2->num_aligned);
    smr_output_free(out1);
    smr_output_free(out2);
    smr_ctx_destroy(ctx);
}

/* ---- Phase 6: I/O isolation ---- */

TEST(test_no_stdout_during_run) {
    /* redirect stdout to /dev/null, run pipeline, check nothing leaked */
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    const char *refs[] = { SMR_DATA_DIR "/test_ref.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/test_read.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;

    /* capture stdout to a temp file */
    fflush(stdout);
    int old_stdout = dup(1);
    FILE *tmp = tmpfile();
    int tmp_fd = fileno(tmp);
    dup2(tmp_fd, 1);

    int rc = smr_run(ctx, refs, 1, reads, 1, &out, &stats);

    /* restore stdout */
    fflush(stdout);
    dup2(old_stdout, 1);
    close(old_stdout);

    /* check run succeeded and captured output is empty */
    ASSERT_EQ_INT(rc, SMR_OK);

    fseek(tmp, 0, SEEK_END);
    long captured_size = ftell(tmp);
    fclose(tmp);

    smr_output_free(out);
    smr_ctx_destroy(ctx);

    ASSERT_EQ_INT((int)captured_size, 0);
}

static void run_log_cb(int level, const char *msg, void *user_data) {
    (void)level; (void)msg;
    int *counter = (int *)user_data;
    if (counter) (*counter)++;
}

TEST(test_log_callback_fires_during_run) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    int count = 0;
    cfg.log_callback = run_log_cb;
    cfg.log_user_data = &count;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    const char *refs[] = { SMR_DATA_DIR "/test_ref.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/test_read.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    smr_run(ctx, refs, 1, reads, 1, &out, &stats);
    /* at minimum: "context created", "pipeline starting", "pipeline complete" */
    ASSERT_TRUE(count >= 3);
    smr_output_free(out);
    smr_ctx_destroy(ctx);
}

TEST(test_null_log_callback_silent) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    cfg.log_callback = NULL;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    const char *refs[] = { SMR_DATA_DIR "/test_ref.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/test_read.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    int rc = smr_run(ctx, refs, 1, reads, 1, &out, &stats);
    ASSERT_EQ_INT(rc, SMR_OK);
    smr_output_free(out);
    smr_ctx_destroy(ctx);
}

/* ---- Phase 7: Golden file comparison ---- */

static char _tmpdir_buf[256];
static char *make_tmpdir(void) {
    snprintf(_tmpdir_buf, sizeof(_tmpdir_buf), "/tmp/smr_test_XXXXXX");
    return mkdtemp(_tmpdir_buf);
}

static void rm_rf(const char *dir) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
    system(cmd);
}

TEST(test_golden_tiny_blast) {
    char *wdir = make_tmpdir();
    ASSERT_NOT_NULL(wdir);
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    cfg.workdir = wdir;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    const char *refs[] = { SMR_DATA_DIR "/test_ref.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/test_read.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    int rc = smr_run(ctx, refs, 1, reads, 1, &out, &stats);
    ASSERT_EQ_INT(rc, SMR_OK);

    char produced[512];
    snprintf(produced, sizeof(produced), "%s/aligned.blast", wdir);
    ASSERT_TRUE(files_match(produced, SMR_GOLDEN_DIR "/tiny/aligned.blast", 0));

    smr_output_free(out);
    smr_ctx_destroy(ctx);
    rm_rf(wdir);
}

TEST(test_golden_tiny_fasta) {
    char *wdir = make_tmpdir();
    ASSERT_NOT_NULL(wdir);
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    cfg.workdir = wdir;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    const char *refs[] = { SMR_DATA_DIR "/test_ref.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/test_read.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    smr_run(ctx, refs, 1, reads, 1, &out, &stats);

    char produced[512];
    snprintf(produced, sizeof(produced), "%s/aligned.fa", wdir);
    ASSERT_TRUE(files_match(produced, SMR_GOLDEN_DIR "/tiny/aligned.fa", 0));

    smr_output_free(out);
    smr_ctx_destroy(ctx);
    rm_rf(wdir);
}

TEST(test_golden_tiny_sam) {
    char *wdir = make_tmpdir();
    ASSERT_NOT_NULL(wdir);
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    cfg.workdir = wdir;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    const char *refs[] = { SMR_DATA_DIR "/test_ref.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/test_read.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    smr_run(ctx, refs, 1, reads, 1, &out, &stats);

    char produced[512];
    snprintf(produced, sizeof(produced), "%s/aligned.sam", wdir);
    ASSERT_TRUE(files_match(produced, SMR_GOLDEN_DIR "/tiny/aligned.sam", 1));

    smr_output_free(out);
    smr_ctx_destroy(ctx);
    rm_rf(wdir);
}

TEST(test_golden_small_blast) {
    char *wdir = make_tmpdir();
    ASSERT_NOT_NULL(wdir);
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    cfg.workdir = wdir;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    const char *refs[] = { SMR_DATA_DIR "/silva-arc-16s-database-id95.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/set7_arc_bac_16S_database_match.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    int rc = smr_run(ctx, refs, 1, reads, 1, &out, &stats);
    ASSERT_EQ_INT(rc, SMR_OK);

    char produced[512];
    snprintf(produced, sizeof(produced), "%s/aligned.blast", wdir);
    ASSERT_TRUE(files_match(produced, SMR_GOLDEN_DIR "/small/aligned.blast", 0));

    smr_output_free(out);
    smr_ctx_destroy(ctx);
    rm_rf(wdir);
}

TEST(test_golden_small_fasta) {
    char *wdir = make_tmpdir();
    ASSERT_NOT_NULL(wdir);
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    cfg.workdir = wdir;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    const char *refs[] = { SMR_DATA_DIR "/silva-arc-16s-database-id95.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/set7_arc_bac_16S_database_match.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    smr_run(ctx, refs, 1, reads, 1, &out, &stats);

    char produced[512];
    snprintf(produced, sizeof(produced), "%s/aligned.fa", wdir);
    ASSERT_TRUE(files_match(produced, SMR_GOLDEN_DIR "/small/aligned.fa", 0));

    smr_output_free(out);
    smr_ctx_destroy(ctx);
    rm_rf(wdir);
}

TEST(test_golden_small_sam) {
    char *wdir = make_tmpdir();
    ASSERT_NOT_NULL(wdir);
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    cfg.workdir = wdir;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    const char *refs[] = { SMR_DATA_DIR "/silva-arc-16s-database-id95.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/set7_arc_bac_16S_database_match.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    smr_run(ctx, refs, 1, reads, 1, &out, &stats);

    char produced[512];
    snprintf(produced, sizeof(produced), "%s/aligned.sam", wdir);
    ASSERT_TRUE(files_match(produced, SMR_GOLDEN_DIR "/small/aligned.sam", 1));

    smr_output_free(out);
    smr_ctx_destroy(ctx);
    rm_rf(wdir);
}

TEST(test_golden_small_other) {
    char *wdir = make_tmpdir();
    ASSERT_NOT_NULL(wdir);
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    cfg.workdir = wdir;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    const char *refs[] = { SMR_DATA_DIR "/silva-arc-16s-database-id95.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/set7_arc_bac_16S_database_match.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    smr_run(ctx, refs, 1, reads, 1, &out, &stats);

    char produced[512];
    snprintf(produced, sizeof(produced), "%s/other.fa", wdir);
    ASSERT_TRUE(files_match(produced, SMR_GOLDEN_DIR "/small/other.fa", 0));

    smr_output_free(out);
    smr_ctx_destroy(ctx);
    rm_rf(wdir);
}

/* ---- Phase 8: Concurrent smr_run ---- */

struct thread_arg {
    int rc;
    uint64_t num_aligned;
};

static void *concurrent_worker(void *arg) {
    struct thread_arg *ta = (struct thread_arg *)arg;
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    const char *refs[] = { SMR_DATA_DIR "/test_ref.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/test_read.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    ta->rc = smr_run(ctx, refs, 1, reads, 1, &out, &stats);
    ta->num_aligned = out ? out->num_aligned : 0;
    smr_output_free(out);
    smr_ctx_destroy(ctx);
    return NULL;
}

TEST(test_concurrent_smr_run) {
    pthread_t t1, t2;
    struct thread_arg a1 = {0, 0}, a2 = {0, 0};
    pthread_create(&t1, NULL, concurrent_worker, &a1);
    pthread_create(&t2, NULL, concurrent_worker, &a2);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    ASSERT_EQ_INT(a1.rc, SMR_OK);
    ASSERT_EQ_INT(a2.rc, SMR_OK);
    ASSERT_EQ_U64(a1.num_aligned, 1);
    ASSERT_EQ_U64(a2.num_aligned, 1);
}

/* ---- Phase 9: Multi-threaded alignment ---- */

TEST(test_run_tiny_multithreaded) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 2;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    ASSERT_NOT_NULL(ctx);
    const char *refs[] = { SMR_DATA_DIR "/test_ref.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/test_read.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    int rc = smr_run(ctx, refs, 1, reads, 1, &out, &stats);
    ASSERT_EQ_INT(rc, SMR_OK);
    ASSERT_NOT_NULL(out);
    ASSERT_EQ_U64(out->num_reads, 1);
    ASSERT_EQ_U64(out->num_aligned, 1);
    smr_output_free(out);
    smr_ctx_destroy(ctx);
}

/* ---- Phase 10: Per-read output ---- */

TEST(test_run_tiny_per_read_output) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    ASSERT_NOT_NULL(ctx);
    const char *refs[] = { SMR_DATA_DIR "/test_ref.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/test_read.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    int rc = smr_run(ctx, refs, 1, reads, 1, &out, &stats);
    ASSERT_EQ_INT(rc, SMR_OK);
    ASSERT_NOT_NULL(out);
    ASSERT_EQ_U64(out->num_reads, 1);
    ASSERT_EQ_U64(out->num_aligned, 1);

    /* per-read arrays should be populated */
    ASSERT_NOT_NULL(out->read_ids);
    ASSERT_NOT_NULL(out->aligned);
    ASSERT_NOT_NULL(out->e_value);
    ASSERT_NOT_NULL(out->identity);
    ASSERT_NOT_NULL(out->coverage);
    ASSERT_NOT_NULL(out->ref_start);
    ASSERT_NOT_NULL(out->ref_end);
    ASSERT_NOT_NULL(out->cigar);

    ASSERT_STR_EQ(out->read_ids[0], "AB271211");
    ASSERT_EQ_INT(out->aligned[0], 1);
    ASSERT_TRUE(out->ref_start[0] == 1);
    ASSERT_TRUE(out->ref_end[0] == 1446);
    ASSERT_DOUBLE_NEAR(out->identity[0], 93.5, 1.0);
    ASSERT_DOUBLE_NEAR(out->coverage[0], 96.2, 1.0);
    ASSERT_NOT_NULL(out->cigar[0]);

    smr_output_free(out);
    smr_ctx_destroy(ctx);
}

TEST(test_run_small_per_read_output) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    ASSERT_NOT_NULL(ctx);
    const char *refs[] = { SMR_DATA_DIR "/silva-arc-16s-database-id95.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/set7_arc_bac_16S_database_match.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    int rc = smr_run(ctx, refs, 1, reads, 1, &out, &stats);
    ASSERT_EQ_INT(rc, SMR_OK);
    ASSERT_NOT_NULL(out);
    ASSERT_EQ_U64(out->num_reads, 6);
    ASSERT_EQ_U64(out->num_aligned, 4);

    ASSERT_NOT_NULL(out->read_ids);
    ASSERT_NOT_NULL(out->aligned);

    /* first 4 reads are aligned, last 2 are not */
    ASSERT_STR_EQ(out->read_ids[0], "BD.ERD505_1");
    ASSERT_EQ_INT(out->aligned[0], 1);
    ASSERT_STR_EQ(out->read_ids[1], "BD.NBS1076_0");
    ASSERT_EQ_INT(out->aligned[1], 1);
    ASSERT_STR_EQ(out->read_ids[2], "LD.Glosor1_17");
    ASSERT_EQ_INT(out->aligned[2], 1);
    ASSERT_STR_EQ(out->read_ids[3], "BD.ERD510_20");
    ASSERT_EQ_INT(out->aligned[3], 1);
    ASSERT_STR_EQ(out->read_ids[4], "random1");
    ASSERT_EQ_INT(out->aligned[4], 0);
    ASSERT_STR_EQ(out->read_ids[5], "random2");
    ASSERT_EQ_INT(out->aligned[5], 0);

    /* unaligned reads have ref_index == -1 */
    ASSERT_EQ_INT(out->ref_index[4], -1);
    ASSERT_EQ_INT(out->ref_index[5], -1);
    ASSERT_NULL(out->cigar[4]);
    ASSERT_NULL(out->cigar[5]);

    /* spot-check aligned read identity/coverage */
    ASSERT_DOUBLE_NEAR(out->identity[0], 90.7, 1.0);
    ASSERT_DOUBLE_NEAR(out->coverage[1], 100.0, 1.0);

    smr_output_free(out);
    smr_ctx_destroy(ctx);
}

/* ---- Phase 11: In-memory input (smr_run_seqs) ---- */

TEST(test_run_seqs_tiny) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    ASSERT_NOT_NULL(ctx);
    const char *refs[] = { SMR_DATA_DIR "/test_ref.fasta" };

    /* AB271211 sequence (same as test_read.fasta) — first 100 chars for a shorter test */
    smr_seq_t seqs[1];
    seqs[0].id = "AB271211";
    seqs[0].sequence =
        "TCCAACGCGTTGGGAGCTCTCCCATATGGTCGACCTGCAGGCGGCCGCACTAGTGATTAG"
        "AGTTTGATCCTGGCTCAGGATGAACGCTGGCGGCGTGCCTAACACATGCAAGTCGAACGG"
        "GAATCTTCGGATTCTAGTGGCGGACGGGTGAGTAACGCGTAAGAATCTAACTTCAGGACG"
        "GGGACAACAGTGGGAAACGACTGCTAATACCCGATGTGCCGCGAGGTGAAACCTAATTGG";
    seqs[0].quality = NULL;

    smr_output_t *out = NULL;
    smr_stats_t stats;
    int rc = smr_run_seqs(ctx, refs, 1, seqs, 1, &out, &stats);
    ASSERT_EQ_INT(rc, SMR_OK);
    ASSERT_NOT_NULL(out);
    ASSERT_EQ_U64(out->num_reads, 1);
    ASSERT_NOT_NULL(out->read_ids);
    ASSERT_STR_EQ(out->read_ids[0], "AB271211");

    smr_output_free(out);
    smr_ctx_destroy(ctx);
}

TEST(test_run_seqs_null_error) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    smr_context_t *ctx = smr_ctx_create(&cfg);
    ASSERT_NOT_NULL(ctx);
    const char *refs[] = { SMR_DATA_DIR "/test_ref.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    int rc = smr_run_seqs(ctx, refs, 1, NULL, 0, &out, &stats);
    ASSERT_TRUE(rc < 0);
    smr_ctx_destroy(ctx);
}

TEST(test_run_seqs_empty_seq_error) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    smr_context_t *ctx = smr_ctx_create(&cfg);
    ASSERT_NOT_NULL(ctx);
    const char *refs[] = { SMR_DATA_DIR "/test_ref.fasta" };
    smr_seq_t seqs[1];
    seqs[0].id = "test";
    seqs[0].sequence = "";
    seqs[0].quality = NULL;
    smr_output_t *out = NULL;
    smr_stats_t stats;
    int rc = smr_run_seqs(ctx, refs, 1, seqs, 1, &out, &stats);
    ASSERT_TRUE(rc < 0);
    smr_ctx_destroy(ctx);
}

TEST(test_run_seqs_quality_length_mismatch) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    smr_context_t *ctx = smr_ctx_create(&cfg);
    ASSERT_NOT_NULL(ctx);
    const char *refs[] = { SMR_DATA_DIR "/test_ref.fasta" };
    smr_seq_t seqs[1];
    seqs[0].id = "test";
    seqs[0].sequence = "ACGT";
    seqs[0].quality = "II";  /* too short */
    smr_output_t *out = NULL;
    smr_stats_t stats;
    int rc = smr_run_seqs(ctx, refs, 1, seqs, 1, &out, &stats);
    ASSERT_EQ_INT(rc, SMR_ERR_INVALID_CONFIG);
    smr_ctx_destroy(ctx);
}

TEST(test_run_seqs_paired_odd_count_error) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.paired = 1;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    ASSERT_NOT_NULL(ctx);
    const char *refs[] = { SMR_DATA_DIR "/test_ref.fasta" };
    smr_seq_t seqs[3];
    seqs[0].id = "r1"; seqs[0].sequence = "ACGT"; seqs[0].quality = NULL;
    seqs[1].id = "r2"; seqs[1].sequence = "TGCA"; seqs[1].quality = NULL;
    seqs[2].id = "r3"; seqs[2].sequence = "AAAA"; seqs[2].quality = NULL;
    smr_output_t *out = NULL;
    smr_stats_t stats;
    int rc = smr_run_seqs(ctx, refs, 1, seqs, 3, &out, &stats);
    ASSERT_EQ_INT(rc, SMR_ERR_INVALID_CONFIG);
    smr_ctx_destroy(ctx);
}

/* ---- Phase 12: ref_name output ---- */

TEST(test_run_tiny_ref_name) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    ASSERT_NOT_NULL(ctx);
    const char *refs[] = { SMR_DATA_DIR "/test_ref.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/test_read.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    int rc = smr_run(ctx, refs, 1, reads, 1, &out, &stats);
    ASSERT_EQ_INT(rc, SMR_OK);
    ASSERT_NOT_NULL(out);
    ASSERT_NOT_NULL(out->ref_name);
    ASSERT_NOT_NULL(out->ref_name[0]);
    /* golden: AB271211 aligned to Unc49508 */
    ASSERT_STR_EQ(out->ref_name[0], "Unc49508");
    smr_output_free(out);
    smr_ctx_destroy(ctx);
}

TEST(test_run_small_ref_name) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    ASSERT_NOT_NULL(ctx);
    const char *refs[] = { SMR_DATA_DIR "/silva-arc-16s-database-id95.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/set7_arc_bac_16S_database_match.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    int rc = smr_run(ctx, refs, 1, reads, 1, &out, &stats);
    ASSERT_EQ_INT(rc, SMR_OK);
    ASSERT_NOT_NULL(out);
    ASSERT_NOT_NULL(out->ref_name);
    /* golden: BD.ERD505_1 → EU602318, unaligned reads have NULL ref_name */
    ASSERT_NOT_NULL(out->ref_name[0]);
    ASSERT_STR_EQ(out->ref_name[0], "EU602318");
    ASSERT_NULL(out->ref_name[4]); /* random1 — unaligned */
    ASSERT_NULL(out->ref_name[5]); /* random2 — unaligned */
    smr_output_free(out);
    smr_ctx_destroy(ctx);
}

TEST(test_run_seqs_paired) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    cfg.paired = 1;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    ASSERT_NOT_NULL(ctx);
    const char *refs[] = { SMR_DATA_DIR "/silva-arc-16s-database-id95.fasta" };

    /* 2 interleaved pairs (4 sequences total) from the small dataset */
    smr_seq_t seqs[4];
    seqs[0].id = "BD.ERD505_1";
    seqs[0].sequence = "AACGTAGGTGGCAAGCGTTGTCCGGAATTACTGGGTGTAAAGGGAGCGCAGGCGGAAAAGCAAGTTGGACGTGAAATCTATGGGCTCAACCCATAGCGTG";
    seqs[0].quality = NULL;
    seqs[1].id = "BD.NBS1076_0";
    seqs[1].sequence = "TACGGAGGGTGCAAGCGTTAATCCGAATTACTGGGCGTAAAGCGCACGCAGGCGGTCTGTCAAGTCGGATGTGAAATCCACGGGCTCAACCTGG";
    seqs[1].quality = NULL;
    seqs[2].id = "LD.Glosor1_17";
    seqs[2].sequence = "TACGGAGGGTGCAAGCGTTAATCGGAATTACTGGGCGTAAAGCGCACGCAGGCGGTCTGTCAAGTCGGATGTGAAATCCCCGGGCTCAACCTGGGAACTG";
    seqs[2].quality = NULL;
    seqs[3].id = "BD.ERD510_20";
    seqs[3].sequence = "TACGGAGGGTGCAAGCGTTAATCGGAATTACTGGGCGTAAAGCGCACGCAGGCGGTCTGTCAAGTCGGATGTGAAATCCCCGGGCTCAACCTGGGAACTG";
    seqs[3].quality = NULL;

    smr_output_t *out = NULL;
    smr_stats_t stats;
    int rc = smr_run_seqs(ctx, refs, 1, seqs, 4, &out, &stats);
    ASSERT_EQ_INT(rc, SMR_OK);
    ASSERT_NOT_NULL(out);
    ASSERT_EQ_U64(out->num_reads, 4);
    ASSERT_NOT_NULL(out->read_ids);
    /* verify interleaved input order is preserved */
    ASSERT_STR_EQ(out->read_ids[0], "BD.ERD505_1");
    ASSERT_STR_EQ(out->read_ids[1], "BD.NBS1076_0");
    ASSERT_STR_EQ(out->read_ids[2], "LD.Glosor1_17");
    ASSERT_STR_EQ(out->read_ids[3], "BD.ERD510_20");

    smr_output_free(out);
    smr_ctx_destroy(ctx);
}

/* ---- Phase 13: strand / score / edit_distance output ---- */

TEST(test_run_tiny_strand_score_edit) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    ASSERT_NOT_NULL(ctx);
    const char *refs[]  = { SMR_DATA_DIR "/test_ref.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/test_read.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    int rc = smr_run(ctx, refs, 1, reads, 1, &out, &stats);
    ASSERT_EQ_INT(rc, SMR_OK);
    ASSERT_NOT_NULL(out);
    ASSERT_EQ_U64(out->num_reads, 1);
    ASSERT_EQ_U64(out->num_aligned, 1);

    /* arrays must be non-NULL */
    ASSERT_NOT_NULL(out->strand);
    ASSERT_NOT_NULL(out->score);
    ASSERT_NOT_NULL(out->edit_distance);

    /* golden: AB271211 FLAG=0 -> forward, AS:i=2430, NM:i=94 */
    ASSERT_EQ_INT(out->strand[0], 1);
    ASSERT_EQ_INT(out->score[0], 2430);
    ASSERT_EQ_INT(out->edit_distance[0], 94);

    smr_output_free(out);
    smr_ctx_destroy(ctx);
}

TEST(test_run_small_strand_score_edit) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    ASSERT_NOT_NULL(ctx);
    const char *refs[]  = { SMR_DATA_DIR "/silva-arc-16s-database-id95.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/set7_arc_bac_16S_database_match.fasta" };
    smr_output_t *out = NULL;
    smr_stats_t stats;
    int rc = smr_run(ctx, refs, 1, reads, 1, &out, &stats);
    ASSERT_EQ_INT(rc, SMR_OK);
    ASSERT_NOT_NULL(out);
    ASSERT_EQ_U64(out->num_reads, 6);
    ASSERT_EQ_U64(out->num_aligned, 4);

    ASSERT_NOT_NULL(out->strand);
    ASSERT_NOT_NULL(out->score);
    ASSERT_NOT_NULL(out->edit_distance);

    /* aligned reads: strand=1 (forward), golden scores and edit distances */
    ASSERT_EQ_INT(out->strand[0], 1);         /* BD.ERD505_1 */
    ASSERT_EQ_INT(out->score[0], 83);
    ASSERT_EQ_INT(out->edit_distance[0], 5);

    ASSERT_EQ_INT(out->strand[1], 1);         /* BD.NBS1076_0 */
    ASSERT_EQ_INT(out->score[1], 132);
    ASSERT_EQ_INT(out->edit_distance[1], 11);

    ASSERT_EQ_INT(out->strand[2], 1);         /* LD.Glosor1_17 */
    ASSERT_EQ_INT(out->score[2], 154);
    ASSERT_EQ_INT(out->edit_distance[2], 9);

    ASSERT_EQ_INT(out->strand[3], 1);         /* BD.ERD510_20 */
    ASSERT_EQ_INT(out->score[3], 154);
    ASSERT_EQ_INT(out->edit_distance[3], 9);

    /* unaligned reads: sentinel -1 for all three */
    ASSERT_EQ_INT(out->strand[4], -1);        /* random1 */
    ASSERT_EQ_INT(out->score[4], -1);
    ASSERT_EQ_INT(out->edit_distance[4], -1);

    ASSERT_EQ_INT(out->strand[5], -1);        /* random2 */
    ASSERT_EQ_INT(out->score[5], -1);
    ASSERT_EQ_INT(out->edit_distance[5], -1);

    smr_output_free(out);
    smr_ctx_destroy(ctx);
}

TEST(test_run_reverse_strand) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    ASSERT_NOT_NULL(ctx);
    const char *refs[] = { SMR_DATA_DIR "/test_ref.fasta" };

    /* reverse-complement of AB271211 — should align on reverse strand */
    smr_seq_t seqs[1];
    seqs[0].id = "AB271211_rc";
    seqs[0].sequence =
        "CCCCAGTCACTAGCCCTGCCTTAGGCATCCCCCTCCTTGCGGTTGAGGTAATGACTTCGGG"
        "CGTGACCAGCTTCCATGGTGTGACGGGCGGTGTGTACAAGGCCCGGGAACGAATTCACCG"
        "CCGTATGCTGACCGGCGATTACTAGCGATTCCTCCTTCATGCAGGCGAGTTGCAGCCTGC"
        "AATCTGAACTGAGGCCGGGTTTGCTGGGATTCGCTGGCTCTCGCAAGTTCGCTGCCCTTT"
        "GTCCCGACCATTGTAGTACGTGTGTCGCCCAAGACGTAAGGGGCATGCTGACTTGACGTC"
        "ATCCCCACCTTCCTCCGGTTTGTCACCGGCAGTCTCCTTAGAGTCCCCAACTTAATGCTGG"
        "CAACTAAGAACGAGGGTTGCGCTCGTTGCGGGACTTAACCCAACATCTCACGACACGAGC"
        "TGACGACAGCCATGCACCACCTGTGTTCGCGCTCCCGAAGGCACCCCCAGCTTTCACCAGG"
        "GTTCGCGACATGTCAAGTCTTGGTAAGGTTCTTCGCGTTGCATCGAATTAAACCACATAC"
        "TCCACCGCTTGTGCGGGCCCCCGTCAATTCCTTTGAGTTTCACACTTGCGTGCGTACTCC"
        "CCAGGCGGGATACTTAACGCGTTAGCTTCGGCACGGCTCGGGTCGATACAAGCCACGCCTA"
        "GTATCCATCGTTTACGGCTAGGACTACAGGGGTATCTAATCCCTTTCGCTCCCCTAGCTTT"
        "CGTCCCTGAGTGTCAGATACAGCCCAGTAGCACGCTTTCGCCACCGATGTTCTTCCCAATC"
        "TCTACGCATTTCACCGCTACACTGGGAATTCCTGCTACCCCTACTGCTCTCTAGTCTGCCA"
        "GTTTCCACCGCCTTTAGGTCGTTAAGCAACCTGATTTGACGGCAGACTTGGCTGACCACCT"
        "GCGGACGCTTTACGCCCAATAATTCCGGATAACGCTTGCCTCCCCCGTATTACCGCGGCTG"
        "CTGGCACGGAGTTAGCCGAGGCTGATTCCTCAAGTACCGTCAGAACTTCTTCCTTGAGAAA"
        "AGAGGTTTACAATCCAAAGACCTTCCTCCCTCACGCGGCGTTGCTCCGTCAGGCTTTCGCC"
        "CATTGCGGAAAATTCCCCACTGCTGCCTCCCGTAGGAGTCTGGGCCGTGTCTCAGTCCCAG"
        "TGTGGCTGCTCATCCTCTCAGACCAGCTACTGATCGTCGCCTTGGTAGGCTCTTACCCCAC"
        "CAACTAGCTAATCAGACGCAAGCTCCTCTTCAGGCCAATTAGGTTTCACCTCGCGGCACAT"
        "CGGGTATTAGCAGTCGTTTCCCACTGTTGTCCCCGTCCTGAAGTTAGATTCTTACGCGTTA"
        "CTCACCCGTCCGCCACTAGAATCCGAAGATTCCCGTTCGACTTGCATGTGTTAGGCACGCC"
        "GCCAGCGTTCATCCTGAGCCAGGATCAAACTCTAATCACTAGTGCGGCCGCCTGCAGGTCG"
        "ACCATATGGGAGAGCTCCCAACGCGTTGGA";
    seqs[0].quality = NULL;

    smr_output_t *out = NULL;
    smr_stats_t stats;
    int rc = smr_run_seqs(ctx, refs, 1, seqs, 1, &out, &stats);
    ASSERT_EQ_INT(rc, SMR_OK);
    ASSERT_NOT_NULL(out);
    ASSERT_EQ_U64(out->num_reads, 1);
    ASSERT_EQ_U64(out->num_aligned, 1);

    ASSERT_NOT_NULL(out->strand);
    ASSERT_NOT_NULL(out->score);
    ASSERT_NOT_NULL(out->edit_distance);

    /* reverse-complement read: strand must be 0 */
    ASSERT_EQ_INT(out->strand[0], 0);
    ASSERT_EQ_INT(out->score[0], 2430);
    ASSERT_EQ_INT(out->edit_distance[0], 94);

    smr_output_free(out);
    smr_ctx_destroy(ctx);
}

TEST(test_run_null_out_does_not_crash) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    cfg.num_threads = 1;
    smr_context_t *ctx = smr_ctx_create(&cfg);
    ASSERT_NOT_NULL(ctx);
    const char *refs[]  = { SMR_DATA_DIR "/test_ref.fasta" };
    const char *reads[] = { SMR_DATA_DIR "/test_read.fasta" };
    smr_stats_t stats;
    int rc = smr_run(ctx, refs, 1, reads, 1, NULL, &stats);
    ASSERT_EQ_INT(rc, SMR_OK);
    ASSERT_EQ_U64(stats.total_aligned, 1);
    smr_ctx_destroy(ctx);
}

TEST_MAIN_BEGIN()
    /* Phase 0 */
    RUN_TEST(test_config_init_sets_struct_size);
    RUN_TEST(test_config_struct_size_is_first_field);
    RUN_TEST(test_config_init_zeroes_pointers);
    RUN_TEST(test_version_returns_440);
    RUN_TEST(test_strerror_returns_string);
    RUN_TEST(test_output_free_null_safe);
    RUN_TEST(test_ctx_destroy_null_safe);
    /* Phase 1 */
    RUN_TEST(test_config_default_threads);
    RUN_TEST(test_config_default_num_alignments);
    RUN_TEST(test_config_default_match);
    RUN_TEST(test_config_default_mismatch);
    RUN_TEST(test_config_default_gap_open);
    RUN_TEST(test_config_default_gap_ext);
    RUN_TEST(test_config_default_score_N);
    RUN_TEST(test_config_default_evalue);
    RUN_TEST(test_config_default_seed_win_len);
    RUN_TEST(test_config_default_best);
    RUN_TEST(test_config_default_booleans_off);
    RUN_TEST(test_config_boolean_type_is_int32);
    RUN_TEST(test_config_explicit_width_types);
    /* Phase 2 */
    RUN_TEST(test_ctx_create_returns_non_null);
    RUN_TEST(test_ctx_create_null_config_returns_null);
    RUN_TEST(test_ctx_create_bad_struct_size_returns_null);
    RUN_TEST(test_ctx_create_multiple_independent);
    RUN_TEST(test_ctx_last_error_empty_initially);
    RUN_TEST(test_strerror_success_msg);
    RUN_TEST(test_strerror_invalid_config_msg);
    RUN_TEST(test_strerror_unknown_code);
    RUN_TEST(test_log_callback_receives_messages);
    RUN_TEST(test_last_error_set_after_smr_run);
    RUN_TEST(test_last_error_code_initially_zero);
    RUN_TEST(test_last_error_code_null_ctx);
    /* Phase 3 */
    RUN_TEST(test_two_contexts_create_destroy);
    /* Phase 4 */
    RUN_TEST(test_run_null_refs_returns_error);
    RUN_TEST(test_run_null_reads_returns_error);
    RUN_TEST(test_run_nonexistent_ref_returns_error);
    RUN_TEST(test_run_nonexistent_reads_returns_error);
    RUN_TEST(test_run_empty_ref_returns_error);
    RUN_TEST(test_last_error_descriptive_after_bad_input);
    RUN_TEST(test_run_bad_input_does_not_crash);
    /* Phase 5 */
    RUN_TEST(test_run_tiny_aligned_count);
    RUN_TEST(test_run_tiny_stats);
    RUN_TEST(test_run_small_aligned_count);
    RUN_TEST(test_run_output_free_after_run);
    RUN_TEST(test_run_multiple_sequential);
    /* Phase 6 */
    RUN_TEST(test_no_stdout_during_run);
    RUN_TEST(test_log_callback_fires_during_run);
    RUN_TEST(test_null_log_callback_silent);
    /* Phase 7: golden file comparison */
    RUN_TEST(test_golden_tiny_blast);
    RUN_TEST(test_golden_tiny_fasta);
    RUN_TEST(test_golden_tiny_sam);
    RUN_TEST(test_golden_small_blast);
    RUN_TEST(test_golden_small_fasta);
    RUN_TEST(test_golden_small_sam);
    RUN_TEST(test_golden_small_other);
    /* Phase 8: concurrent smr_run */
    RUN_TEST(test_concurrent_smr_run);
    /* Phase 9: multi-threaded alignment */
    RUN_TEST(test_run_tiny_multithreaded);
    /* Phase 10: per-read output */
    RUN_TEST(test_run_tiny_per_read_output);
    RUN_TEST(test_run_small_per_read_output);
    /* Phase 11: in-memory input */
    RUN_TEST(test_run_seqs_tiny);
    RUN_TEST(test_run_seqs_null_error);
    RUN_TEST(test_run_seqs_empty_seq_error);
    RUN_TEST(test_run_seqs_quality_length_mismatch);
    RUN_TEST(test_run_seqs_paired_odd_count_error);
    RUN_TEST(test_run_seqs_paired);
    /* Phase 12: ref_name output */
    RUN_TEST(test_run_tiny_ref_name);
    RUN_TEST(test_run_small_ref_name);
    /* Phase 13: strand / score / edit_distance */
    RUN_TEST(test_run_tiny_strand_score_edit);
    RUN_TEST(test_run_small_strand_score_edit);
    RUN_TEST(test_run_reverse_strand);
    RUN_TEST(test_run_null_out_does_not_crash);
TEST_MAIN_END()
