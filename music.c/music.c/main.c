/* playlist_manager.c
   Music Playlist Manager (single-file C program)
   Features:
    - Add / remove / list tracks
    - Search by title/artist/album (case-insensitive)
    - Shuffle, sort (title/artist/duration)
    - Play simulation (prints and sleeps)
    - Save/load CSV (playlist.csv by default)
   Compile: gcc -O2 -o playlist_manager playlist_manager.c
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h> /* sleep */

#define INITIAL_CAP 32
#define MAX_LINE 1024
#define DEFAULT_SAVE "playlist.csv"

/* Track structure */
typedef struct {
    char *title;
    char *artist;
    char *album;
    int duration; /* seconds */
} Track;

/* Playlist dynamic array */
typedef struct {
    Track *items;
    size_t size;
    size_t cap;
} Playlist;

/* Utility functions */
static void trim(char *s) {
    if (!s) return;
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t len = strlen(s);
    while (len && isspace((unsigned char)s[len - 1])) s[--len] = '\0';
}
static char *strdup_safe(const char *s) {
    if (!s) return NULL;
    char *d = strdup(s);
    if (!d) { perror("strdup"); exit(1); }
    return d;
}
static char *read_input_line(const char *prompt) {
    char buf[MAX_LINE];
    if (prompt) printf("%s", prompt);
    if (!fgets(buf, sizeof(buf), stdin)) return NULL;
    size_t len = strlen(buf);
    if (len && buf[len-1] == '\n') buf[len-1] = '\0';
    trim(buf);
    return strdup_safe(buf);
}
static char *str_tolower_copy(const char *s) {
    char *d = strdup_safe(s);
    for (char *p = d; *p; ++p) *p = (char)tolower((unsigned char)*p);
    return d;
}

/* Playlist operations */
static void init_playlist(Playlist *pl) {
    pl->cap = INITIAL_CAP;
    pl->size = 0;
    pl->items = calloc(pl->cap, sizeof(Track));
    if (!pl->items) { perror("calloc"); exit(1); }
}
static void free_track(Track *t) {
    if (!t) return;
    free(t->title); free(t->artist); free(t->album);
    t->title = t->artist = t->album = NULL;
    t->duration = 0;
}
static void free_playlist(Playlist *pl) {
    if (!pl) return;
    for (size_t i = 0; i < pl->size; ++i) free_track(&pl->items[i]);
    free(pl->items);
    pl->items = NULL;
    pl->size = pl->cap = 0;
}
static void ensure_capacity(Playlist *pl) {
    if (pl->size < pl->cap) return;
    pl->cap *= 2;
    pl->items = realloc(pl->items, pl->cap * sizeof(Track));
    if (!pl->items) { perror("realloc"); exit(1); }
}
static void add_track(Playlist *pl, const char *title, const char *artist, const char *album, int duration) {
    ensure_capacity(pl);
    Track *t = &pl->items[pl->size++];
    t->title = strdup_safe(title);
    t->artist = strdup_safe(artist);
    t->album = strdup_safe(album);
    t->duration = duration;
}
static void remove_track_at(Playlist *pl, size_t idx) {
    if (idx >= pl->size) return;
    free_track(&pl->items[idx]);
    for (size_t i = idx + 1; i < pl->size; ++i) pl->items[i-1] = pl->items[i];
    pl->size--;
}

/* CSV helpers: fields do not contain newlines; commas allowed if quoted */
static void csv_escape_field(FILE *f, const char *s) {
    if (strchr(s, ',') || strchr(s, '"')) {
        /* quote and double quotes inside */
        fputc('"', f);
        for (const char *p = s; *p; ++p) {
            if (*p == '"') fputs("\"\"", f);
            else fputc(*p, f);
        }
        fputc('"', f);
    } else {
        fputs(s, f);
    }
}
static char *csv_read_field(char **sp) {
    char *s = *sp;
    if (!s || !*s) { *sp = NULL; return strdup_safe(""); }
    char tmp[MAX_LINE]; size_t ti = 0;
    if (*s == '"') {
        s++; /* skip leading quote */
        while (*s) {
            if (*s == '"' && *(s+1) == '"') { tmp[ti++] = '"'; s += 2; continue; }
            if (*s == '"') { s++; break; }
            tmp[ti++] = *s++; if (ti+1 >= sizeof(tmp)) break;
        }
        while (*s && *s != ',') s++;
        if (*s == ',') s++;
    } else {
        while (*s && *s != ',') { tmp[ti++] = *s++; if (ti+1 >= sizeof(tmp)) break; }
        if (*s == ',') s++;
    }
    tmp[ti] = '\0';
    *sp = (*s) ? s : NULL;
    return strdup_safe(tmp);
}

/* Save / Load playlist to CSV */
static int save_playlist_csv(const Playlist *pl, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    /* header */
    fprintf(f, "title,artist,album,duration_seconds\n");
    for (size_t i = 0; i < pl->size; ++i) {
        csv_escape_field(f, pl->items[i].title); fputc(',', f);
        csv_escape_field(f, pl->items[i].artist); fputc(',', f);
        csv_escape_field(f, pl->items[i].album); fputc(',', f);
        fprintf(f, "%d\n", pl->items[i].duration);
    }
    fclose(f);
    return 1;
}
static int load_playlist_csv(Playlist *pl, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[MAX_LINE];
    /* skip header if present */
    if (!fgets(line, sizeof(line), f)) { fclose(f); return 0; }
    if (strstr(line, "title") == NULL || strstr(line, "artist") == NULL) {
        /* first line is data; rewind */
        fseek(f, 0, SEEK_SET);
    }
    while (fgets(line, sizeof(line), f)) {
        /* strip newline */
        char *nl = strchr(line, '\n'); if (nl) *nl = '\0';
        char *p = line;
        char *f1 = csv_read_field(&p);
        char *f2 = csv_read_field(&p);
        char *f3 = csv_read_field(&p);
        char *f4 = csv_read_field(&p);
        int dur = atoi(f4);
        if (*f1) add_track(pl, f1, f2, f3, dur);
        free(f1); free(f2); free(f3); free(f4);
    }
    fclose(f);
    return 1;
}

/* Print helpers */
static void print_track(const Track *t, size_t idx) {
    int mins = t->duration / 60;
    int secs = t->duration % 60;
    printf("%3zu) %s\n     Artist: %s  Album: %s  Duration: %d:%02d\n",
           idx+1, t->title, t->artist, t->album, mins, secs);
}
static void list_playlist(const Playlist *pl) {
    if (pl->size == 0) { printf("Playlist is empty.\n"); return; }
    for (size_t i = 0; i < pl->size; ++i) print_track(&pl->items[i], i);
}

/* Search (case-insensitive substring) */
static void search_playlist(const Playlist *pl, const char *term) {
    char *low = str_tolower_copy(term);
    int found = 0;
    for (size_t i = 0; i < pl->size; ++i) {
        char *t = str_tolower_copy(pl->items[i].title);
        char *a = str_tolower_copy(pl->items[i].artist);
        char *al = str_tolower_copy(pl->items[i].album);
        if (strstr(t, low) || strstr(a, low) || strstr(al, low)) {
            print_track(&pl->items[i], i); found = 1;
        }
        free(t); free(a); free(al);
    }
    free(low);
    if (!found) printf("No matches for \"%s\".\n", term);
}

/* Shuffle: Fisher-Yates */
static void shuffle_playlist(Playlist *pl) {
    if (pl->size < 2) return;
    srand((unsigned int)time(NULL));
    for (size_t i = pl->size - 1; i > 0; --i) {
        size_t j = rand() % (i + 1);
        Track tmp = pl->items[i];
        pl->items[i] = pl->items[j];
        pl->items[j] = tmp;
    }
}

/* Sorting helpers */
static int cmp_title(const void *a, const void *b) {
    const Track *ta = a, *tb = b;
    return strcasecmp(ta->title, tb->title);
}
static int cmp_artist(const void *a, const void *b) {
    const Track *ta = a, *tb = b;
    int r = strcasecmp(ta->artist, tb->artist);
    if (r != 0) return r;
    return strcasecmp(ta->title, tb->title);
}
static int cmp_duration(const void *a, const void *b) {
    const Track *ta = a, *tb = b;
    return ta->duration - tb->duration;
}

/* Play simulation: prints and sleeps briefly (duration limited for demo) */
static void play_track(const Track *t) {
    int demo_seconds = t->duration < 6 ? t->duration : 5; /* don't actually wait full song */
    printf("Now playing: %s — %s [%d:%02d]  (demo %d sec)\n",
           t->title, t->artist, t->duration/60, t->duration%60, demo_seconds);
    fflush(stdout);
    sleep(demo_seconds);
}

/* Interactive menu */
static void print_help(void) {
    puts("\nCommands:");
    puts(" add        - add a new track");
    puts(" list       - list all tracks");
    puts(" remove N   - remove track at index N (1-based)");
    puts(" search X   - search title/artist/album for X");
    puts(" shuffle    - shuffle playlist");
    puts(" sort title - sort by title");
    puts(" sort artist- sort by artist then title");
    puts(" sort dur   - sort by duration ascending");
    puts(" play N     - play track N (simulated)");
    puts(" save [f]   - save playlist to file (default: playlist.csv)");
    puts(" load [f]   - load playlist from file and append (default: playlist.csv)");
    puts(" clear      - clear playlist (destructive)");
    puts(" help       - show this help");
    puts(" quit       - save and exit\n");
}

/* Parse integer from token, return -1 if invalid */
static int parse_index_token(const char *tok, int max) {
    if (!tok) return -1;
    char *end; long v = strtol(tok, &end, 10);
    if (*end != '\0' || v < 1 || v > max) return -1;
    return (int)v - 1;
}

int main(void) {
    Playlist pl;
    init_playlist(&pl);

    /* try loading default file */
    load_playlist_csv(&pl, DEFAULT_SAVE);

    printf("Music Playlist Manager — simple and presentable\n");
    printf("Type 'help' for commands. Starting with %zu tracks loaded.\n", pl.size);

    char cmdline[MAX_LINE];
    while (1) {
        printf("\n> ");
        if (!fgets(cmdline, sizeof(cmdline), stdin)) break;
        size_t len = strlen(cmdline); if (len && cmdline[len-1] == '\n') cmdline[len-1] = '\0';
        trim(cmdline);
        if (cmdline[0] == '\0') continue;

        /* tokenise command */
        char *tokens = strdup_safe(cmdline);
        char *tok = strtok(tokens, " ");
        if (!tok) { free(tokens); continue; }

        if (strcasecmp(tok, "add") == 0) {
            char *title = read_input_line("Title: ");
            char *artist = read_input_line("Artist: ");
            char *album = read_input_line("Album: ");
            char *dur_s = read_input_line("Duration (seconds): ");
            int dur = atoi(dur_s ? dur_s : "0");
            if (!title || !*title) { puts("Title required."); free(title); free(artist); free(album); free(dur_s); free(tokens); continue; }
            if (!artist) { artist = strdup_safe("Unknown"); }
            if (!album) { album = strdup_safe("Unknown"); }
            add_track(&pl, title, artist, album, dur);
            printf("Added: %s — %s\n", title, artist);
            free(title); free(artist); free(album); free(dur_s);
        } else if (strcasecmp(tok, "list") == 0) {
            list_playlist(&pl);
        } else if (strcasecmp(tok, "remove") == 0) {
            char *n = strtok(NULL, " ");
            int idx = parse_index_token(n, (int)pl.size);
            if (idx < 0) printf("Invalid index. Usage: remove N (1..%zu)\n", pl.size);
            else { remove_track_at(&pl, (size_t)idx); printf("Removed track %d.\n", idx+1); }
        } else if (strcasecmp(tok, "search") == 0) {
            char *term = strtok(NULL, "");
            if (!term) term = read_input_line("Search term: ");
            search_playlist(&pl, term);
            if (!strchr(cmdline, ' ')) free(term);
        } else if (strcasecmp(tok, "shuffle") == 0) {
            shuffle_playlist(&pl); printf("Playlist shuffled.\n");
        } else if (strcasecmp(tok, "sort") == 0) {
            char *kind = strtok(NULL, " ");
            if (!kind) { puts("sort title | artist | dur"); }
            else if (strcasecmp(kind, "title") == 0) { qsort(pl.items, pl.size, sizeof(Track), cmp_title); puts("Sorted by title."); }
            else if (strcasecmp(kind, "artist") == 0) { qsort(pl.items, pl.size, sizeof(Track), cmp_artist); puts("Sorted by artist."); }
            else if (strcasecmp(kind, "dur") == 0 || strcasecmp(kind, "duration") == 0) { qsort(pl.items, pl.size, sizeof(Track), cmp_duration); puts("Sorted by duration."); }
            else printf("Unknown sort key '%s'. Use title|artist|dur\n", kind);
        } else if (strcasecmp(tok, "play") == 0) {
            char *n = strtok(NULL, " ");
            int idx = parse_index_token(n, (int)pl.size);
            if (idx < 0) printf("Invalid index. Usage: play N (1..%zu)\n", pl.size);
            else play_track(&pl.items[idx]);
        } else if (strcasecmp(tok, "save") == 0) {
            char *file = strtok(NULL, " ");
            if (!file) file = DEFAULT_SAVE;
            if (save_playlist_csv(&pl, file)) printf("Saved to %s\n", file); else printf("Failed to save to %s\n", file);
        } else if (strcasecmp(tok, "load") == 0) {
            char *file = strtok(NULL, " ");
            if (!file) file = DEFAULT_SAVE;
            if (load_playlist_csv(&pl, file)) printf("Loaded (appended) from %s\n", file); else printf("Failed to load from %s\n", file);
        } else if (strcasecmp(tok, "clear") == 0) {
            for (size_t i = 0; i < pl.size; ++i) free_track(&pl.items[i]);
            pl.size = 0; puts("Playlist cleared.");
        } else if (strcasecmp(tok, "help") == 0) {
            print_help();
        } else if (strcasecmp(tok, "quit") == 0 || strcasecmp(tok, "exit") == 0) {
            save_playlist_csv(&pl, DEFAULT_SAVE);
            printf("Saved to %s. Bye!\n", DEFAULT_SAVE);
            free(tokens);
            break;
        } else {
            printf("Unknown command: %s. Type 'help' for commands.\n", tok);
        }

        free(tokens);
    }

    free_playlist(&pl);
    return 0;
}

