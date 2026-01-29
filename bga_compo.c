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

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

char *read_file(const char *path)
{
  FILE *f = fopen(path, "rb");
  if (f == NULL) return NULL;

  char *buf = NULL;

  do {
    if (fseek(f, 0, SEEK_END) != 0) break;
    long len = ftell(f);
    if (fseek(f, 0, SEEK_SET) != 0) break;
    if ((buf = (char *)malloc(len)) == NULL) break;
    if (fread(buf, len, 1, f) != 1) { free(buf); buf = NULL; break; }
  } while (0);

  fclose(f);
  return buf;
}

char *strdupcat(const char *s1, const char *s2)
{
  if (s1 == NULL) return strdup(s2);
  size_t len1 = strlen(s1);
  size_t len2 = strlen(s2);
  char *buf = (char *)malloc(len1 + len2 + 1);
  if (!buf) return NULL;
  memcpy(buf, s1, len1);
  memcpy(buf + len1, s2, len2);
  buf[len1 + len2] = '\0';
  return buf;
}

char *strdupcat3(const char *s1, const char *s2, const char *s3)
{
  if (s1 == NULL) return strdupcat(s2, s3);
  size_t len1 = strlen(s1);
  size_t len2 = strlen(s2);
  size_t len3 = strlen(s3);
  char *buf = (char *)malloc(len1 + len2 + len3 + 1);
  if (!buf) return NULL;
  memcpy(buf, s1, len1);
  memcpy(buf + len1, s2, len2);
  memcpy(buf + len1 + len2, s3, len3);
  buf[len1 + len2 + len3] = '\0';
  return buf;
}

int main(int argc, char *argv[])
{
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stdin),  _O_BINARY);
#endif

  int arg_ptr = 1;
  int is_video = 1;

  if (arg_ptr < argc && argv[arg_ptr][0] == '-') {
    if (argv[arg_ptr][1] == 'a') is_video = 0;
    else if (argv[arg_ptr][1] != 'v' && argv[arg_ptr][1] != '-') {
      fprintf(stderr, "Unrecognized option: %s\n", argv[arg_ptr]);
      return 1;
    }
    arg_ptr++;
  }

  int is_audio = !is_video;
  if (arg_ptr >= argc) {
    fprintf(stderr, "Usage: %s [-v|-a] <BMS file>\n", argv[0]);
    return 1;
  }

  const char *bms_path = argv[arg_ptr];

  char *bms_wdir = strdup(bms_path);
  char *d1 = strrchr(bms_wdir, '/');
  char *d2 = strrchr(bms_wdir, '\\');
  char *dirsep = d1 > d2 ? d1 : d2;

  if (dirsep) *(dirsep + 1) = '\0';
  else { free(bms_wdir); bms_wdir = NULL; }

  fprintf(stderr, "Loading chart\n");
  char *src = read_file(bms_path);
  if (!src) {
    fprintf(stderr, "Cannot read file %s\n", bms_path);
    return 1;
  }

  struct bm_chart chart;
  int msgs = bm_load(&chart, src);
  for (int i = 0; i < msgs; i++)
    fprintf(stderr, "Log: Line %d: %s\n", bm_logs[i].line, bm_logs[i].message);

  int bitmaps_w = -1, bitmaps_h = -1;
  uint8_t *bitmaps_pix[BM_INDEX_MAX] = {0};

  if (is_video) {
    fprintf(stderr, "Loading bitmaps\n");
    for (int i = 0; i < BM_INDEX_MAX; i++) {
      if (!chart.tables.bmp[i]) continue;

      char *bmp_path = strdupcat(bms_wdir, chart.tables.bmp[i]);
      int w, h;
      bitmaps_pix[i] = stbi_load(bmp_path, &w, &h, NULL, 3);
      free(bmp_path);

      if (!bitmaps_pix[i]) {
        fprintf(stderr, "Cannot load bitmap %s\n", chart.tables.bmp[i]);
        return 1;
      }

      if (bitmaps_w < 0) {
        bitmaps_w = w;
        bitmaps_h = h;
        fprintf(stderr, "Image size is %dx%d\n", w, h);
      } else if (w != bitmaps_w || h != bitmaps_h) {
        fprintf(stderr, "Bitmap %s has different size %dx%d\n",
                chart.tables.bmp[i], w, h);
        return 1;
      }
    }
  }

  const char *wave_exts[] = {
    ".ogg",".wav",".mp3",".OGG",".WAV",".MP3"
  };

  struct wave { int16_t *pcm; int len, ptr; } waves[BM_INDEX_MAX] = {0};

  int n_ch = 2;
  int sample_rate = 44100;

  if (is_audio) {
    fprintf(stderr, "Loading waves\n");
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_s16, n_ch, sample_rate);

    for (int i = 0; i < BM_INDEX_MAX; i++) {
      if (!chart.tables.wav[i]) continue;

      char *ext = strrchr(chart.tables.wav[i], '.');
      if (ext) *ext = '\0';

      int ok = 0;
      for (int j = 0; j < (int)(sizeof wave_exts / sizeof wave_exts[0]); j++) {
        char *p = strdupcat3(bms_wdir, chart.tables.wav[i], wave_exts[j]);
        ma_uint64 len;
        if (ma_decode_file(p, &cfg, &len, (void**)&waves[i].pcm) == MA_SUCCESS) {
          waves[i].len = (int)len;
          ok = 1;
          free(p);
          break;
        }
        free(p);
      }

      if (!ok) {
        fprintf(stderr, "Cannot load wave %s\n", chart.tables.wav[i]);
        return 1;
      }
    }

    for (int i = 0; i < BM_INDEX_MAX; i++) waves[i].ptr = -1;
  }

  struct bm_seq seq;
  bm_to_seq(&chart, &seq);

  double fps = 30.0;
  int n_frames = 0;
  int n_samples = 0;

  double time = 0.0;
  double tempo = chart.meta.init_tempo;
  int bg = -1, fg = -1;

  for (int i = 0; i < seq.event_count; i++) {
    struct bm_event ev = seq.events[i];
    int delta = ev.pos - (i ? seq.events[i-1].pos : 0);
    time += delta * (60.0 / 48.0 / tempo);

    if (is_video) {
      while (n_frames < time * fps - 1e-6) {
        for (int p = 0; p < bitmaps_w * bitmaps_h; p++) {
          uint8_t pix[3] = {0};

          if (bg >= 0 && bitmaps_pix[bg]) {
            memcpy(pix, &bitmaps_pix[bg][p*3], 3);
          }

          if (fg >= 0 && bitmaps_pix[fg]) {
            uint8_t *f = &bitmaps_pix[fg][p*3];
            if (f[0] || f[1] || f[2])
              memcpy(pix, f, 3);
          }

          putchar(pix[0]);
          putchar(pix[1]);
          putchar(pix[2]);
        }
        n_frames++;
      }
    }

    if (is_audio) {
      int new_samples = (int)(time * sample_rate - 1e-6) - n_samples;
      if (new_samples > 0) {
        int32_t *buf = calloc(new_samples * n_ch, sizeof(int32_t));
        for (int w = 0; w < BM_INDEX_MAX; w++) {
          if (waves[w].ptr < 0) continue;
          for (int j = 0; j < new_samples && waves[w].ptr + j < waves[w].len; j++)
            for (int c = 0; c < n_ch; c++)
              buf[j*n_ch+c] += waves[w].pcm[(waves[w].ptr+j)*n_ch+c];
          waves[w].ptr += new_samples;
          if (waves[w].ptr >= waves[w].len) waves[w].ptr = -1;
        }

        for (int i = 0; i < new_samples*n_ch; i++) {
          int32_t s = buf[i] >> 1;
          if (s > INT16_MAX) s = INT16_MAX;
          if (s < INT16_MIN) s = INT16_MIN;
          putchar(s & 0xff);
          putchar((s >> 8) & 0xff);
        }
        free(buf);
      }
      n_samples += new_samples;
    }

    if (ev.type == BM_TEMPO_CHANGE) tempo = ev.value_f;
    else if (ev.type == BM_BGA_BASE_CHANGE) bg = ev.value;
    else if (ev.type == BM_BGA_LAYER_CHANGE) fg = ev.value;
    else if (ev.type == BM_NOTE || ev.type == BM_NOTE_LONG) waves[ev.value].ptr = 0;
  }

  return 0;
}
