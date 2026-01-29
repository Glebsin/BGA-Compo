#include "bmflat.h"

#define STBI_ONLY_BMP
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

static int ends_with(const char *s, const char *ext)
{
  size_t ls = strlen(s);
  size_t le = strlen(ext);
  if (ls < le) return 0;
  return strcasecmp(s + ls - le, ext) == 0;
}

char *read_file(const char *path)
{
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;

  char *buf = NULL;
  do {
    if (fseek(f, 0, SEEK_END) != 0) break;
    long len = ftell(f);
    if (len <= 0) break;
    if (fseek(f, 0, SEEK_SET) != 0) break;
    buf = malloc(len);
    if (!buf) break;
    if (fread(buf, len, 1, f) != 1) {
      free(buf);
      buf = NULL;
      break;
    }
  } while (0);

  fclose(f);
  return buf;
}

char *strdupcat(const char *a, const char *b)
{
  if (!a) return strdup(b);
  size_t la = strlen(a), lb = strlen(b);
  char *r = malloc(la + lb + 1);
  if (!r) return NULL;
  memcpy(r, a, la);
  memcpy(r + la, b, lb);
  r[la + lb] = 0;
  return r;
}

char *strdupcat3(const char *a, const char *b, const char *c)
{
  if (!a) return strdupcat(b, c);
  size_t la = strlen(a), lb = strlen(b), lc = strlen(c);
  char *r = malloc(la + lb + lc + 1);
  if (!r) return NULL;
  memcpy(r, a, la);
  memcpy(r + la, b, lb);
  memcpy(r + la + lb, c, lc);
  r[la + lb + lc] = 0;
  return r;
}

int main(int argc, char **argv)
{
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stdin),  _O_BINARY);
#endif

  int arg = 1;
  int is_video = 1;

  if (arg < argc && argv[arg][0] == '-') {
    if (argv[arg][1] == 'a') is_video = 0;
    arg++;
  }
  int is_audio = !is_video;

  if (arg >= argc) return 1;

  const char *bms_path = argv[arg];

  char *bms_dir = strdup(bms_path);
  char *s1 = strrchr(bms_dir, '/');
  char *s2 = strrchr(bms_dir, '\\');
  char *sep = s1 > s2 ? s1 : s2;
  if (sep) *(sep + 1) = 0;
  else { free(bms_dir); bms_dir = NULL; }

  char *src = read_file(bms_path);
  if (!src) return 1;

  struct bm_chart chart;
  bm_load(&chart, src);

  int bw = -1, bh = -1;
  uint8_t *bitmaps[BM_INDEX_MAX] = {0};

  if (is_video) {
    for (int i = 0; i < BM_INDEX_MAX; i++) {
      const char *name = chart.tables.bmp[i];
      if (!name || !name[0]) continue;
      if (!ends_with(name, ".bmp")) continue;

      char *path = strdupcat(bms_dir, name);
      int w, h;
      uint8_t *pix = stbi_load(path, &w, &h, NULL, 3);
      free(path);

      if (!pix) continue;

      if (bw < 0) {
        bw = w;
        bh = h;
      } else if (w != bw || h != bh) {
        stbi_image_free(pix);
        continue;
      }

      bitmaps[i] = pix;
    }
  }

  const char *wave_exts[] = { ".ogg",".wav",".mp3",".OGG",".WAV",".MP3" };

  struct wave { int16_t *pcm; int len, ptr; } waves[BM_INDEX_MAX];

  for (int i = 0; i < BM_INDEX_MAX; i++) waves[i].ptr = -1;

  int ch = 2;
  int sr = 44100;

  if (is_audio) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_s16, ch, sr);
    for (int i = 0; i < BM_INDEX_MAX; i++) {
      if (!chart.tables.wav[i]) continue;
      char *ext = strrchr(chart.tables.wav[i], '.');
      if (ext) *ext = 0;
      for (int j = 0; j < 6; j++) {
        char *p = strdupcat3(bms_dir, chart.tables.wav[i], wave_exts[j]);
        ma_uint64 len;
        if (ma_decode_file(p, &cfg, &len, (void**)&waves[i].pcm) == MA_SUCCESS) {
          waves[i].len = (int)len;
          waves[i].ptr = -1;
          free(p);
          break;
        }
        free(p);
      }
    }
  }

  struct bm_seq seq;
  bm_to_seq(&chart, &seq);

  double fps = 30.0;
  double time = 0.0;
  double tempo = chart.meta.init_tempo;
  int bg = -1, fg = -1;
  int frames = 0;
  int samples = 0;

  for (int i = 0; i < seq.event_count; i++) {
    struct bm_event ev = seq.events[i];
    int dt = ev.pos - (i ? seq.events[i-1].pos : 0);
    time += dt * (60.0 / 48.0 / tempo);

    if (is_video) {
      while (frames < time * fps - 1e-6) {
        for (int p = 0; p < bw * bh; p++) {
          uint8_t pix[3] = {0};
          if (bg >= 0 && bitmaps[bg])
            memcpy(pix, &bitmaps[bg][p*3], 3);
          if (fg >= 0 && bitmaps[fg]) {
            uint8_t *f = &bitmaps[fg][p*3];
            if (f[0] || f[1] || f[2])
              memcpy(pix, f, 3);
          }
          putchar(pix[0]);
          putchar(pix[1]);
          putchar(pix[2]);
        }
        frames++;
      }
    }

    if (is_audio) {
      int ns = (int)(time * sr - 1e-6) - samples;
      if (ns > 0) {
        int32_t *buf = calloc(ns * ch, sizeof(int32_t));
        for (int w = 0; w < BM_INDEX_MAX; w++) {
          if (waves[w].ptr < 0) continue;
          for (int j = 0; j < ns && waves[w].ptr + j < waves[w].len; j++)
            for (int c = 0; c < ch; c++)
              buf[j*ch+c] += waves[w].pcm[(waves[w].ptr+j)*ch+c];
          waves[w].ptr += ns;
          if (waves[w].ptr >= waves[w].len) waves[w].ptr = -1;
        }
        for (int k = 0; k < ns*ch; k++) {
          int32_t s = buf[k] >> 1;
          if (s > INT16_MAX) s = INT16_MAX;
          if (s < INT16_MIN) s = INT16_MIN;
          putchar(s & 0xff);
          putchar((s >> 8) & 0xff);
        }
        free(buf);
      }
      samples += ns;
    }

    if (ev.type == BM_TEMPO_CHANGE) tempo = ev.value_f;
    else if (ev.type == BM_BGA_BASE_CHANGE) bg = ev.value;
    else if (ev.type == BM_BGA_LAYER_CHANGE) fg = ev.value;
    else if ((ev.type == BM_NOTE || ev.type == BM_NOTE_LONG) && is_audio)
      waves[ev.value].ptr = 0;
  }

  if (is_audio) {
    int active;
    do {
      active = 0;
      for (int i = 0; i < BM_INDEX_MAX; i++)
        if (waves[i].ptr >= 0) active = 1;

      if (!active) break;

      time += 1.0 / fps;

      int ns = (int)(time * sr - 1e-6) - samples;
      if (ns > 0) {
        int32_t *buf = calloc(ns * ch, sizeof(int32_t));
        for (int w = 0; w < BM_INDEX_MAX; w++) {
          if (waves[w].ptr < 0) continue;
          for (int j = 0; j < ns && waves[w].ptr + j < waves[w].len; j++)
            for (int c = 0; c < ch; c++)
              buf[j*ch+c] += waves[w].pcm[(waves[w].ptr+j)*ch+c];
          waves[w].ptr += ns;
          if (waves[w].ptr >= waves[w].len) waves[w].ptr = -1;
        }
        for (int k = 0; k < ns*ch; k++) {
          int32_t s = buf[k] >> 1;
          if (s > INT16_MAX) s = INT16_MAX;
          if (s < INT16_MIN) s = INT16_MIN;
          putchar(s & 0xff);
          putchar((s >> 8) & 0xff);
        }
        free(buf);
      }
      samples += ns;
    } while (active);
  }

  return 0;
}
