#include "lifetime_stats.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef ANDROID
#include "../android_path.h"
#endif

#define LIFETIME_STATS_FILE "lifetime_stats.dat"
#define LIFETIME_STATS_MAGIC_V1 0x4C53544Cu
#define LIFETIME_STATS_MAGIC_V2 0x4C535432u

typedef struct {
  uint32_t magic;
  int32_t best_length;
  int32_t total_kills;
} lifetime_stats_file_v1;

typedef struct {
  uint32_t magic;
  int32_t best_length;
  int32_t total_kills;
  int32_t best_match_kills;
} lifetime_stats_file_v2;

static int s_best_length = 0;
static int s_total_kills = 0;
static int s_best_match_kills = 0;
static bool s_loaded = false;
static bool s_dirty = false;

static FILE* open_file(const char* mode) {
#ifdef ANDROID
  char path[512];
  android_build_path(path, sizeof(path), LIFETIME_STATS_FILE);
  return fopen(path, mode);
#else
  return fopen(LIFETIME_STATS_FILE, mode);
#endif
}

void lifetime_stats_load(void) {
  if (s_loaded) return;
  s_loaded = true;
  FILE* f = open_file("rb");
  if (!f) return;
  unsigned char raw[sizeof(lifetime_stats_file_v2)];
  size_t got = fread(raw, 1, sizeof raw, f);
  fclose(f);
  uint32_t magic = 0;
  if (got >= sizeof magic) memcpy(&magic, raw, sizeof magic);
  if (magic == LIFETIME_STATS_MAGIC_V2 && got >= sizeof(lifetime_stats_file_v2)) {
    lifetime_stats_file_v2 d;
    memcpy(&d, raw, sizeof d);
    s_best_length = d.best_length > 0 ? d.best_length : 0;
    s_total_kills = d.total_kills > 0 ? d.total_kills : 0;
    s_best_match_kills = d.best_match_kills > 0 ? d.best_match_kills : 0;
  } else if (magic == LIFETIME_STATS_MAGIC_V1 &&
             got >= sizeof(lifetime_stats_file_v1)) {
    lifetime_stats_file_v1 d;
    memcpy(&d, raw, sizeof d);
    s_best_length = d.best_length > 0 ? d.best_length : 0;
    s_total_kills = d.total_kills > 0 ? d.total_kills : 0;
  }
}

int lifetime_stats_best_length(void) {
  lifetime_stats_load();
  return s_best_length;
}

int lifetime_stats_total_kills(void) {
  lifetime_stats_load();
  return s_total_kills;
}

int lifetime_stats_best_match_kills(void) {
  lifetime_stats_load();
  return s_best_match_kills;
}

void lifetime_stats_note_length(int length) {
  lifetime_stats_load();
  if (length > s_best_length) {
    s_best_length = length;
    s_dirty = true;
  }
}

void lifetime_stats_add_kill(void) {
  lifetime_stats_load();
  s_total_kills++;
  s_dirty = true;
}

void lifetime_stats_note_match_kills(int kills) {
  lifetime_stats_load();
  if (kills > s_best_match_kills) {
    s_best_match_kills = kills;
    s_dirty = true;
  }
}

void lifetime_stats_save_if_dirty(void) {
  if (!s_dirty) return;
  FILE* f = open_file("wb");
  if (!f) return;
  lifetime_stats_file_v2 d = {LIFETIME_STATS_MAGIC_V2, s_best_length,
                              s_total_kills, s_best_match_kills};
  fwrite(&d, sizeof d, 1, f);
  fclose(f);
  s_dirty = false;
}
