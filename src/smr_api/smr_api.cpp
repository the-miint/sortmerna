/*
 * smr_api.cpp -- SortMeRNA reentrant C API implementation
 */

#include "smr_api.h"
#include "version.h"
#include "options.hpp"
#include "index.hpp"
#include "kvdb.hpp"
#include "readfeed.hpp"
#include "readstats.hpp"
#include "refstats.hpp"
#include "references.hpp"
#include "read.hpp"
#include "processor.hpp"
#include "output.hpp"
#include "summary.hpp"
#include "otumap.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <atomic>
#include <mutex>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <stdexcept>
#include <cmath>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

/* stringification helpers for version macros */
#define SMR_STRINGIFY2(x) #x
#define SMR_STRINGIFY(x) SMR_STRINGIFY2(x)

/* process-level atomic counter for unique workdir names across all contexts */
static std::atomic<int> g_run_counter{0};

/* process-level mutex serializing smr_run calls. Required because the
 * dup2-based fd suppression is process-wide and not thread-safe. The
 * mutex is uncontended in the common single-threaded case. */
static std::mutex g_run_mutex;

/* thread-local log routing defined in smr_log.cpp (part of smr_objs),
 * declared in common.hpp. Set by smr_run(), read by INFO/ERR/WARN macros. */

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

/* RAII guard for temp workdir cleanup */
class WorkdirGuard {
    std::string path_;
    bool owned_;
public:
    WorkdirGuard(const std::string &path, bool owned)
        : path_(path), owned_(owned) {}
    ~WorkdirGuard() {
        if (owned_) {
            std::error_code ec;
            std::filesystem::remove_all(path_, ec);
        }
    }
    void release() { owned_ = false; }
};

/* RAII guard for stdout/stderr suppression via dup2.
 * Best-effort: if dup/open fails, silently proceeds without suppression.
 * NOTE: dup2 is process-wide — not safe for concurrent smr_run() calls
 * from multiple threads. The macro-based log routing (thread-local) is
 * the primary isolation mechanism; this is a secondary defense against
 * direct std::cout writes that bypass the macros. */
class FdRedirectGuard {
    int saved_out_, saved_err_;
public:
    FdRedirectGuard() : saved_out_(-1), saved_err_(-1) {
        fflush(stdout); fflush(stderr);
        saved_out_ = dup(STDOUT_FILENO);
        saved_err_ = dup(STDERR_FILENO);
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            if (saved_out_ >= 0) dup2(devnull, STDOUT_FILENO);
            if (saved_err_ >= 0) dup2(devnull, STDERR_FILENO);
            close(devnull);
        } else {
            /* open failed — clean up saved fds to avoid leak */
            if (saved_out_ >= 0) { close(saved_out_); saved_out_ = -1; }
            if (saved_err_ >= 0) { close(saved_err_); saved_err_ = -1; }
        }
    }
    ~FdRedirectGuard() {
        fflush(stdout); fflush(stderr);
        if (saved_out_ >= 0) { dup2(saved_out_, STDOUT_FILENO); close(saved_out_); }
        if (saved_err_ >= 0) { dup2(saved_err_, STDERR_FILENO); close(saved_err_); }
    }
};

/* RAII guard for thread-local log callback */
class LogRouteGuard {
public:
    LogRouteGuard(smr_log_fn cb, void *ud) {
        smr_tl_log_callback = cb;
        smr_tl_log_user_data = ud;
    }
    ~LogRouteGuard() {
        smr_tl_log_callback = nullptr;
        smr_tl_log_user_data = nullptr;
    }
};

/* --- Configuration --- */

void smr_config_init(smr_config_t *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->struct_size = (uint32_t)sizeof(*cfg);

    /* threading */
    cfg->num_threads = 2;          /* Runopts::num_proc_thread */

    /* alignment scoring -- matches Runopts defaults */
    cfg->match    =  2;            /* Runopts::match */
    cfg->mismatch = -3;            /* Runopts::mismatch */
    cfg->gap_open =  5;            /* Runopts::gap_open */
    cfg->gap_ext  =  2;            /* Runopts::gap_extension */
    cfg->score_N  = -3;            /* Runopts default: equals mismatch */
    cfg->evalue   = -1.0;          /* Runopts::evalue (-1 = unset) */

    /* indexing */
    cfg->seed_win_len   = 18;      /* Runopts::seed_win_len */
    cfg->num_alignments =  1;      /* Runopts::num_alignments */

    /* boolean flags */
    cfg->best = 1;                 /* Runopts::is_best */
}

/* --- Context lifecycle --- */

smr_context_t *smr_ctx_create(const smr_config_t *cfg) {
    if (!cfg) return nullptr;
    if (cfg->struct_size != (uint32_t)sizeof(smr_config_t)) return nullptr;

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

/* --- Computation helpers --- */

static bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static bool file_is_empty(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return true;
    return st.st_size == 0;
}

/*
 * Build an argv vector from smr_config_t + input paths.
 * Always emits all parameters unconditionally to avoid coupling
 * with Runopts internal defaults.
 */
static std::vector<std::string> build_argv(
    const smr_config_t &cfg,
    const char **ref_paths, int32_t num_refs,
    const char **read_paths, int32_t num_reads,
    const std::string &workdir)
{
    std::vector<std::string> args;
    args.push_back("sortmerna");

    for (int32_t i = 0; i < num_refs; i++) {
        args.push_back("--ref");
        args.push_back(ref_paths[i]);
    }
    for (int32_t i = 0; i < num_reads; i++) {
        args.push_back("--reads");
        args.push_back(read_paths[i]);
    }

    args.push_back("--workdir");
    args.push_back(workdir);
    args.push_back("--aligned");
    args.push_back(workdir + "/aligned");
    args.push_back("--other");
    args.push_back(workdir + "/other");

    args.push_back("--threads");
    args.push_back(std::to_string(cfg.num_threads));
    args.push_back("--num_alignments");
    args.push_back(std::to_string(cfg.num_alignments));

    /* always produce BLAST and SAM output for structured result extraction */
    args.push_back("--blast");
    args.push_back("1 cigar qcov");
    args.push_back("--fastx");
    args.push_back("--sam");

    /* always emit all scoring parameters */
    args.push_back("--match");
    args.push_back(std::to_string(cfg.match));
    args.push_back("--mismatch");
    args.push_back(std::to_string(cfg.mismatch));
    args.push_back("--gap_open");
    args.push_back(std::to_string(cfg.gap_open));
    args.push_back("--gap_ext");
    args.push_back(std::to_string(cfg.gap_ext));
    args.push_back("-N");
    args.push_back(std::to_string(cfg.score_N));
    args.push_back("-L");
    args.push_back(std::to_string(cfg.seed_win_len));

    if (cfg.evalue >= 0.0) {
        args.push_back("-e");
        args.push_back(std::to_string(cfg.evalue));
    }

    if (cfg.forward_only) args.push_back("-F");
    if (cfg.reverse_only) args.push_back("-R");
    if (!cfg.best) args.push_back("--no-best");
    if (cfg.full_search) args.push_back("--full_search");
    if (cfg.paired) args.push_back("--paired_in");

    return args;
}

/*
 * Build a CIGAR string from an s_align2's packed cigar vector + read coords.
 * Matches the format produced by report_blast.cpp.
 */
static std::string build_cigar_string(const s_align2 &align, uint32_t readlen) {
    std::string cigar;
    if (align.read_begin1 != 0)
        cigar += std::to_string(align.read_begin1) + "S";
    for (uint32_t c = 0; c < align.cigar.size(); ++c) {
        uint32_t letter = 0xf & align.cigar[c];
        uint32_t length = (0xfffffff0 & align.cigar[c]) >> 4;
        cigar += std::to_string(length);
        if (letter == 0) cigar += "M";
        else if (letter == 1) cigar += "I";
        else cigar += "D";
    }
    auto end_mask = readlen - align.read_end1 - 1;
    if (end_mask > 0)
        cigar += std::to_string(end_mask) + "S";
    return cigar;
}

/*
 * Extract the read identifier (first token of header, without > or @).
 */
static std::string extract_read_id(const std::string &header) {
    size_t start = (header.size() > 0 && (header[0] == '>' || header[0] == '@')) ? 1 : 0;
    auto end = header.find_first_of(" \t", start);
    return header.substr(start, end - start);
}

/*
 * Populate per-read output arrays by iterating through readfeed and loading
 * alignment results from kvdb. Loads references to compute %ID, %COV, E-value
 * (same pattern as the report and denovo_stats phases).
 */
static bool populate_per_read_output(smr_output_t *o,
                                     Readfeed &readfeed,
                                     Readstats &readstats,
                                     KeyValueDatabase &kvdb,
                                     Runopts &opts) {
    uint64_t n = o->num_reads;
    if (n == 0) return true;

    o->read_ids   = static_cast<const char**>(calloc(n, sizeof(char*)));
    o->aligned    = static_cast<int32_t*>(calloc(n, sizeof(int32_t)));
    o->ref_index  = static_cast<int32_t*>(calloc(n, sizeof(int32_t)));
    o->e_value    = static_cast<double*>(calloc(n, sizeof(double)));
    o->identity   = static_cast<double*>(calloc(n, sizeof(double)));
    o->coverage   = static_cast<double*>(calloc(n, sizeof(double)));
    o->ref_start  = static_cast<int32_t*>(calloc(n, sizeof(int32_t)));
    o->ref_end    = static_cast<int32_t*>(calloc(n, sizeof(int32_t)));
    o->cigar      = static_cast<const char**>(calloc(n, sizeof(char*)));

    if (!o->read_ids || !o->aligned || !o->ref_index || !o->e_value ||
        !o->identity || !o->coverage || !o->ref_start || !o->ref_end || !o->cigar)
        return false;

    /* defaults for all reads */
    for (uint64_t i = 0; i < n; i++) {
        o->ref_index[i] = -1;
    }

    Refstats refstats(opts, readstats);
    References refs;
    unsigned num_splits = readfeed.num_splits > 0 ? readfeed.num_splits : 1;

    /*
     * Helper lambda: iterate all splits in round-robin input order.
     * Reads were distributed: input read i → split (i % num_splits), position (i / num_splits).
     * To reconstruct input order: pull one read from each split in turn.
     */
    auto for_each_read = [&](auto callback) {
        readfeed.init_reading();
        uint64_t out_idx = 0;
        bool any_remaining = true;
        while (any_remaining && out_idx < n) {
            any_remaining = false;
            for (unsigned s = 0; s < num_splits && out_idx < n; ++s) {
                Read rd;
                if (readfeed.next(static_cast<int>(s), rd)) {
                    any_remaining = true;
                    callback(out_idx, rd);
                    ++out_idx;
                }
            }
        }
    };

    /* Pass 1: collect read IDs and basic alignment info from kvdb (no refs needed) */
    uint64_t aligned_count = 0;
    for_each_read([&](uint64_t idx, Read &read) {
        read.init(opts);
        read.load_db(kvdb);

        std::string rid = extract_read_id(read.header);
        o->read_ids[idx] = strdup(rid.c_str());

        if (read.is_hit && !read.alignment.alignv.empty()) {
            const auto &align = read.alignment.alignv[0];
            o->aligned[idx]   = 1;
            o->ref_index[idx] = static_cast<int32_t>(align.index_num);
            o->ref_start[idx] = align.ref_begin1 + 1;
            o->ref_end[idx]   = align.ref_end1 + 1;

            /* E-value: K * m * n * exp(-λ * S) */
            o->e_value[idx] = static_cast<double>(refstats.gumbel[align.index_num].second)
                * refstats.full_ref[align.index_num]
                * refstats.full_read[align.index_num]
                * std::exp(-refstats.gumbel[align.index_num].first * align.score1);

            /* CIGAR string */
            uint32_t readlen = static_cast<uint32_t>(read.sequence.size());
            o->cigar[idx] = strdup(build_cigar_string(align, readlen).c_str());

            /* coverage from alignment coordinates (no refs needed) */
            if (readlen > 0) {
                o->coverage[idx] = static_cast<double>(align.read_end1 - align.read_begin1 + 1)
                                 / readlen * 100.0;
            }

            ++aligned_count;
        }
    });
    o->num_aligned = aligned_count;

    /* Pass 2: compute %ID for aligned reads (requires loaded refs for sequence comparison) */
    for (uint16_t ref_idx = 0; ref_idx < opts.indexfiles.size(); ++ref_idx) {
        for (uint16_t idx_part = 0; idx_part < refstats.num_index_parts[ref_idx]; ++idx_part) {
            refs.load(ref_idx, idx_part, opts, refstats);

            for_each_read([&](uint64_t ri, Read &rd) {
                if (o->aligned[ri] == 1) {
                    rd.init(opts);
                    rd.load_db(kvdb);
                    if (!rd.alignment.alignv.empty()) {
                        const auto &al = rd.alignment.alignv[0];
                        if (al.index_num == ref_idx && al.part == idx_part) {
                            if (rd.is03) rd.flip34();
                            auto mgm = rd.calc_miss_gap_match(refs, al);
                            o->identity[ri] = std::get<3>(mgm) * 100.0;
                            o->coverage[ri] = std::get<4>(mgm) * 100.0;
                        }
                    }
                }
            });

            refs.unload();
        }
    }

    return true;
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

    /* serialize smr_run calls — dup2 fd suppression is process-wide */
    std::lock_guard<std::mutex> run_lock(g_run_mutex);

    auto wall_start = std::chrono::high_resolution_clock::now();

    /* create unique temporary workdir using process-level atomic counter */
    std::string workdir;
    bool workdir_is_temp = (ctx->config.workdir == nullptr);
    if (!workdir_is_temp) {
        workdir = ctx->config.workdir;
    } else {
        int run_id = g_run_counter.fetch_add(1);
        std::ostringstream ss;
        ss << "/tmp/smr_api_" << getpid() << "_" << run_id;
        workdir = ss.str();
    }

    /* RAII guards — C++ destroys in reverse declaration order, so:
     *   declared first:  WorkdirGuard  → destroyed last  (cleanup temp dir)
     *   declared second: LogRouteGuard → destroyed second (clear thread-local)
     *   declared third:  FdRedirectGuard → destroyed first (restore fds)
     * This means fds are restored while log routing is still active,
     * so any teardown logging still has somewhere to go. */
    WorkdirGuard wdguard(workdir, workdir_is_temp);
    LogRouteGuard loguard(ctx->config.log_callback, ctx->config.log_user_data);
    FdRedirectGuard fdguard;

    try {
        /* build argv and construct Runopts */
        auto args = build_argv(ctx->config, ref_paths, num_refs, read_paths, num_reads, workdir);
        std::vector<char*> argv_ptrs;
        for (auto &a : args) argv_ptrs.push_back(const_cast<char*>(a.c_str()));
        argv_ptrs.push_back(nullptr);

        bool dryrun = false;
        Runopts opts(static_cast<int>(argv_ptrs.size() - 1), argv_ptrs.data(), dryrun);

        ctx_log(ctx, SMR_LOG_INFO, "pipeline starting: %d refs, %d reads", num_refs, num_reads);

        /* run the alignment pipeline (same as main.cpp) */
        Index index(opts);

        KeyValueDatabase kvdb(opts.kvdbdir.string());
        Readfeed readfeed(opts.feed_type, opts.readfiles, opts.num_proc_thread, opts.readb_dir, opts.is_paired);
        Readstats readstats(readfeed.num_reads_tot, readfeed.length_all,
                            readfeed.min_read_len, readfeed.max_read_len, kvdb, opts);

        /* align + report */
        align(readfeed, readstats, index, kvdb, opts);
        writeSummary(readstats, opts);
        writeReports(readfeed, readstats, kvdb, opts);

        ctx_log(ctx, SMR_LOG_INFO, "pipeline complete");

        /* populate stats from Readstats (authoritative source) */
        if (stats) {
            memset(stats, 0, sizeof(*stats));
            stats->total_reads = readstats.all_reads_count;
            stats->total_aligned = readstats.num_aligned.load();
            stats->total_id_cov_pass = readstats.n_yid_ycov.load();
            stats->total_denovo = readstats.num_denovo.load();
            stats->min_read_len = readstats.min_read_len;
            stats->max_read_len = readstats.max_read_len;
            auto wall_end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed = wall_end - wall_start;
            stats->wall_time_sec = elapsed.count();
        }

        /* populate output */
        if (out) {
            auto *o = static_cast<smr_output_t *>(calloc(1, sizeof(smr_output_t)));
            if (!o) {
                set_error(ctx, SMR_ERR_ALLOC, "failed to allocate smr_output_t");
                return SMR_ERR_ALLOC;
            }

            o->num_reads = readstats.all_reads_count;

            if (!populate_per_read_output(o, readfeed, readstats, kvdb, opts)) {
                smr_output_free(o);
                set_error(ctx, SMR_ERR_ALLOC, "failed to allocate per-read output arrays");
                return SMR_ERR_ALLOC;
            }

            *out = o;
        }

        /* RAII guards handle cleanup on return */
        set_error(ctx, SMR_OK, "");
        return SMR_OK;

    } catch (const smr_exit_requested &) {
        set_error(ctx, SMR_ERR_INVALID_CONFIG, "unexpected --help/--version in library context");
        return SMR_ERR_INVALID_CONFIG;
    } catch (const std::exception &e) {
        set_error(ctx, SMR_ERR_ALIGN, "%s", e.what());
        return SMR_ERR_ALIGN;
    } catch (...) {
        set_error(ctx, SMR_ERR_ALIGN, "unknown exception in pipeline");
        return SMR_ERR_ALIGN;
    }
}

int smr_run_seqs(smr_context_t *ctx,
                 const char **ref_paths, int32_t num_refs,
                 const smr_seq_t *seqs, int32_t num_seqs,
                 smr_output_t **out,
                 smr_stats_t *stats) {
    if (!ctx) return SMR_ERR_INVALID_CONFIG;

    if (!ref_paths || num_refs <= 0) {
        set_error(ctx, SMR_ERR_INVALID_CONFIG, "ref_paths is NULL or num_refs <= 0");
        return SMR_ERR_INVALID_CONFIG;
    }
    if (!seqs || num_seqs <= 0) {
        set_error(ctx, SMR_ERR_INVALID_CONFIG, "seqs is NULL or num_seqs <= 0");
        return SMR_ERR_INVALID_CONFIG;
    }

    /* validate ref file existence */
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

    /* validate sequences */
    for (int32_t i = 0; i < num_seqs; i++) {
        if (!seqs[i].id || !seqs[i].sequence) {
            set_error(ctx, SMR_ERR_INVALID_CONFIG, "seq[%d] has NULL id or sequence", i);
            return SMR_ERR_INVALID_CONFIG;
        }
        if (seqs[i].sequence[0] == '\0') {
            set_error(ctx, SMR_ERR_INVALID_CONFIG, "seq[%d] has empty sequence", i);
            return SMR_ERR_INVALID_CONFIG;
        }
    }

    std::lock_guard<std::mutex> run_lock(g_run_mutex);
    auto wall_start = std::chrono::high_resolution_clock::now();

    std::string workdir;
    bool workdir_is_temp = (ctx->config.workdir == nullptr);
    if (!workdir_is_temp) {
        workdir = ctx->config.workdir;
    } else {
        int run_id = g_run_counter.fetch_add(1);
        std::ostringstream ss;
        ss << "/tmp/smr_api_" << getpid() << "_" << run_id;
        workdir = ss.str();
    }

    WorkdirGuard wdguard(workdir, workdir_is_temp);
    LogRouteGuard loguard(ctx->config.log_callback, ctx->config.log_user_data);
    FdRedirectGuard fdguard;

    try {
        /* Runopts requires --reads pointing to a valid file for option parsing
         * and format detection. Write a minimal placeholder matching the actual
         * format. The real reads are served from the MEMORY-mode Readfeed below;
         * this file is only touched by Runopts validation and is cleaned up
         * by WorkdirGuard. */
        std::filesystem::create_directories(workdir);
        bool has_qual = (seqs[0].quality != nullptr);
        std::string placeholder_ext = has_qual ? ".fq" : ".fa";
        std::string placeholder_reads = workdir + "/placeholder_reads" + placeholder_ext;
        {
            std::ofstream ofs(placeholder_reads);
            if (has_qual)
                ofs << "@placeholder\nA\n+\nI\n";
            else
                ofs << ">placeholder\nA\n";
        }
        const char *dummy_read_paths[] = { placeholder_reads.c_str() };

        auto args = build_argv(ctx->config, ref_paths, num_refs, dummy_read_paths, 1, workdir);
        std::vector<char*> argv_ptrs;
        for (auto &a : args) argv_ptrs.push_back(const_cast<char*>(a.c_str()));
        argv_ptrs.push_back(nullptr);

        bool dryrun = false;
        Runopts opts(static_cast<int>(argv_ptrs.size() - 1), argv_ptrs.data(), dryrun);

        ctx_log(ctx, SMR_LOG_INFO, "pipeline starting (in-memory): %d refs, %d seqs", num_refs, num_seqs);

        /* build in-memory vectors from smr_seq_t array */
        std::vector<std::string> ids, sequences, quals;
        ids.reserve(num_seqs);
        sequences.reserve(num_seqs);
        if (has_qual) quals.reserve(num_seqs);
        for (int32_t i = 0; i < num_seqs; i++) {
            bool this_has_qual = (seqs[i].quality != nullptr);
            if (this_has_qual != has_qual) {
                set_error(ctx, SMR_ERR_INVALID_CONFIG,
                          "seq[%d]: all sequences must have quality strings or none", i);
                return SMR_ERR_INVALID_CONFIG;
            }
            ids.emplace_back(seqs[i].id);
            sequences.emplace_back(seqs[i].sequence);
            if (has_qual)
                quals.emplace_back(seqs[i].quality);
        }

        Index index(opts);
        KeyValueDatabase kvdb(opts.kvdbdir.string());

        /* construct Readfeed in MEMORY mode — no file I/O for reads */
        auto basedir = opts.readb_dir;
        Readfeed readfeed(std::move(ids), std::move(sequences), std::move(quals),
                          opts.num_proc_thread, basedir, opts.is_paired);
        Readstats readstats(readfeed.num_reads_tot, readfeed.length_all,
                            readfeed.min_read_len, readfeed.max_read_len, kvdb, opts);

        align(readfeed, readstats, index, kvdb, opts);
        writeSummary(readstats, opts);
        writeReports(readfeed, readstats, kvdb, opts);

        ctx_log(ctx, SMR_LOG_INFO, "pipeline complete");

        if (stats) {
            memset(stats, 0, sizeof(*stats));
            stats->total_reads = readstats.all_reads_count;
            stats->total_aligned = readstats.num_aligned.load();
            stats->total_id_cov_pass = readstats.n_yid_ycov.load();
            stats->total_denovo = readstats.num_denovo.load();
            stats->min_read_len = readstats.min_read_len;
            stats->max_read_len = readstats.max_read_len;
            auto wall_end = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed = wall_end - wall_start;
            stats->wall_time_sec = elapsed.count();
        }

        if (out) {
            auto *o = static_cast<smr_output_t *>(calloc(1, sizeof(smr_output_t)));
            if (!o) {
                set_error(ctx, SMR_ERR_ALLOC, "failed to allocate smr_output_t");
                return SMR_ERR_ALLOC;
            }

            o->num_reads = readstats.all_reads_count;

            if (!populate_per_read_output(o, readfeed, readstats, kvdb, opts)) {
                smr_output_free(o);
                set_error(ctx, SMR_ERR_ALLOC, "failed to allocate per-read output arrays");
                return SMR_ERR_ALLOC;
            }

            *out = o;
        }

        set_error(ctx, SMR_OK, "");
        return SMR_OK;

    } catch (const smr_exit_requested &) {
        set_error(ctx, SMR_ERR_INVALID_CONFIG, "unexpected --help/--version in library context");
        return SMR_ERR_INVALID_CONFIG;
    } catch (const std::exception &e) {
        set_error(ctx, SMR_ERR_ALIGN, "%s", e.what());
        return SMR_ERR_ALIGN;
    } catch (...) {
        set_error(ctx, SMR_ERR_ALIGN, "unknown exception in pipeline");
        return SMR_ERR_ALIGN;
    }
}

void smr_output_free(smr_output_t *out) {
    if (!out) return; /* NULL is always safe -- contract */
    /* free library-owned string arrays */
    if (out->read_ids) {
        for (uint64_t i = 0; i < out->num_reads; i++)
            free(const_cast<char*>(out->read_ids[i]));
        free(const_cast<char**>(out->read_ids));
    }
    if (out->cigar) {
        for (uint64_t i = 0; i < out->num_reads; i++)
            free(const_cast<char*>(out->cigar[i]));
        free(const_cast<char**>(out->cigar));
    }
    free(out->aligned);
    free(out->ref_index);
    free(out->e_value);
    free(out->identity);
    free(out->coverage);
    free(out->ref_start);
    free(out->ref_end);
    free(out);
}

/* --- Version --- */

const char *smr_version(void) {
    return SMR_STRINGIFY(SORTMERNA_MAJOR) "."
           SMR_STRINGIFY(SORTMERNA_MINOR) "."
           SMR_STRINGIFY(SORTMERNA_PATCH);
}
