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
#include "processor.hpp"
#include "output.hpp"
#include "summary.hpp"
#include "otumap.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

/* stringification helpers for version macros */
#define SMR_STRINGIFY2(x) #x
#define SMR_STRINGIFY(x) SMR_STRINGIFY2(x)

/* process-level atomic counter for unique workdir names across all contexts */
static std::atomic<int> g_run_counter{0};

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

    /* always produce BLAST output for structured result extraction */
    args.push_back("--blast");
    args.push_back("1 cigar qcov");
    args.push_back("--fastx");

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
 * Find an output file trying multiple extensions (.fa, .fasta, .fq, .fastq).
 * Returns empty string if none found.
 */
static std::string find_output_file(const std::string &prefix) {
    static const char* exts[] = { ".fa", ".fasta", ".fq", ".fastq" };
    for (auto ext : exts) {
        std::string path = prefix + ext;
        if (file_exists(path.c_str())) return path;
    }
    return "";
}

/*
 * Count non-empty lines in a file.
 */
static uint64_t count_lines(const std::string &path) {
    std::ifstream ifs(path);
    uint64_t count = 0;
    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty()) count++;
    }
    return count;
}

/*
 * Count sequences in a FASTA/FASTQ file by counting header lines.
 */
static uint64_t count_seqs(const std::string &path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) return 0;
    uint64_t count = 0;
    std::string line;
    while (std::getline(ifs, line)) {
        if (!line.empty() && (line[0] == '>' || line[0] == '@')) count++;
    }
    return count;
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

    /* RAII cleanup for temp workdir — handles all exit paths including exceptions */
    WorkdirGuard wdguard(workdir, workdir_is_temp);

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
                /* wdguard destructor cleans up workdir */
            }

            /* use readstats for authoritative total count */
            o->num_reads = readstats.all_reads_count;

            /* count aligned from BLAST output (one line per alignment with --num_alignments 1) */
            std::string blast_file = workdir + "/aligned.blast";
            if (file_exists(blast_file.c_str())) {
                o->num_aligned = count_lines(blast_file);
            } else {
                /* fallback: count aligned FASTA/FASTQ sequences */
                std::string aligned_path = find_output_file(workdir + "/aligned");
                if (!aligned_path.empty())
                    o->num_aligned = count_seqs(aligned_path);
            }

            *out = o;
        }

        set_error(ctx, SMR_OK, "");
        return SMR_OK;

    } catch (const smr_exit_requested &) {
        set_error(ctx, SMR_ERR_INVALID_CONFIG, "unexpected --help/--version in library context");
        return SMR_ERR_INVALID_CONFIG;
        /* wdguard destructor cleans up workdir */
    } catch (const std::exception &e) {
        set_error(ctx, SMR_ERR_ALIGN, "%s", e.what());
        return SMR_ERR_ALIGN;
        /* wdguard destructor cleans up workdir */
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
