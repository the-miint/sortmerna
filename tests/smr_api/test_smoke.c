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
TEST_MAIN_END()
