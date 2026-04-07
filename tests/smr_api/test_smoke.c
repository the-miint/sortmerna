/*
 * test_smoke.c -- main test runner for smr_api
 * Compiled as C to verify smr_api.h is C-linkable.
 *
 * Single-TU design: all test files are #include'd here.
 */

#include "test_harness.h"
#include "smr_api.h"
#include <stddef.h> /* offsetof */

/* ---- Phase 0: Smoke tests ---- */

TEST(test_config_init_sets_struct_size) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    ASSERT_EQ_SZ(cfg.struct_size, sizeof(smr_config_t));
}

TEST(test_config_struct_size_is_first_field) {
    ASSERT_EQ_SZ(offsetof(smr_config_t, struct_size), 0);
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
    ASSERT_EQ_INT(cfg.score_N, 0);
}

TEST(test_config_default_evalue) {
    smr_config_t cfg;
    smr_config_init(&cfg);
    /* Runopts default is exactly -1.0 (unset sentinel), IEEE 754 representable */
    ASSERT_TRUE(cfg.evalue == -1.0);
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
TEST_MAIN_END()
