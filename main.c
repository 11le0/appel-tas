#define _POSIX_C_SOURCE 200809L


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
static int bf_ncpu(void) { SYSTEM_INFO si; GetSystemInfo(&si); return (int)si.dwNumberOfProcessors; }
#else
static int bf_ncpu(void) { return (int)sysconf(_SC_NPROCESSORS_ONLN); }
#endif

#include "physics.h"
#include "spikes.h"
#include "level.h"
#include "playerstate_json.h"
#include "hashset.h"
#include "expr.h"


#define MAX_DEPTH 4096

#define MAX_SPLIT_DEPTH 8

typedef enum { PIN_NONE, PIN_EXACT, PIN_EXCLUDE, PIN_CONSTRAINT } PinType;

typedef struct {
    PinType type;
    int     mask;
    int     mustSet;
    int     mustClear;
} FramePin;

typedef enum { EXPORT_NONE = 0, EXPORT_CSV, EXPORT_BIN, EXPORT_JSON } ExportFormat;

typedef struct {
    char    *level_path;
    char    *state_path;
    char    *config_path;
    char    *out_path;
    char    *spikes_path;
    bool     spikes_explicit;
    int      depth;
    char    *goal_expr;
    bool     minimize;
    char    *killzone_expr;
    long     progress_every;
    int      threads;
    int      split_depth;

    char        *export_path;
    ExportFormat export_format;
    bool         export_full_path;

    bool     enabled_moves[16];
    FramePin pins[MAX_DEPTH + 2];
    int      combo_max[16];
} Config;

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int sig) { (void)sig; g_stop = 1; }

static void config_defaults(Config *c)
{
    memset(c, 0, sizeof(*c));
    c->spikes_path = NULL;
    c->spikes_explicit = false;
    c->depth = 20;
    c->goal_expr = strdup("PLAYER_X");
    c->minimize = false;
    c->killzone_expr = NULL;
    c->progress_every = 5000000;
    c->threads = 0;
    c->split_depth = 0;
    c->export_path = NULL;
    c->export_format = EXPORT_NONE;
    c->export_full_path = false;
    for (int i = 0; i < 16; i++) { c->enabled_moves[i] = true; c->combo_max[i] = -1; }
}

static char *read_whole_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0';
    fclose(f);
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' || buf[n-1] == ' ')) buf[--n] = '\0';
    return buf;
}

static char *resolve_default_spikes_path(const char *argv0)
{
    char exe_path[4096];
#ifdef _WIN32
    long len = (long)GetModuleFileNameA(NULL, exe_path, sizeof(exe_path) - 1);
    for (long i = 0; i < len; i++) if (exe_path[i] == '\\') exe_path[i] = '/';
#else
    long len = (long)readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
#endif
    if (len > 0) {
        exe_path[len] = '\0';
        char *slash = strrchr(exe_path, '/');
        if (slash) {
            *slash = '\0';
            char *out = malloc(strlen(exe_path) + strlen("/spike_correlation_table.json") + 1);
            sprintf(out, "%s/spike_correlation_table.json", exe_path);
            return out;
        }
    }

    const char *slash = strrchr(argv0, '/');
    if (slash) {
        size_t dirlen = (size_t)(slash - argv0);
        char *out = malloc(dirlen + strlen("/spike_correlation_table.json") + 1);
        memcpy(out, argv0, dirlen);
        sprintf(out + dirlen, "/spike_correlation_table.json");
        return out;
    }

    return strdup("spike_correlation_table.json");
}


static void parse_config_file(Config *c, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "warning: could not open config file '%s'\n", path); return; }

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;

        char key[64] = {0};
        int consumed = 0;
        if (sscanf(p, "%63[^:]:%n", key, &consumed) != 1) continue;
        char *rest = p + consumed;
        while (*rest == ' ' || *rest == '\t') rest++;

        if (strcmp(key, "enabled_moves") == 0) {
            for (int i = 0; i < 16; i++) c->enabled_moves[i] = false;
            char *tok = strtok(rest, ", \t\r\n");
            while (tok) {
                int m = atoi(tok);
                if (m >= 0 && m < 16) c->enabled_moves[m] = true;
                tok = strtok(NULL, ", \t\r\n");
            }
        } else if (strcmp(key, "combo_max") == 0) {
            int mask, count;
            if (sscanf(rest, "%d %d", &mask, &count) == 2 && mask >= 0 && mask < 16) {
                c->combo_max[mask] = count;
            }
        } else if (strcmp(key, "pin") == 0) {
            int frame;
            char type[32];
            if (sscanf(rest, "%d %31s", &frame, type) == 2 && frame > 0 && frame <= MAX_DEPTH) {
                if (strcmp(type, "exact") == 0) {
                    int mask;
                    sscanf(rest, "%d %*s %d", &frame, &mask);
                    c->pins[frame] = (FramePin){ PIN_EXACT, mask, 0, 0 };
                } else if (strcmp(type, "exclude") == 0) {
                    int mask;
                    sscanf(rest, "%d %*s %d", &frame, &mask);
                    c->pins[frame] = (FramePin){ PIN_EXCLUDE, mask, 0, 0 };
                } else if (strcmp(type, "constraint") == 0) {
                    int mustSet = 0, mustClear = 0;
                    sscanf(rest, "%d %*s %d %d", &frame, &mustSet, &mustClear);
                    c->pins[frame] = (FramePin){ PIN_CONSTRAINT, 0, mustSet, mustClear };
                }
            }
        }
    }
    fclose(f);
}

static void print_usage(const char *argv0)
{
    fprintf(stderr,
        "Usage: %s -l level.txt -s state.json -d DEPTH [options]\n"
        "\n"
        "Required:\n"
        "  -l, --level FILE       file containing the raw level code string\n"
        "  -s, --state FILE       JSON file with the starting player state\n"
        "\n"
        "Options:\n"
        "  -d, --depth N          search depth in frames (default 20)\n"
        "  -g, --goal EXPR        score expression to maximize (default \"PLAYER_X\")\n"
        "      --min              minimize the goal expression instead of maximizing\n"
        "  -k, --killzone EXPR    prune any state where this expression is truthy\n"
        "  -c, --config FILE      optional config file: enabled_moves / pin / combo_max\n"
        "      --spikes FILE      spike correlation-table JSON (default: spike_correlation_table.json)\n"
        "      --no-spikes        explicitly disable spike collision (no spikes in level)\n"
        "  -o, --out FILE         write the best input sequence (TAS format) here\n"
        "      --progress N       print a progress line every N leaf evaluations\n"
        "  -t, --threads N        worker threads (default: auto-detect cores)\n"
        "      --split-depth N    frames to fan out across threads before each\n"
        "                         thread does its own DFS (default: auto)\n"
        "      --export-leaves FILE   write every surviving leaf (score + inputs),\n"
        "                         not just the best, to FILE for external analysis\n"
        "      --export-format {csv|bin|json}  format for --export-leaves (default: csv)\n"
        "                         csv:  \"score,inputs\\n\" per leaf, inputs in TAS\n"
        "                               format (human-/spreadsheet-readable, but\n"
        "                               ~50-80 bytes/leaf)\n"
        "                         bin:  fixed-size binary records — 4-byte float\n"
        "                               score + ceil(depth/2) bytes of 4-bit-packed\n"
        "                               per-frame move masks, no header per record.\n"
        "                               ~5-10x smaller than csv; use this for runs\n"
        "                               with hundreds of millions to billions of\n"
        "                               leaves. See the leaf-export comment block\n"
        "                               above export_bin_record_size() in main.c\n"
        "                               for the exact record layout.\n"
        "                         json: one NDJSON object per leaf with score,\n"
        "                               inputs, and the full ending PlayerState.\n"
        "      --export-full-path json only: also include a \"path\" array with\n"
        "                         every intermediate frame's state (not just the\n"
        "                         ending one) — everything the player actually\n"
        "                         passed through, not just where it landed.\n"
        "                         Costs one extra full replay per leaf, so files\n"
        "                         get much bigger — pair with small depths.\n"
        "  -h, --help             show this message\n",
        argv0);
}


static void inputs_to_tas_format(const uint8_t *inputs, int n, char *out, size_t out_sz)
{
    size_t pos = 0;
    int i = 0;
    bool first = true;
    while (i < n) {
        uint8_t mask = inputs[i];
        int count = 1;
        while (i + count < n && inputs[i + count] == mask) count++;

        char keys[6] = {0};
        int kp = 0;
        if (mask & 8) keys[kp++] = 'W';
        if (mask & 2) keys[kp++] = 'A';
        if (mask & 4) keys[kp++] = 'S';
        if (mask & 1) keys[kp++] = 'D';
        if (kp == 0) keys[kp++] = '_';
        keys[kp] = '\0';

        int written = snprintf(out + pos, (pos < out_sz) ? out_sz - pos : 0,
                                "%s%s%d", first ? "" : " ", keys, count);
        if (written > 0) pos += (size_t)written;
        first = false;
        i += count;
    }
}


static const char *export_format_name(ExportFormat fmt)
{
    switch (fmt) {
        case EXPORT_BIN:  return "bin";
        case EXPORT_JSON: return "json";
        default:          return "csv";
    }
}

static int export_bin_record_size(int depth)
{
    return 4  + (depth + 1) / 2 ;
}

static void export_write_leaf_bin(FILE *f, double raw_score, const uint8_t *path_buffer, int depth)
{
    float score32 = (float)raw_score;
    fwrite(&score32, sizeof(score32), 1, f);

    int packed_len = (depth + 1) / 2;
    uint8_t packed[(MAX_DEPTH + 1) / 2];
    for (int i = 0; i < packed_len; i++) {
        uint8_t lo = path_buffer[2 * i] & 0x0F;
        uint8_t hi = (2 * i + 1 < depth) ? (path_buffer[2 * i + 1] & 0x0F) : 0;
        packed[i] = (uint8_t)(lo | (hi << 4));
    }
    fwrite(packed, 1, (size_t)packed_len, f);
}

static void export_write_leaf_csv(FILE *f, double raw_score, const uint8_t *path_buffer, int depth)
{
    char tas[MAX_DEPTH * 4 + 64];
    inputs_to_tas_format(path_buffer, depth, tas, sizeof(tas));
    fprintf(f, "%.6g,%s\n", raw_score, tas);
}

static void export_write_state_obj(FILE *f, const PlayerState *st)
{
    fprintf(f,
        "{"
        "\"PLAYER_X\":%.6g,\"PLAYER_Y\":%.6g,\"PLAYER_SX\":%.6g,\"PLAYER_SY\":%.6g,"
        "\"is_jumping\":%d,\"is_falling\":%d,\"player_state\":%d,"
        "\"player_wall\":%d,\"flipped\":%d,\"direction\":%.6g,"
        "\"PLAYER_DEATH\":%s,\"wasInSpikeTileLastFrame\":%s,"
        "\"KEY_LEFT\":%u,\"KEY_RIGHT\":%u,\"KEY_DOWN\":%u,\"KEY_UP\":%u,"
        "\"PSZ\":[%d,%d,%d,%d]"
        "}",
        st->PLAYER_X, st->PLAYER_Y, st->PLAYER_SX, st->PLAYER_SY,
        (int)st->is_jumping, (int)st->is_falling, (int)st->player_state,
        ps_get_wall(st), ps_get_flipped(st), ps_get_direction(st),
        st->PLAYER_DEATH ? "true" : "false",
        st->wasInSpikeTileLastFrame ? "true" : "false",
        (unsigned)st->KEY_LEFT, (unsigned)st->KEY_RIGHT, (unsigned)st->KEY_DOWN, (unsigned)st->KEY_UP,
        ps_get_psz(st, 1), ps_get_psz(st, 2), ps_get_psz(st, 3), ps_get_psz(st, 4));
}

static void export_write_leaf_json(FILE *f, double raw_score, const uint8_t *path_buffer,
                                    int depth, const PlayerState *st,
                                    bool full_path, AppelPhysics *ap,
                                    const PlayerState *start_state)
{
    char tas[MAX_DEPTH * 4 + 64];
    inputs_to_tas_format(path_buffer, depth, tas, sizeof(tas));


    fprintf(f, "{\"score\":%.6g,\"inputs\":\"%s\",\"state\":", raw_score, tas);
    export_write_state_obj(f, st);

    if (full_path) {
        PlayerState replay = *start_state;
        fprintf(f, ",\"path\":[");
        for (int frame = 0; frame < depth; frame++) {
            appel_tick(ap, &replay, path_buffer[frame]);
            if (frame > 0) fputc(',', f);
            fprintf(f, "{\"frame\":%d,\"move\":%d,\"state\":", frame + 1, (int)path_buffer[frame]);
            export_write_state_obj(f, &replay);
            fputc('}', f);
        }
        fputc(']', f);
    }

    fprintf(f, "}\n");
}

static void export_merge_shard(FILE *out, const char *shard_path)
{
    FILE *in = fopen(shard_path, "rb");
    if (!in) {
        fprintf(stderr, "warning: could not reopen export shard '%s' for merging\n", shard_path);
        return;
    }
    char buf[1 << 20];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, n, out);
    }
    fclose(in);
    remove(shard_path);
}


typedef struct {
    int      count;
    const int *moves;
} MoveList;


typedef struct {
    PlayerState state;
    uint8_t     path[MAX_SPLIT_DEPTH];
    int16_t     counts[16];
} Task;

typedef struct {
    Task   *tasks;
    size_t  count;
    size_t  capacity;
} TaskList;

static void tasklist_push(TaskList *tl, const Task *t)
{
    if (tl->count == tl->capacity) {
        tl->capacity = tl->capacity ? tl->capacity * 2 : 256;
        tl->tasks = realloc(tl->tasks, tl->capacity * sizeof(Task));
    }
    tl->tasks[tl->count++] = *t;
}

typedef struct {
    AppelPhysics *ap;
    Expr         *killzone_expr;
    int         **frame_moves_buf;
    int          *frame_moves_cnt;
    int           combo_max[16];
    int           split_depth;
    HashSet      *gen_visited[MAX_SPLIT_DEPTH + 1];
    TaskList     *out;
} GenCtx;

static void generate_tasks_rec(GenCtx *g, int cur_depth, PlayerState *state,
                                uint8_t *path, int16_t *counts)
{
    if (cur_depth == g->split_depth) {
        Task t;
        t.state = *state;
        memcpy(t.path, path, (size_t)g->split_depth);
        memcpy(t.counts, counts, sizeof(t.counts));
        tasklist_push(g->out, &t);
        return;
    }

    int mv_count = g->frame_moves_cnt[cur_depth];
    const int *moves = g->frame_moves_buf[cur_depth];

    for (int i = 0; i < mv_count; i++) {
        int move = moves[i];
        if (g->combo_max[move] >= 0 && counts[move] >= g->combo_max[move]) continue;

        PlayerState next = *state;
        appel_tick(g->ap, &next, move);
        if (next.PLAYER_DEATH) continue;

        ExprCtx ctx = { &next, g->ap, cur_depth + 1 };
        if (g->killzone_expr && expr_eval(g->killzone_expr, &ctx) != 0.0) continue;

        StateKey key; state_key_build(&next, &key);
        if (!hashset_insert_if_absent(g->gen_visited[cur_depth + 1], &key)) continue;

        path[cur_depth] = (uint8_t)move;
        int16_t next_counts[16];
        memcpy(next_counts, counts, sizeof(next_counts));
        next_counts[move]++;

        generate_tasks_rec(g, cur_depth + 1, &next, path, next_counts);
    }
}

typedef struct {
    AppelPhysics ap;

    Expr    *goal_expr;
    Expr    *killzone_expr;
    int    **frame_moves_buf;
    int     *frame_moves_cnt;
    int      combo_max[16];
    bool     minimize;
    int      depth;
    int      split_depth;
    long     progress_every;
    struct timespec t_start;

    TaskList     *tasks;
    atomic_size_t *next_task;

    pthread_mutex_t *best_mutex;
    double           *best_score;
    bool             *have_best;
    uint8_t          *best_inputs;

    atomic_long *leaf_count;
    atomic_long *total_ticks;
    atomic_size_t *tasks_done;

    FILE         *export_file;
    ExportFormat  export_format;
    bool          export_full_path;
    PlayerState   start_state;
} WorkerCtx;

static void *worker_thread(void *arg)
{
    WorkerCtx *w = (WorkerCtx *)arg;
    int depth = w->depth;
    int split_depth = w->split_depth;

    PlayerState *state_pool = malloc(sizeof(PlayerState) * (size_t)(depth + 1));
    HashSet    **visited     = malloc(sizeof(HashSet *) * (size_t)(depth + 1));
    for (int i = split_depth; i <= depth; i++) visited[i] = hashset_create(1 << 12);
    int8_t  *input_stack = malloc(sizeof(int8_t) * (size_t)(depth + 1));
    uint8_t *path_buffer = malloc(sizeof(uint8_t) * (size_t)depth);

    typedef int16_t CountRow[16];
    CountRow *counts_at_depth = calloc((size_t)(depth + 1), sizeof(CountRow));

    for (;;) {
        if (g_stop) break;
        size_t task_idx = atomic_fetch_add(w->next_task, 1);
        if (task_idx >= w->tasks->count) break;

        Task *task = &w->tasks->tasks[task_idx];
        int current_depth = split_depth;
        state_pool[current_depth] = task->state;
        memcpy(path_buffer, task->path, (size_t)split_depth);
        memcpy(counts_at_depth[current_depth], task->counts, sizeof(CountRow));
        input_stack[current_depth] = -1;

        while (1) {
            if (current_depth >= depth) {
                current_depth--;
                if (current_depth < split_depth) break;
                continue;
            }

            input_stack[current_depth]++;

            int mv_count = w->frame_moves_cnt[current_depth];
            if (input_stack[current_depth] >= mv_count) {
                current_depth--;
                if (current_depth < split_depth) break;
                continue;
            }
            if (g_stop) break;

            int move = w->frame_moves_buf[current_depth][input_stack[current_depth]];

            if (w->combo_max[move] >= 0 &&
                counts_at_depth[current_depth][move] >= w->combo_max[move]) {
                continue;
            }

            PlayerState *prev_state = &state_pool[current_depth];
            PlayerState *next_state = &state_pool[current_depth + 1];
            *next_state = *prev_state;

            appel_tick(&w->ap, next_state, move);
            atomic_fetch_add(w->total_ticks, 1);

            path_buffer[current_depth] = (uint8_t)move;

            if (next_state->PLAYER_DEATH) continue;

            ExprCtx ctx = { next_state, &w->ap, current_depth + 1 };
            if (w->killzone_expr && expr_eval(w->killzone_expr, &ctx) != 0.0) continue;

            int next_depth = current_depth + 1;

            if (next_depth == depth) {
                long old_leaf = atomic_fetch_add(w->leaf_count, 1);
                long new_leaf = old_leaf + 1;

                double raw_score = expr_eval(w->goal_expr, &ctx);
                double score = w->minimize ? -raw_score : raw_score;

                if (w->export_file) {
                    if (w->export_format == EXPORT_BIN)
                        export_write_leaf_bin(w->export_file, raw_score, path_buffer, depth);
                    else if (w->export_format == EXPORT_JSON)
                        export_write_leaf_json(w->export_file, raw_score, path_buffer, depth, next_state,
                                                w->export_full_path, &w->ap, &w->start_state);
                    else
                        export_write_leaf_csv(w->export_file, raw_score, path_buffer, depth);
                }

                pthread_mutex_lock(w->best_mutex);
                if (score > *w->best_score) {
                    *w->best_score = score;
                    *w->have_best = true;
                    memcpy(w->best_inputs, path_buffer, (size_t)depth);

                    char tas[8192];
                    inputs_to_tas_format(w->best_inputs, depth, tas, sizeof(tas));
                    fprintf(stderr, "[new best] score=%.6g  leaf=%ld  ticks=%ld  inputs: %s\n",
                            raw_score, new_leaf, (long)atomic_load(w->total_ticks), tas);
                }
                pthread_mutex_unlock(w->best_mutex);

                if (w->progress_every > 0 &&
                    old_leaf / w->progress_every != new_leaf / w->progress_every) {
                    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
                    double elapsed = (now.tv_sec - w->t_start.tv_sec) +
                                      (now.tv_nsec - w->t_start.tv_nsec) / 1e9;
                    long ticks = atomic_load(w->total_ticks);
                    pthread_mutex_lock(w->best_mutex);
                    double bs = *w->best_score;
                    pthread_mutex_unlock(w->best_mutex);
                    fprintf(stderr, "progress: leaves=%ld ticks=%ld  %.0f ticks/s  best=%.6g\n",
                            new_leaf, ticks, ticks / (elapsed > 0 ? elapsed : 1), bs);
                }
            } else {
                StateKey key; state_key_build(next_state, &key);
                if (!hashset_insert_if_absent(visited[next_depth], &key)) continue;

                memcpy(counts_at_depth[next_depth], counts_at_depth[current_depth], sizeof(CountRow));
                counts_at_depth[next_depth][move]++;

                current_depth++;
                input_stack[current_depth] = -1;
            }
        }

        if (!g_stop) atomic_fetch_add(w->tasks_done, 1);
        if (g_stop) break;
    }

    for (int i = split_depth; i <= depth; i++) hashset_free(visited[i]);
    free(visited);
    free(state_pool);
    free(input_stack);
    free(path_buffer);
    free(counts_at_depth);
    if (w->export_file) fclose(w->export_file);
    return NULL;
}

typedef struct {
    size_t          tasks_total;
    atomic_size_t  *tasks_done;
    atomic_long    *leaf_count;
    atomic_long    *total_ticks;
    struct timespec t_start;
} ReporterCtx;

static void *progress_reporter_thread(void *arg)
{
    ReporterCtx *r = (ReporterCtx *)arg;
    while (!g_stop) {
        struct timespec ts = { .tv_sec = 1, .tv_nsec = 0 };
        nanosleep(&ts, NULL);
        if (g_stop) break;

        size_t done = atomic_load(r->tasks_done);
        if (done > r->tasks_total) done = r->tasks_total;
        double pct = r->tasks_total ? (100.0 * (double)done / (double)r->tasks_total) : 100.0;

        struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - r->t_start.tv_sec) + (now.tv_nsec - r->t_start.tv_nsec) / 1e9;

        long leaves = atomic_load(r->leaf_count);
        long ticks  = atomic_load(r->total_ticks);

        fprintf(stderr,
                "task_progress: %zu/%zu (%.1f%%) leaves=%ld ticks=%ld elapsed=%.1fs\n",
                done, r->tasks_total, pct, leaves, ticks, elapsed);

        if (done >= r->tasks_total) break;
    }
    return NULL;
}

int main(int argc, char **argv)
{
    Config cfg;
    config_defaults(&cfg);

    static struct { const char *flag, *flag_long; } _unused; (void)_unused;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        #define NEXT() (i + 1 < argc ? argv[++i] : (fprintf(stderr, "missing value for %s\n", a), exit(1), (char*)NULL))
        if (!strcmp(a, "-l") || !strcmp(a, "--level"))       cfg.level_path   = strdup(NEXT());
        else if (!strcmp(a, "-s") || !strcmp(a, "--state"))  cfg.state_path   = strdup(NEXT());
        else if (!strcmp(a, "-d") || !strcmp(a, "--depth"))  cfg.depth        = atoi(NEXT());
        else if (!strcmp(a, "-g") || !strcmp(a, "--goal")) { free(cfg.goal_expr); cfg.goal_expr = strdup(NEXT()); }
        else if (!strcmp(a, "--min"))                        cfg.minimize     = true;
        else if (!strcmp(a, "-k") || !strcmp(a, "--killzone")) cfg.killzone_expr = strdup(NEXT());
        else if (!strcmp(a, "-c") || !strcmp(a, "--config")) cfg.config_path  = strdup(NEXT());
        else if (!strcmp(a, "--spikes"))                     { free(cfg.spikes_path); cfg.spikes_path = strdup(NEXT()); cfg.spikes_explicit = true; }
        else if (!strcmp(a, "--no-spikes"))                  { free(cfg.spikes_path); cfg.spikes_path = NULL; cfg.spikes_explicit = true; }
        else if (!strcmp(a, "-o") || !strcmp(a, "--out"))    cfg.out_path     = strdup(NEXT());
        else if (!strcmp(a, "--progress"))                   cfg.progress_every = atol(NEXT());
        else if (!strcmp(a, "-t") || !strcmp(a, "--threads")) cfg.threads       = atoi(NEXT());
        else if (!strcmp(a, "--split-depth"))                cfg.split_depth   = atoi(NEXT());
        else if (!strcmp(a, "--export-leaves"))              cfg.export_path   = strdup(NEXT());
        else if (!strcmp(a, "--export-format")) {
            const char *f = NEXT();
            if      (!strcmp(f, "csv"))  cfg.export_format = EXPORT_CSV;
            else if (!strcmp(f, "bin"))  cfg.export_format = EXPORT_BIN;
            else if (!strcmp(f, "json")) cfg.export_format = EXPORT_JSON;
            else { fprintf(stderr, "--export-format must be 'csv', 'bin', or 'json', got '%s'\n", f); return 1; }
        }
        else if (!strcmp(a, "--export-full-path"))           cfg.export_full_path = true;
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) { print_usage(argv[0]); return 0; }
        else { fprintf(stderr, "unknown argument: %s\n", a); print_usage(argv[0]); return 1; }
        #undef NEXT
    }

    if (!cfg.level_path || !cfg.state_path) {
        print_usage(argv[0]);
        return 1;
    }
    if (cfg.depth <= 0 || cfg.depth > MAX_DEPTH) {
        fprintf(stderr, "depth must be between 1 and %d\n", MAX_DEPTH);
        return 1;
    }
    if (cfg.export_path && cfg.export_format == EXPORT_NONE) cfg.export_format = EXPORT_CSV;
    if (cfg.config_path) parse_config_file(&cfg, cfg.config_path);

    if (!cfg.spikes_explicit) {
        cfg.spikes_path = resolve_default_spikes_path(argv[0]);
    }

    char *level_code = read_whole_file(cfg.level_path);
    if (!level_code) { fprintf(stderr, "could not read level file '%s'\n", cfg.level_path); return 1; }

    LevelData ld;
    if (!level_load_from_code(level_code, &ld)) {
        fprintf(stderr, "failed to decode level code\n");
        return 1;
    }
    free(level_code);

    char *state_json = read_whole_file(cfg.state_path);
    if (!state_json) { fprintf(stderr, "could not read state file '%s'\n", cfg.state_path); return 1; }

    PlayerState start_state;
    memset(&start_state, 0, sizeof(start_state));
    ps_set_psz(&start_state, 1, 17); ps_set_psz(&start_state, 2, 13);
    ps_set_psz(&start_state, 3, 17); ps_set_psz(&start_state, 4, 13);
    start_state.is_falling = 255;
    ps_set_wall(&start_state, WALL_NONE);

    if (!playerstate_load_json(state_json, &start_state)) {
        fprintf(stderr, "failed to parse player state JSON\n");
        return 1;
    }
    free(state_json);

    AppelPhysics ap;
    appel_physics_init(&ap, ld.map, ld.rotations, NULL, ld.size_x, ld.map_len, NULL);

    SpikeData sd;
    memset(&sd, 0, sizeof(sd));
    if (cfg.spikes_path) {
        if (!spikes_load(&sd, cfg.spikes_path)) {
            fprintf(stderr,
                "could not load spike data '%s' — refusing to run with spike "
                "collision silently disabled (pass --no-spikes to explicitly "
                "run without it, e.g. for levels with no spikes)\n",
                cfg.spikes_path);
            return 1;
        }
        ap.spikes = &sd;
    } else {
        fprintf(stderr, "warning: running with --no-spikes; spike collision is disabled\n");
    }

    Expr *goal_expr = expr_compile(cfg.goal_expr);
    if (!goal_expr) { fprintf(stderr, "invalid --goal expression\n"); return 1; }
    Expr *killzone_expr = NULL;
    if (cfg.killzone_expr && *cfg.killzone_expr) {
        killzone_expr = expr_compile(cfg.killzone_expr);
        if (!killzone_expr) { fprintf(stderr, "invalid --killzone expression\n"); return 1; }
    }

    int allowed_moves[16], allowed_count = 0;
    for (int m = 0; m < 16; m++) if (cfg.enabled_moves[m]) allowed_moves[allowed_count++] = m;

    int depth = cfg.depth;
    int  *frame_moves_buf[MAX_DEPTH + 1];
    int   frame_moves_cnt[MAX_DEPTH + 1];
    for (int i = 0; i <= depth; i++) {
        int frame = i + 1;
        FramePin pin = (frame <= MAX_DEPTH) ? cfg.pins[frame] : (FramePin){ PIN_NONE, 0, 0, 0 };

        if (pin.type == PIN_EXACT) {
            frame_moves_buf[i] = malloc(sizeof(int));
            frame_moves_buf[i][0] = pin.mask;
            frame_moves_cnt[i] = 1;
        } else if (pin.type == PIN_EXCLUDE) {
            frame_moves_buf[i] = malloc(sizeof(int) * allowed_count);
            int n = 0;
            for (int j = 0; j < allowed_count; j++) if (allowed_moves[j] != pin.mask) frame_moves_buf[i][n++] = allowed_moves[j];
            frame_moves_cnt[i] = n;
        } else if (pin.type == PIN_CONSTRAINT) {
            frame_moves_buf[i] = malloc(sizeof(int) * allowed_count);
            int n = 0;
            for (int j = 0; j < allowed_count; j++) {
                int m = allowed_moves[j];
                if ((m & pin.mustSet) == pin.mustSet && (m & pin.mustClear) == 0) frame_moves_buf[i][n++] = m;
            }
            frame_moves_cnt[i] = n;
        } else {
            frame_moves_buf[i] = allowed_moves;
            frame_moves_cnt[i] = allowed_count;
        }
    }

    int nthreads = cfg.threads > 0 ? cfg.threads : bf_ncpu();
    if (nthreads < 1) nthreads = 1;

    int split_depth = 0;
    TaskList tasks = { 0 };
    int max_user_split = depth > 0 ? depth - 1 : 0;

    bool user_split = cfg.split_depth > 0;
    if (user_split) {
        split_depth = cfg.split_depth;
        if (split_depth > max_user_split) split_depth = max_user_split;
        if (split_depth > MAX_SPLIT_DEPTH) split_depth = MAX_SPLIT_DEPTH;
    }

    {
        int depth_cap = depth > 0 ? depth - 1 : 0;
        int max_split = depth_cap < MAX_SPLIT_DEPTH ? depth_cap : MAX_SPLIT_DEPTH;
        long target_tasks = (long)nthreads * 8;
        int try_split = split_depth;

        for (;;) {
            if (tasks.tasks) { free(tasks.tasks); tasks = (TaskList){ 0 }; }

            HashSet *gen_visited[MAX_SPLIT_DEPTH + 1];
            for (int i = 0; i <= try_split; i++) gen_visited[i] = hashset_create(1 << 12);

            GenCtx g = {
                .ap = &ap, .killzone_expr = killzone_expr,
                .frame_moves_buf = frame_moves_buf, .frame_moves_cnt = frame_moves_cnt,
                .split_depth = try_split, .out = &tasks,
            };
            memcpy(g.combo_max, cfg.combo_max, sizeof(g.combo_max));
            memcpy(g.gen_visited, gen_visited, sizeof(gen_visited));

            uint8_t path_buf[MAX_SPLIT_DEPTH];
            int16_t counts_buf[16] = { 0 };
            PlayerState root = start_state;
            StateKey k0; state_key_build(&root, &k0);
            hashset_insert_if_absent(gen_visited[0], &k0);

            generate_tasks_rec(&g, 0, &root, path_buf, counts_buf);

            for (int i = 0; i <= try_split; i++) hashset_free(gen_visited[i]);

            if (user_split || (long)tasks.count >= target_tasks ||
                try_split >= max_split || try_split >= depth_cap) {
                split_depth = try_split;
                break;
            }
            try_split++;
        }
    }

    if (tasks.count == 0) {
        fprintf(stderr, "Starting search: depth=%d goal=\"%s\" %s killzone=\"%s\"\n",
                depth, cfg.goal_expr, cfg.minimize ? "(minimize)" : "(maximize)",
                cfg.killzone_expr ? cfg.killzone_expr : "(none)");
        printf("no valid path found (every path died or hit the kill-zone before reaching depth %d)\n", depth);
        return 0;
    }
    if ((long)tasks.count < nthreads) nthreads = (int)tasks.count;

    uint8_t *best_inputs = malloc(sizeof(uint8_t) * (size_t)depth);
    pthread_mutex_t best_mutex = PTHREAD_MUTEX_INITIALIZER;
    double best_score = -1e300;
    bool   have_best = false;
    atomic_long   leaf_count  = 0;
    atomic_long   total_ticks = 0;
    atomic_size_t next_task   = 0;
    atomic_size_t tasks_done  = 0;

    signal(SIGINT, on_sigint);

    struct timespec t_start; clock_gettime(CLOCK_MONOTONIC, &t_start);

    fprintf(stderr,
            "Starting search: depth=%d goal=\"%s\" %s killzone=\"%s\"\n"
            "  threads=%d split_depth=%d tasks=%zu\n",
            depth, cfg.goal_expr, cfg.minimize ? "(minimize)" : "(maximize)",
            cfg.killzone_expr ? cfg.killzone_expr : "(none)",
            nthreads, split_depth, tasks.count);

    pthread_t  *tids  = malloc(sizeof(pthread_t) * (size_t)nthreads);
    WorkerCtx  *wctxs = malloc(sizeof(WorkerCtx) * (size_t)nthreads);

    char **export_shard_paths = NULL;
    char  **export_shard_bufs  = NULL;
    if (cfg.export_path) {
        export_shard_paths = malloc(sizeof(char *) * (size_t)nthreads);
        export_shard_bufs  = malloc(sizeof(char *) * (size_t)nthreads);
        for (int i = 0; i < nthreads; i++) {
            char path[4096];
            snprintf(path, sizeof(path), "%s.shard%d", cfg.export_path, i);
            export_shard_paths[i] = strdup(path);

            FILE *f = fopen(path, "wb");
            if (!f) {
                fprintf(stderr, "could not open export shard '%s' for writing\n", path);
                exit(1);
            }
            export_shard_bufs[i] = malloc(1 << 20);
            setvbuf(f, export_shard_bufs[i], _IOFBF, 1 << 20);
            wctxs[i].export_file      = f;
            wctxs[i].export_format    = cfg.export_format;
            wctxs[i].export_full_path = cfg.export_full_path;
        }
        fprintf(stderr, "exporting leaves to '%s' (format=%s%s)\n",
                cfg.export_path, export_format_name(cfg.export_format),
                cfg.export_full_path ? ", full-path" : "");
    } else {
        for (int i = 0; i < nthreads; i++) {
            wctxs[i].export_file      = NULL;
            wctxs[i].export_format    = EXPORT_NONE;
            wctxs[i].export_full_path = false;
        }
    }

    if (cfg.export_full_path && cfg.export_format != EXPORT_JSON) {
        fprintf(stderr, "warning: --export-full-path only applies to --export-format json; ignoring\n");
        for (int i = 0; i < nthreads; i++) wctxs[i].export_full_path = false;
    }

    pthread_t reporter_tid;
    ReporterCtx reporter_ctx = {
        .tasks_total = tasks.count,
        .tasks_done  = &tasks_done,
        .leaf_count  = &leaf_count,
        .total_ticks = &total_ticks,
        .t_start     = t_start,
    };
    pthread_create(&reporter_tid, NULL, progress_reporter_thread, &reporter_ctx);

    for (int i = 0; i < nthreads; i++) {
        WorkerCtx *w = &wctxs[i];
        w->ap = ap;
        w->goal_expr = goal_expr;
        w->killzone_expr = killzone_expr;
        w->frame_moves_buf = frame_moves_buf;
        w->frame_moves_cnt = frame_moves_cnt;
        memcpy(w->combo_max, cfg.combo_max, sizeof(w->combo_max));
        w->minimize = cfg.minimize;
        w->depth = depth;
        w->split_depth = split_depth;
        w->progress_every = cfg.progress_every;
        w->t_start = t_start;
        w->tasks = &tasks;
        w->next_task = &next_task;
        w->best_mutex = &best_mutex;
        w->best_score = &best_score;
        w->have_best = &have_best;
        w->best_inputs = best_inputs;
        w->leaf_count = &leaf_count;
        w->total_ticks = &total_ticks;
        w->tasks_done = &tasks_done;
        w->start_state = start_state;

        pthread_create(&tids[i], NULL, worker_thread, w);
    }
    for (int i = 0; i < nthreads; i++) pthread_join(tids[i], NULL);

    if (cfg.export_path) {
        FILE *out = fopen(cfg.export_path, "wb");
        if (!out) {
            fprintf(stderr, "could not open '%s' for writing merged export\n", cfg.export_path);
        } else {
            int record_size = export_bin_record_size(depth);
            if (cfg.export_format == EXPORT_JSON) {
            } else {
                fprintf(out, "# appeltasc leaf export: format=%s depth=%d\n",
                        export_format_name(cfg.export_format), depth);
                if (cfg.export_format == EXPORT_BIN) {
                    fprintf(out, "# record_size=%d bytes = 4 (float32 score) + %d (ceil(depth/2) packed nibbles)\n",
                            record_size, record_size - 4);
                    fprintf(out, "# binary records follow immediately, back to back, no delimiters\n");
                } else {
                    fprintf(out, "score,inputs\n");
                }
            }
            for (int i = 0; i < nthreads; i++) export_merge_shard(out, export_shard_paths[i]);
            fclose(out);
            fprintf(stderr, "wrote leaf export to '%s'\n", cfg.export_path);
        }
        for (int i = 0; i < nthreads; i++) {
            free(export_shard_paths[i]);
            free(export_shard_bufs[i]);
        }
        free(export_shard_paths);
        free(export_shard_bufs);
    }


    g_stop = 1;
    pthread_join(reporter_tid, NULL);

    free(tids);
    free(wctxs);
    pthread_mutex_destroy(&best_mutex);

    long final_leaves = atomic_load(&leaf_count);
    long final_ticks  = atomic_load(&total_ticks);

    struct timespec t_end; clock_gettime(CLOCK_MONOTONIC, &t_end);
    double elapsed = (t_end.tv_sec - t_start.tv_sec) + (t_end.tv_nsec - t_start.tv_nsec) / 1e9;

    fprintf(stderr, "\n%s. leaves=%ld ticks=%ld elapsed=%.2fs (%.0f ticks/s)\n",
            g_stop ? "Stopped (Ctrl+C)" : "Search complete",
            final_leaves, final_ticks, elapsed, final_ticks / (elapsed > 0 ? elapsed : 1));

    if (have_best) {
        char tas[8192];
        inputs_to_tas_format(best_inputs, depth, tas, sizeof(tas));
        printf("best_score: %.6g\n", best_score);
        printf("tas_inputs: %s\n", tas);

        if (cfg.out_path) {
            FILE *f = fopen(cfg.out_path, "w");
            if (f) {
                fprintf(f, "%s\n", tas);
                fclose(f);
                fprintf(stderr, "wrote best input sequence to %s\n", cfg.out_path);
            } else {
                fprintf(stderr, "could not open --out file '%s' for writing\n", cfg.out_path);
            }
        }
    } else {
        printf("no valid path found (every path died or hit the kill-zone before reaching depth %d)\n", depth);
    }

    for (int i = 0; i <= depth; i++) {
        if (frame_moves_buf[i] != allowed_moves) free(frame_moves_buf[i]);
    }
    free(tasks.tasks);
    free(best_inputs);
    expr_free(goal_expr);
    expr_free(killzone_expr);
    level_free(&ld);
    if (ap.spikes) spikes_free(&sd);

    return 0;
}
