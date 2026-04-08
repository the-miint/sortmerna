C Library API
=============

SortMeRNA provides a reentrant C API for embedding rRNA filtering into other
applications without spawning a subprocess. The API is defined in
``include/smr_api.h`` and links as a static library (``libsmr_api.a``).

The library never calls ``exit()``, ``abort()``, or ``assert()``. All output
is routed through a caller-provided log callback; stdout and stderr are
suppressed during pipeline execution.

.. contents:: On this page
   :local:
   :depth: 2

Quick start
-----------

.. code-block:: c

   #include "smr_api.h"

   int main(void) {
       /* 1. Configure */
       smr_config_t cfg;
       smr_config_init(&cfg);
       cfg.num_threads = 4;

       /* 2. Create context */
       smr_context_t *ctx = smr_ctx_create(&cfg);
       if (!ctx) return 1;

       /* 3. Run alignment */
       const char *refs[]  = { "silva-bac-16s.fasta" };
       const char *reads[] = { "sample.fastq" };
       smr_output_t *out = NULL;
       smr_stats_t stats;

       int rc = smr_run(ctx, refs, 1, reads, 1, &out, &stats);
       if (rc != SMR_OK) {
           fprintf(stderr, "Error: %s\n", smr_last_error(ctx));
           smr_ctx_destroy(ctx);
           return 1;
       }

       printf("Total reads: %llu\n", (unsigned long long)stats.total_reads);
       printf("Aligned:     %llu\n", (unsigned long long)out->num_aligned);

       /* 4. Clean up */
       smr_output_free(out);
       smr_ctx_destroy(ctx);
       return 0;
   }

Building
--------

The API library is built automatically when CMake tests are enabled::

   cmake -B build -DWITH_TESTS=ON -DCONCURRENTQUEUE_HOME=3rdparty/concurrentqueue
   cmake --build build --target smr_api

This produces ``libsmr_api.a`` which statically links ``smr_objs`` (the core
SortMeRNA object library). To link your application::

   gcc -o myapp myapp.c -Iinclude -Lbuild/src/smr_api -lsmr_api \
       -Lbuild/src/sortmerna -lsmr_objs \
       -lrocksdb -lz -lpthread -lstdc++ -lm -ldl

For CMake projects:

.. code-block:: cmake

   add_subdirectory(path/to/sortmerna/src/smr_api)
   target_link_libraries(myapp smr_api)


API reference
-------------

Configuration
#############

.. c:function:: void smr_config_init(smr_config_t *cfg)

   Initialize a configuration struct with sensible defaults. Must be called
   before modifying any fields. Sets ``struct_size`` for ABI version detection.

   After calling ``smr_config_init``, the struct is ready for immediate use
   without further modification.

.. c:type:: smr_config_t

   Configuration struct. The first field (``struct_size``) must always be set
   by ``smr_config_init`` -- never initialize this struct manually.

   **Threading:**

   ========================  ===========  ===========
   Field                     Type         Default
   ========================  ===========  ===========
   ``num_threads``           ``int32_t``  2
   ========================  ===========  ===========

   **Alignment scoring:**

   ========================  ===========  ===========
   Field                     Type         Default
   ========================  ===========  ===========
   ``match``                 ``int32_t``  2
   ``mismatch``              ``int32_t``  -3
   ``gap_open``              ``int32_t``  5
   ``gap_ext``               ``int32_t``  2
   ``score_N``               ``int32_t``  0
   ``evalue``                ``double``   -1.0 (off)
   ``seed_win_len``          ``uint32_t`` 18
   ``num_alignments``        ``uint32_t`` 1
   ========================  ===========  ===========

   **Boolean flags** (``int32_t``, 0 = off, nonzero = on):

   ========================  ===========
   Field                     Default
   ========================  ===========
   ``best``                  1 (on)
   ``paired``                0
   ``forward_only``          0
   ``reverse_only``          0
   ``full_search``           0
   ``fastx``                 0
   ``sam``                   0
   ``blast``                 0
   ``otu_map``               0
   ``denovo``                0
   ========================  ===========

   **Paths:**

   ========================  ===============  ===========
   Field                     Type             Default
   ========================  ===============  ===========
   ``workdir``               ``const char*``  NULL (auto temp dir)
   ========================  ===============  ===========

   When ``workdir`` is NULL, a temporary directory is created per ``smr_run``
   call and cleaned up automatically. When set, the caller is responsible for
   the directory lifecycle.

   **Logging:**

   ========================  =================================  ===========
   Field                     Type                               Default
   ========================  =================================  ===========
   ``log_callback``          ``void(*)(int,const char*,void*)`` NULL
   ``log_user_data``         ``void*``                          NULL
   ========================  =================================  ===========

   When ``log_callback`` is NULL, the library operates silently. When set,
   all informational, warning, and error messages are routed through the
   callback. The ``log_user_data`` pointer is passed through unchanged.

   Both ``workdir`` and ``log_user_data`` are borrowed pointers -- the pointee
   must outlive the context.


Context lifecycle
#################

.. c:function:: smr_context_t* smr_ctx_create(const smr_config_t *cfg)

   Create a new context from a configuration struct. Returns an opaque pointer
   on success, or NULL on failure (invalid config or allocation failure).

   The config is copied into the context -- the caller may free or reuse the
   ``smr_config_t`` struct immediately after this call.

   :param cfg: Pointer to an initialized config struct. Must not be NULL.
               ``cfg->struct_size`` must equal ``sizeof(smr_config_t)``.
   :returns: Opaque context pointer, or NULL on failure.

.. c:function:: void smr_ctx_destroy(smr_context_t *ctx)

   Destroy a context and free all associated resources. Passing NULL is safe
   (no-op). The context must not be used after this call.


Error reporting
###############

.. c:function:: const char* smr_strerror(int code)

   Return a static string describing an error code category.

   ================================  =============================
   Code                              Message
   ================================  =============================
   ``SMR_OK`` (0)                    ``"Success"``
   ``SMR_ERR_INVALID_CONFIG`` (-1)   ``"Invalid configuration"``
   ``SMR_ERR_ALLOC`` (-2)            ``"Memory allocation failed"``
   ``SMR_ERR_IO`` (-3)               ``"I/O error"``
   ``SMR_ERR_INDEX`` (-4)            ``"Index error"``
   ``SMR_ERR_ALIGN`` (-5)            ``"Alignment error"``
   ``SMR_ERR_NOT_IMPLEMENTED`` (-99) ``"Not implemented"``
   ================================  =============================

.. c:function:: const char* smr_last_error(const smr_context_t *ctx)

   Return a detailed, context-specific error message from the most recent
   failing call. Returns an empty string if no error has occurred or if
   ``ctx`` is NULL.

.. c:function:: int smr_last_error_code(const smr_context_t *ctx)

   Return the numeric error code from the most recent failing call.
   Returns ``SMR_OK`` if no error has occurred. Returns
   ``SMR_ERR_INVALID_CONFIG`` if ``ctx`` is NULL.


Computation
###########

.. c:function:: int smr_run(smr_context_t *ctx, const char **ref_paths, int32_t num_refs, const char **read_paths, int32_t num_reads, smr_output_t **out, smr_stats_t *stats)

   Run the SortMeRNA alignment pipeline.

   :param ctx: Context created by ``smr_ctx_create``.
   :param ref_paths: Array of reference FASTA file paths.
   :param num_refs: Number of reference files (must be > 0).
   :param read_paths: Array of reads file paths (FASTA or FASTQ, plain or gzipped).
   :param num_reads: Number of reads files (must be > 0).
   :param out: Pointer to receive the output struct. May be NULL if only stats
               are needed. The caller must free the output with ``smr_output_free``.
   :param stats: Pointer to receive run statistics. May be NULL.
   :returns: ``SMR_OK`` on success, or a negative error code on failure.

   The function validates all inputs before running: NULL checks, file
   existence, and empty file detection. On error, ``smr_last_error(ctx)``
   provides a descriptive message.

   A context may be reused for multiple sequential ``smr_run`` calls. Each
   call produces an independent output that must be freed separately.

.. c:type:: smr_output_t

   Alignment results. Library-allocated; call ``smr_output_free`` when done.
   The output is independent of the context and remains valid after
   ``smr_ctx_destroy``.

   ==================  ================  ===========================================
   Field               Type              Description
   ==================  ================  ===========================================
   ``num_reads``       ``uint64_t``      Total number of input reads
   ``num_aligned``     ``uint64_t``      Number of reads with at least one alignment
   ``read_ids``        ``const char**``  Read identifiers (reserved, currently NULL)
   ``aligned``         ``int32_t*``      0/1 per read (reserved, currently NULL)
   ``ref_index``       ``int32_t*``      Reference index (reserved, currently NULL)
   ``e_value``         ``double*``       E-value per read (reserved, currently NULL)
   ``identity``        ``double*``       Percent identity (reserved, currently NULL)
   ``coverage``        ``double*``       Query coverage (reserved, currently NULL)
   ``ref_start``       ``int32_t*``      1-based ref start (reserved, currently NULL)
   ``ref_end``         ``int32_t*``      1-based ref end (reserved, currently NULL)
   ``cigar``           ``const char**``  CIGAR string (reserved, currently NULL)
   ==================  ================  ===========================================

.. c:type:: smr_stats_t

   Summary statistics from a run. Value-only struct -- no free required.

   =====================  ==============  ==========================================
   Field                  Type            Description
   =====================  ==============  ==========================================
   ``total_reads``        ``uint64_t``    Total reads in input
   ``total_aligned``      ``uint64_t``    Reads passing E-value threshold
   ``total_id_cov_pass``  ``uint64_t``    Reads passing both identity and coverage
   ``total_denovo``       ``uint64_t``    De novo reads (failing ID and coverage)
   ``min_read_len``       ``uint32_t``    Shortest read length
   ``max_read_len``       ``uint32_t``    Longest read length
   ``wall_time_sec``      ``double``      Wall clock time in seconds
   =====================  ==============  ==========================================

.. c:function:: void smr_output_free(smr_output_t *out)

   Free an output struct and all library-owned memory within it.
   Passing NULL is safe (no-op).


Version
#######

.. c:function:: const char* smr_version(void)

   Return the SortMeRNA version string (e.g., ``"4.4.0"``).


Design notes
------------

ABI stability
#############

The ``smr_config_t`` struct uses ``struct_size`` as its first field for ABI
version detection. ``smr_ctx_create`` validates that the caller's struct size
matches the library's. This allows the library to detect mismatches when a
caller was compiled against a different version of the header.

All boolean fields use ``int32_t`` (not ``bool``) for consistent sizing across
C and C++ compilers. Integer fields use explicit-width types from
``<stdint.h>``.

Opaque context
##############

The ``smr_context_t`` type is an opaque pointer. Internal fields are hidden
from callers, allowing the library to change its internal layout without
breaking ABI. Contexts are created with ``smr_ctx_create`` and destroyed with
``smr_ctx_destroy``.

Error handling
##############

The library uses a two-level error reporting scheme:

1. **Error codes** (return values): negative integers for errors, zero for
   success. Use ``smr_strerror`` for category descriptions.
2. **Detailed messages** (per-context): ``smr_last_error`` returns a
   context-specific message with file paths, parameter values, or exception
   details from the most recent failing call.

The library never calls ``exit()``, ``abort()``, or ``assert()``. All errors
are propagated to the caller via return codes.

I/O isolation
#############

The library suppresses stdout/stderr output using two mechanisms:

1. **Thread-local log routing**: All internal logging macros (INFO, ERR, WARN)
   check a thread-local callback pointer set by ``smr_run``. When set,
   messages route through the caller's ``log_callback``. When NULL (native
   binary mode), they fall back to stdout/stderr.

2. **fd-level suppression**: As a secondary defense against direct
   ``std::cout`` / ``std::cerr`` writes that bypass the macros, ``smr_run``
   temporarily redirects file descriptors 1 and 2 to ``/dev/null`` via
   ``dup2``. This is process-wide and not thread-safe for concurrent
   ``smr_run`` calls.

Log levels passed to the callback:

=================  =====
Constant           Value
=================  =====
``SMR_LOG_DEBUG``  0
``SMR_LOG_INFO``   1
``SMR_LOG_WARN``   2
``SMR_LOG_ERROR``  3
=================  =====

Thread safety
#############

``smr_run`` is serialized by a process-level mutex. It is safe to call from
multiple threads with independent contexts -- calls will execute sequentially.
Context creation, destruction, and error queries on *different* contexts are
thread-safe and do not acquire the mutex. Concurrent access to the *same*
context from multiple threads is not supported.

The mutex exists because stdout/stderr suppression uses ``dup2``, which is
process-wide. The log callback routing itself uses thread-locals and is
lock-free.

Memory management
#################

- ``smr_ctx_create`` allocates the context with ``malloc``.
- ``smr_ctx_destroy`` frees it with ``free``. Passing NULL is safe.
- ``smr_run`` allocates the output struct with ``calloc``.
- ``smr_output_free`` frees the output and all library-owned arrays within it.
  Passing NULL is safe.
- The output is independent of the context and remains valid after
  ``smr_ctx_destroy``.
- Borrowed pointers in ``smr_config_t`` (``workdir``, ``log_user_data``) are
  not copied as strings -- the pointee must outlive the context.
