/*
 * smr_log.cpp -- thread-local log callback definitions for library/binary mode
 *
 * These are declared in common.hpp and used by the INFO/ERR/WARN macros.
 * In library mode (smr_api), smr_run() sets these before calling the pipeline.
 * In native binary mode, they remain nullptr and macros fall back to stdout/stderr.
 */
#include "common.hpp"

thread_local smr_log_fn smr_tl_log_callback = nullptr;
thread_local void* smr_tl_log_user_data = nullptr;
