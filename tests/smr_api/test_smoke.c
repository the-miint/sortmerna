/*
 * test_smoke.c -- Phase 0 smoke test for smr_api
 * Compiled as C to verify smr_api.h is C-linkable.
 *
 * Single-TU design: all test files are #include'd here.
 */

#include "test_harness.h"
#include "smr_api.h"
#include <stddef.h> /* offsetof */

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

TEST_MAIN_BEGIN()
    RUN_TEST(test_config_init_sets_struct_size);
    RUN_TEST(test_config_struct_size_is_first_field);
    RUN_TEST(test_config_init_zeroes_pointers);
    RUN_TEST(test_version_returns_440);
    RUN_TEST(test_strerror_returns_string);
    RUN_TEST(test_output_free_null_safe);
    RUN_TEST(test_ctx_destroy_null_safe);
TEST_MAIN_END()
