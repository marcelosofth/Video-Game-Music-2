#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

#include <libretro.h>
#include <retro_miscellaneous.h>
#include <streams/file_stream.h>

#include "graphics.h"
#include "player.h"
#include "spectrum.h"
#include "credits_bg.h"
#include "credits_music.h"
#include "credits_logo.h"
#include "credits_sonic.h"
#include "credits_sprites.h"
#include "xevious_ship.h"
#include "galaga_ship.h"
#include "raiden_ship.h"
#include "viper_ship.h"
#include "pulstar_ship.h"
#include "rtype_ship.h"
#include "choplifter_ship.h"
#include "rallyx_cars.h"
#include "arthur_run.h"
#include "mario_run.h"
#include "sonic_run.h"
#include "car_run.h"
#include "runners_extra.h"
#include "shinobi_run.h"
#include "alex_run.h"

// Static globals
static surface *framebuffer = NULL;
static uint16_t previnput = 0;
static int browser_open_state = 0;
static int browser_cursor      = 0;
static int browser_count_ui    = 0;
static int l_active_ = 0;
static int r_active_ = 0;
static int credits_open_state = 0;
static int credits_frame      = 0;
static int credits_anim       = 0;   /* frames desde que a janela abriu (anima o sprite; nao para com o B) */
static int credits_paused     = 0;   /* 1 = texto dos creditos parado (B pausa/continua) */

// Callbacks

retro_log_printf_t log_cb;

static retro_video_refresh_t video_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_environment_t environ_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
// libretro global setters
void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

void retro_set_environment(retro_environment_t cb)
{
   struct retro_vfs_interface_info vfs_iface_info;
   environ_cb = cb;

   vfs_iface_info.required_interface_version = 1;
   vfs_iface_info.iface                      = NULL;
   if (cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &vfs_iface_info))
      filestream_vfs_init(&vfs_iface_info);
}

void retro_set_audio_sample(retro_audio_sample_t cb) { audio_cb = cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }

unsigned retro_api_version(void) { return RETRO_API_VERSION; }
unsigned retro_get_region(void) { return RETRO_REGION_PAL; }

// Serialisation methods
size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void *data, size_t size) { return false; }
bool retro_unserialize(const void *data, size_t size) { return false; }

// libretro unused api functions
void retro_set_controller_port_device(unsigned port, unsigned device) {}
void *retro_get_memory_data(unsigned id) { return NULL; }
size_t retro_get_memory_size(unsigned id){ return 0; }

// Cheats
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) {}

/* ---- desenho de texto (sem alocar memoria, com clip) ---- */

static void put_glyph(unsigned char ch, int px, int py, unsigned short color, int cx0, int cx1)
{
   int x, y, sx, sy;
   int charx = (ch % 16) * 8;
   int chary = (ch >> 4) * 8;
   for (y = 0; y < 8; y++)
   {
      for (x = 0; x < 8; x++)
      {
         if (!is_font_pixel(charx + x, chary + y))
            continue;
         for (sy = 0; sy < 2; sy++)
         {
            int yy = py + y * 2 + sy;
            if (yy < 0 || yy >= 480)
               continue;
            for (sx = 0; sx < 2; sx++)
            {
               int xx = px + x * 2 + sx;
               if (xx < cx0 || xx >= cx1 || xx < 0 || xx >= 640)
                  continue;
               set_pixel(framebuffer, xx, yy, color);
            }
         }
      }
   }
}

/* colunas realmente usadas pelo glifo (para espacamento proporcional) */
static void glyph_extent(unsigned char ch, int *x0, int *x1)
{
   int x, y;
   int charx = (ch % 16) * 8;
   int chary = (ch >> 4) * 8;
   *x0 = 8;
   *x1 = -1;
   for (x = 0; x < 8; x++)
      for (y = 0; y < 8; y++)
         if (is_font_pixel(charx + x, chary + y))
         {
            if (x < *x0) *x0 = x;
            if (x > *x1) *x1 = x;
         }
}

static int text_width_prop(const char *text)
{
   int w = 0, x0, x1;
   for (; *text; text++)
   {
      glyph_extent((unsigned char)*text, &x0, &x1);
      w += (x1 < 0) ? 4 : (x1 - x0 + 2);
   }
   w = w > 0 ? w - 1 : 0;
   return w * 2;
}

static void draw_text_prop(const char *text, int x, int y, unsigned short color)
{
   int x0, x1;
   for (; *text; text++)
   {
      unsigned char ch = (unsigned char)*text;
      glyph_extent(ch, &x0, &x1);
      if (x1 < 0)
      {
         x += 4 * 2;
         continue;
      }
      put_glyph(ch, x - x0 * 2, y, color, 0, 640);
      x += (x1 - x0 + 2) * 2;
   }
}

/* texto monoespacado; se passar de maxw, rola de um lado para o outro */
static void draw_text_scroll(const char *text, int x, int y, int maxw, unsigned short color)
{
   int len   = (int)strlen(text);
   int textw = len * 8 * 2;
   int off   = 0, i;

   if (textw > maxw)
   {
      int delta  = textw - maxw;
      int delay  = 30;
      int modulo = delta + delay * 2;
      int frames = get_track_elapsed_frames();
      off = (modulo - abs((frames / 2) % (2 * modulo) - modulo)) - delay;
      if (off < 0)     off = 0;
      if (off > delta) off = delta;
   }

   for (i = 0; i < len; i++)
   {
      int gx = x - off + i * 8 * 2;
      if (gx + 8 * 2 <= x || gx >= x + maxw)
         continue;
      put_glyph((unsigned char)text[i], gx, y, color, x, x + maxw);
   }
}

/* texto proporcional cortado em maxx */
static void draw_text_prop_clip(const char *text, int x, int y, unsigned short color, int maxx)
{
   int x0, x1;
   for (; *text && x < maxx; text++)
   {
      unsigned char ch = (unsigned char)*text;
      glyph_extent(ch, &x0, &x1);
      if (x1 < 0)
      {
         x += 4 * 2;
         continue;
      }
      put_glyph(ch, x - x0 * 2, y, color, 0, maxx);
      x += (x1 - x0 + 2) * 2;
   }
}

/* LED de atividade: acende com o volume do audio e decai rapido */
static int led_level = 0;

static void led_update(const short *audio, int n)
{
   int i, pk = 0, cur;
   for (i = 0; i < n; i++)
   {
      int v = audio[i];
      if (v < 0) v = -v;
      if (v > pk) pk = v;
   }
   cur = pk / 40;
   if (cur > 255) cur = 255;
   led_level = (led_level * 192) >> 8;
   if (cur > led_level)
      led_level = cur;
}

static void draw_led(int cx, int cy)
{
   int dx, dy;
   int t = led_level;
   int r = 8 + (23 * t) / 255, g = 3 + (9 * t) / 255, b = 2 + (5 * t) / 255;
   unsigned short col;
   if (t < 48) /* em repouso/fraco: escurece ate o preto; acima disso fica igual */
   {
      r = (r * t) / 48;
      g = (g * t) / 48;
      b = (b * t) / 48;
   }
   if (t < 4) /* sem som: preto */
      col = get_color(0, 0, 0);
   else
      col = get_color(r, g, b);
   for (dy = -6; dy <= 6; dy++)
      for (dx = -6; dx <= 6; dx++)
         if (dx * dx + dy * dy <= 40)
            set_pixel(framebuffer, cx + dx, cy + dy, col);
   if (t > 200) /* brilho branco do LED: 2x2 pixels (antes era 1) */
   {
      unsigned short wh = get_color(31, 40, 30);
      set_pixel(framebuffer, cx - 3, cy - 3, wh);
      set_pixel(framebuffer, cx - 2, cy - 3, wh);
      set_pixel(framebuffer, cx - 3, cy - 2, wh);
      set_pixel(framebuffer, cx - 2, cy - 2, wh);
   }
}

/* versao reduzida do mesmo LED (mesma cor/pulsar de led_level), so que menor -
 * usada para sobrepor a bolinha do "joystick" no logo (que e parte do fundo). */
static void draw_led_mini(int cx, int cy, int r)
{
   int dx, dy;
   int t = led_level;
   int rr = 8 + (23 * t) / 255, gg = 3 + (9 * t) / 255, bb = 2 + (5 * t) / 255;
   unsigned short col;
   if (t < 48)
   {
      rr = (rr * t) / 48;
      gg = (gg * t) / 48;
      bb = (bb * t) / 48;
   }
   col = (t < 4) ? get_color(0, 0, 0) : get_color(rr, gg, bb);
   for (dy = -r; dy <= r; dy++)
      for (dx = -r; dx <= r; dx++)
         if (dx * dx + dy * dy <= r * r)
            set_pixel(framebuffer, cx + dx, cy + dy, col);
   if (t > 200 && r >= 3)
   {
      unsigned short wh = get_color(31, 40, 30);
      set_pixel(framebuffer, cx, cy - 1, wh);
   }
}

/* ---- layout: cabecalho / espectro / progresso ---- */
#define UI_LEFT   32
#define UI_RIGHT  608
#define UI_W      (UI_RIGHT - UI_LEFT)

/* Cabecalho: 4 linhas, uma por faixa do fundo (y = topo do texto).
 * 1) chip  2) sistema  3) jogo  4) faixa + Track N/M.
 * HDR_TEXT_MAX_X: limite direito das linhas 1-3 (o logo comeca depois). */
#define HDR_Y1 24
#define HDR_Y2 60
#define HDR_Y3 96
#define HDR_Y4 132
#define HDR_TEXT_MAX_X 416

/* ---- fonte do CHIP: Press Start 2P (src/font_chip.h) com degrade vertical ---- */
#include "font_chip.h"

#define CHIP_SPACE 5   /* largura do espaco (px do atlas) */

static unsigned short chip_grad[8];

static void chip_grad_init(void)
{
   /* degrade da imagem de referencia: azul -> branco (topo), dourado -> creme (base) */
   static const unsigned char rgb[8][3] = {
      { 69, 149, 204}, {135, 190, 231}, {212, 237, 253}, {255, 255, 255},
      {183, 129,  41}, {229, 185,  87}, {252, 245, 232}, {252, 245, 232}
   };
   int i;
   for (i = 0; i < 8; i++)
      chip_grad[i] = get_color(rgb[i][0] >> 3, rgb[i][1] >> 2, rgb[i][2] >> 3);
}

static int chip_pixel(unsigned char ch, int x, int y)
{
   const unsigned short *p = (const unsigned short *)font_chip.pixel_data;
   return p[(ch % 16) * 8 + x + ((ch >> 4) * 8 + y) * 128] == 0;
}

static void chip_extent(unsigned char ch, int *x0, int *x1)
{
   int x, y;
   *x0 = 8;
   *x1 = -1;
   for (x = 0; x < 8; x++)
      for (y = 0; y < 8; y++)
         if (chip_pixel(ch, x, y))
         {
            if (x < *x0) *x0 = x;
            if (x > *x1) *x1 = x;
         }
}

static void chip_put_glyph(unsigned char ch, int px, int py, int cx1)
{
   int x, y, sx, sy;
   for (y = 0; y < 8; y++)
      for (x = 0; x < 8; x++)
      {
         if (!chip_pixel(ch, x, y))
            continue;
         for (sy = 0; sy < 2; sy++)
         {
            int yy = py + y * 2 + sy;
            if (yy < 0 || yy >= 480)
               continue;
            for (sx = 0; sx < 2; sx++)
            {
               int xx = px + x * 2 + sx;
               if (xx < 0 || xx >= 640 || xx >= cx1)
                  continue;
               set_pixel(framebuffer, xx, yy, chip_grad[y]);
            }
         }
      }
}

static int chip_text_width(const char *text)
{
   int w = 0, x0, x1;
   for (; *text; text++)
   {
      chip_extent((unsigned char)*text, &x0, &x1);
      w += (x1 < 0) ? CHIP_SPACE : (x1 - x0 + 2);
   }
   w = w > 0 ? w - 1 : 0;
   return w * 2;
}

/* maxx = limite direito absoluto (x) */
static void chip_draw_text(const char *text, int x, int y, int maxx)
{
   int x0, x1;
   chip_grad_init();
   for (; *text && x < maxx; text++)
   {
      unsigned char ch = (unsigned char)*text;
      chip_extent(ch, &x0, &x1);
      if (x1 < 0)
      {
         x += CHIP_SPACE * 2;
         continue;
      }
      chip_put_glyph(ch, x - x0 * 2, y, maxx);
      x += (x1 - x0 + 2) * 2;
   }
}

/* ---- barra de transporte: Prev / Stop / Play / Pause / Loop / Next ---- */

static void draw_bar(int x, int y, int w, int h, unsigned short color)
{
   draw_shape(framebuffer, color, x, y, w, h);
}

/* triangulo generico: de x_tip (bico, altura 0) ate x_base (base, altura 2*half_h+1) */
static void draw_tri(int x_tip, int x_base, int cy, int half_h, unsigned short color)
{
   int width = x_base - x_tip;
   int steps = abs(width);
   int i;
   if (steps == 0)
      return;
   for (i = 0; i <= steps; i++)
   {
      int x = x_tip + (width >= 0 ? i : -i);
      int h = (half_h * i) / steps;
      int y;
      for (y = -h; y <= h; y++)
         set_pixel(framebuffer, x, cy + y, color);
   }
}

static void draw_loop_icon(int cx, int cy, int r, unsigned short color)
{
   int dx, dy;
   for (dy = -r; dy <= r; dy++)
      for (dx = -r; dx <= r; dx++)
      {
         int d2 = dx * dx + dy * dy;
         if (d2 <= r * r && d2 > (r - 3) * (r - 3))
         {
            if (dy > r / 3 && dx > 0) /* abertura para a seta */
               continue;
            set_pixel(framebuffer, cx + dx, cy + dy, color);
         }
      }
   draw_tri(cx + r - 8, cx + r - 2, cy + r - 4, 4, color);
}

#define TRANS_Y  449
#define ICON_HH  6   /* meia-altura padrao dos icones (altura total 12, igual a foto) */

static void draw_transport_bar(void)
{
   static const int off[6] = { -110, -57, -18, 18, 57, 110 };
   int cx = (UI_LEFT + UI_RIGHT) / 2;
   int cy = TRANS_Y;
   int ix;
   unsigned short dim  = get_color(9, 20, 12);
   unsigned short lit  = get_color(8, 56, 17);
   bool playing  = get_is_playing();
   bool stopped  = get_is_stopped();
   bool paused   = !playing && !stopped;
   bool loop_on  = get_loop_enabled();
   unsigned short prev_col = l_active_ ? lit : dim;
   unsigned short next_col = r_active_ ? lit : dim;

   /* prev: barra + triangulo apontando p/ esquerda */
   ix = cx + off[0];
   draw_bar(ix - 6, cy - ICON_HH, 2, ICON_HH * 2, prev_col);
   draw_tri(ix - 4, ix + 5, cy, ICON_HH, prev_col);

   /* stop: quadrado */
   ix = cx + off[1];
   draw_bar(ix - 6, cy - 6, 12, 12, stopped ? lit : dim);

   /* play: triangulo apontando p/ direita */
   ix = cx + off[2];
   draw_tri(ix + 5, ix - 5, cy, ICON_HH, playing ? lit : dim);

   /* pause: duas barras */
   ix = cx + off[3];
   draw_bar(ix - 4, cy - ICON_HH, 2, ICON_HH * 2, paused ? lit : dim);
   draw_bar(ix + 2, cy - ICON_HH, 2, ICON_HH * 2, paused ? lit : dim);

   /* loop */
   ix = cx + off[4];
   draw_loop_icon(ix, cy, 8, loop_on ? lit : dim);

   /* next: triangulo apontando p/ direita + barra */
   ix = cx + off[5];
   draw_tri(ix + 3, ix - 5, cy, ICON_HH, next_col);
   draw_bar(ix + 5, cy - ICON_HH, 2, ICON_HH * 2, next_col);
}

/* posicao da bolinha do "joystick" no logo (medida na tela 640x480; ajuste
   fino se nao bater exatamente com a arte do seu fundo) */
#define LOGO_LED_X 534
#define LOGO_LED_Y 64
#define LOGO_LED_R 4

/* texto 2x com DIGITOS DE LARGURA FIXA (celula = digito mais largo), para o
 * tempo nao "tremer" quando muda de numero */
static int tab_digit_cell(void)
{
   static int cell = 0;
   if (!cell)
   {
      int d, x0, x1;
      for (d = '0'; d <= '9'; d++)
      {
         glyph_extent((unsigned char)d, &x0, &x1);
         if (x1 >= 0 && (x1 - x0 + 2) * 2 > cell)
            cell = (x1 - x0 + 2) * 2;
      }
      if (!cell)
         cell = 12;
   }
   return cell;
}

static void draw_text_tab(const char *text, int x, int y, unsigned short color)
{
   int x0, x1;
   for (; *text; text++)
   {
      unsigned char ch = (unsigned char)*text;
      glyph_extent(ch, &x0, &x1);
      if (x1 < 0)
      {
         x += 4 * 2;
         continue;
      }
      if (ch >= '0' && ch <= '9')
      {
         int cell = tab_digit_cell();
         put_glyph(ch, x + (cell - (x1 - x0 + 2) * 2) / 2 - x0 * 2, y, color, 0, 640);
         x += cell;
      }
      else
      {
         put_glyph(ch, x - x0 * 2, y, color, 0, 640);
         x += (x1 - x0 + 2) * 2;
      }
   }
}

static void draw_ui(void)
{
   char message[512];
   int w, prog;

   /* cabecalho: linha 4 = faixa (esq) + Track N/M (dir) */
   get_track_label(message);
   w = text_width_prop(message);
   draw_text_prop(message, UI_RIGHT - w, HDR_Y4, get_color(28, 56, 28));
   draw_text_scroll(get_song_name(message), UI_LEFT, HDR_Y4, UI_W - w - 16, get_color(30, 58, 25));

   /* linha 3 = jogo */
   draw_text_scroll(get_game_name(message), UI_LEFT, HDR_Y3, HDR_TEXT_MAX_X - UI_LEFT, get_color(19, 36, 27));

   /* linha 2 = sistema */
   get_system_line(message);
   draw_text_prop_clip(message, UI_LEFT, HDR_Y2, get_color(15, 33, 21), HDR_TEXT_MAX_X);

   /* linha 1 = CHIP: (led) chip de audio - Press Start 2P com degrade */
   {
      int lx = UI_LEFT + chip_text_width("CHIP:") + 16;
      chip_draw_text("CHIP:", UI_LEFT, HDR_Y1, HDR_TEXT_MAX_X);
      draw_led(lx + 6, HDR_Y1 + 6);
      chip_draw_text(get_chip_text(message), lx + 24, HDR_Y1, HDR_TEXT_MAX_X);
   }

   /* copia menor do LED, por cima da bolinha do joystick no logo (fundo) */
   draw_led_mini(LOGO_LED_X, LOGO_LED_Y, LOGO_LED_R);

   /* barra de progresso (proxima ao espectro, cuja base fica em y=408) */
   draw_shape(framebuffer, get_color(4, 8, 9), UI_LEFT, 413, UI_W, 12);
   prog = get_track_progress_permille();
   if (prog > 0)
      draw_shape(framebuffer, get_color(12, 25, 28), UI_LEFT, 413, (UI_W * prog) / 1000, 12);

   /* tempo (esquerda) e taxa de amostragem (direita) */
   {
      int em = 0, es = 0, tmn = 0, tsc = 0, ecs = 0;
      int fr = get_track_elapsed_frames();
      char tbuf[64];
      get_time_text(message);
      if (sscanf(message, "%d:%d/%d:%d", &em, &es, &tmn, &tsc) == 4)
      {
         if (fr / 60 == em * 60 + es)
            ecs = (fr % 60) * 100 / 60;
         snprintf(tbuf, sizeof(tbuf), "%d:%02d:%02d/%d:%02d", em, es, ecs, tmn, tsc);
         draw_text_tab(tbuf, UI_LEFT, 442, get_color(27, 54, 29));
      }
      else
         draw_text_prop(message, UI_LEFT, 442, get_color(27, 54, 29));
   }

   get_rate_text(message);
   w = text_width_prop(message);
   draw_text_prop(message, UI_RIGHT - w, 442, get_color(15, 33, 21));

   draw_transport_bar();
}

/* ---- osciloscopio estereo (espaco entre o cabecalho e o espectro) ---- */
#define WAVE_TOP  167
#define WAVE_H    48
#define WAVE_GAP  10

/* Degrade de transparencia do osciloscopio roxo (canal de baixo) em volta dos 2 VU meters:
 * no centro de cada VU a linha fica quase transparente (WAVE_FADE_MIN) e volta a ficar opaca
 * a WAVE_FADE_R pixels de distancia, com transicao suave. */
#define WAVE_FADE_L_CX  90     /* centro do VU esquerdo (mesmo valor de VU_L_CX) */
#define WAVE_FADE_R_CX  550    /* centro do VU direito  (mesmo valor de VU_R_CX) */
#define WAVE_FADE_HALF_BOX 30  /* raio em volta do centro do VU que fica bem apagado */
#define WAVE_FADE_R     40     /* largura da transicao suave ate quase a borda da caixa do VU, em px */
#define WAVE_FADE_MIN   40     /* opacidade minima no centro do VU (0 = some, 255 = opaca) */

static int wave_fade_alpha(int x)
{
   int dl = abs(x - WAVE_FADE_L_CX), dr = abs(x - WAVE_FADE_R_CX);
   int d = dl < dr ? dl : dr, t, sm;
   if (d <= WAVE_FADE_HALF_BOX)
      return WAVE_FADE_MIN;                              /* dentro da caixa do VU: totalmente apagado */
   d -= WAVE_FADE_HALF_BOX;
   if (d >= WAVE_FADE_R)
      return 255;
   t  = d * 256 / WAVE_FADE_R;                        /* 0..255 */
   sm = ((t * t) >> 8) * (768 - 2 * t) >> 8;          /* smoothstep 0..256 */
   return WAVE_FADE_MIN + ((255 - WAVE_FADE_MIN) * sm) / 256;
}

static void wave_plot_fade(int x, int y, unsigned short color)
{
   uint16_t *fb = (uint16_t *)framebuffer->pixel_data;
   uint16_t *dst;
   int a, ir, ig, ib, br, bg, bb;
   unsigned short bgc;
   if (x < 0 || x >= 640 || y < 0 || y >= 480)
      return;
   a   = wave_fade_alpha(x);
   dst = &fb[x + y * framebuffer->width];
   bgc = *dst;
   ir = (color >> 11) & 31; ig = (color >> 5) & 63; ib = color & 31;
   br = (bgc   >> 11) & 31; bg = (bgc   >> 5) & 63; bb = bgc  & 31;
   *dst = get_color((ir * a + br * (255 - a)) / 255,
                    (ig * a + bg * (255 - a)) / 255,
                    (ib * a + bb * (255 - a)) / 255);
}

static void wave_line_fade(int x0, int y0, int x1, int y1, unsigned short color)
{
   int dx = abs(x1 - x0), dy = -abs(y1 - y0);
   int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
   int err = dx + dy, e2;
   for (;;)
   {
      wave_plot_fade(x0, y0, color);
      if (x0 == x1 && y0 == y1)
         break;
      e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
   }
}

static void draw_wave_channel(const short *audio, int frames, int offset,
                               int y0, int h, unsigned short color, int gain_q8, int fade)
{
   int x, px = -1, py = 0;
   int mid = y0 + h / 2;

   draw_line(framebuffer, get_color(4, 7, 8), UI_LEFT, mid, UI_RIGHT, mid);

   for (x = 0; x < UI_W; x++)
   {
      int idx = (x * frames) / UI_W;
      int s   = audio[idx * 2 + offset];
      int v = (s * gain_q8) >> 8;
      int y = mid - (v * (h / 2)) / 32768;
      if (y < y0)         y = y0;
      if (y >= y0 + h)     y = y0 + h - 1;
      if (px >= 0)
      {
         if (fade)
            wave_line_fade(UI_LEFT + px, py, UI_LEFT + x, y, color);
         else
            draw_line(framebuffer, color, UI_LEFT + px, py, UI_LEFT + x, y);
      }
      px = x;
      py = y;
   }
}

/* Janela do osciloscopio: WAVE_SPAN amostras (por canal) mais recentes.
 * 735 = 1 quadro (~16,7 ms); maior = mais oscilacoes visiveis na tela.
 * WAVE_RING deve ser potencia de 2 e maior que WAVE_SPAN. */
#define WAVE_SPAN 1470
#define WAVE_RING 8192

static short wave_ring[WAVE_RING * 2];
static short wave_lin[WAVE_SPAN * 2];
static unsigned wave_pos = 0;

/* Ganho automatico do osciloscopio (mais movimento nas ondas) */
#define WAVE_TARGET   28000   /* pico alvo (max 32767) */
#define WAVE_MAX_GAIN 12      /* ganho maximo (x) */
#define WAVE_MIN_PEAK 1500    /* abaixo disso nao amplia mais (evita ruido) */

static int wave_peak[2] = { 0, 0 };

static void draw_waveform(const short *audio, int frames)
{
   int i, ch;
   int gain[2];
   unsigned start;

   for (i = 0; i < frames; i++)
   {
      unsigned p = wave_pos & (WAVE_RING - 1);
      wave_ring[p * 2]     = audio[i * 2];
      wave_ring[p * 2 + 1] = audio[i * 2 + 1];
      wave_pos++;
   }

   start = wave_pos - WAVE_SPAN;
   for (i = 0; i < WAVE_SPAN; i++)
   {
      unsigned p = (start + (unsigned)i) & (WAVE_RING - 1);
      wave_lin[i * 2]     = wave_ring[p * 2];
      wave_lin[i * 2 + 1] = wave_ring[p * 2 + 1];
   }

   for (ch = 0; ch < 2; ch++)
   {
      int pk = 0;
      for (i = 0; i < WAVE_SPAN; i++)
      {
         int s = wave_lin[i * 2 + ch];
         if (s < 0) s = -s;
         if (s > pk) pk = s;
      }
      wave_peak[ch] -= wave_peak[ch] / 32;      /* decai devagar */
      if (pk > wave_peak[ch]) wave_peak[ch] = pk;
      pk = wave_peak[ch];
      if (pk < WAVE_MIN_PEAK) pk = WAVE_MIN_PEAK;
      gain[ch] = (WAVE_TARGET * 256) / pk;      /* ponto fixo 8.8 */
      if (gain[ch] > WAVE_MAX_GAIN * 256) gain[ch] = WAVE_MAX_GAIN * 256;
      if (gain[ch] < 256) gain[ch] = 256;
   }

   draw_wave_channel(wave_lin, WAVE_SPAN, 0, WAVE_TOP, WAVE_H, get_color(10, 28, 31), gain[0], 0);
   draw_wave_channel(wave_lin, WAVE_SPAN, 1, WAVE_TOP + WAVE_H + WAVE_GAP, WAVE_H, get_color(28, 12, 27), gain[1], 1);
}


/* ---- VU meters estereo (2 ponteiros analogicos: L e R) ----
 * Posicao: um em cada lado, entre o osciloscopio e o espectro.
 * Ajuste fino: VU_L_CX / VU_R_CX / VU_CY (centro de cada medidor, em pixels).
 * VU_REF_DB: nivel (dBFS) que corresponde a "0" do VU; menor = ponteiro sobe mais. */
#define VU_L_CX    90
#define VU_R_CX    550
#define VU_CY      252
#define VU_BOX_W   124
#define VU_BOX_H   90
#define VU_R       58
#define VU_ANG     55.0f
#define VU_REF_DB  14.0f
#define VU_HIT_TOL 0.04f   /* tolerancia (na escala 0..1) para o numero ficar preto */
#define VU_PI      3.14159265f

static const unsigned char vu_font[][6] = {
   {'0',7,5,5,5,7}, {'1',2,6,2,2,7}, {'2',7,1,7,4,7}, {'3',7,1,7,1,7},
   {'5',7,4,7,1,7}, {'7',7,1,2,2,2}, {'V',5,5,5,5,2}, {'U',5,5,5,5,7},
   {'L',4,4,4,4,7}, {'R',6,5,6,5,5}
};

/* escala do VU (dB do VU -> posicao 0..1 no arco; comprimida a esquerda, como o real) */
static const float vu_db_tab[9] = { -20.f, -10.f, -7.f, -5.f, -3.f, 0.f, 1.f, 2.f, 3.f };
static const float vu_t_tab[9]  = { 0.00f, 0.25f, 0.38f, 0.50f, 0.62f, 0.78f, 0.85f, 0.92f, 1.00f };
static const char *const vu_lbl[9] = { "20", "10", "7", "5", "3", "0", "1", "2", "3" };

static float vu_pos[2] = { 0.0f, 0.0f };

static void vu_px(int x, int y, unsigned short color)
{
   if (x < 0 || x >= 640 || y < 0 || y >= 480)
      return;
   set_pixel(framebuffer, x, y, color);
}

static void vu_text(const char *s, int cx, int y, unsigned short color)
{
   int len = (int)strlen(s);
   int x   = cx - (len * 4 - 1) / 2;
   int i, k, r, c;
   for (i = 0; i < len; i++, x += 4)
   {
      for (k = 0; k < (int)(sizeof(vu_font) / sizeof(vu_font[0])); k++)
      {
         if (vu_font[k][0] != (unsigned char)s[i])
            continue;
         for (r = 0; r < 5; r++)
            for (c = 0; c < 3; c++)
               if ((vu_font[k][r + 1] >> (2 - c)) & 1)
                  vu_px(x + c, y + r, color);
         break;
      }
   }
}

static float vu_db_to_pos(float db)
{
   int i;
   if (db <= vu_db_tab[0]) return 0.0f;
   if (db >= vu_db_tab[8]) return 1.0f;
   for (i = 0; i < 8; i++)
      if (db < vu_db_tab[i + 1])
         return vu_t_tab[i] + (vu_t_tab[i + 1] - vu_t_tab[i]) *
                (db - vu_db_tab[i]) / (vu_db_tab[i + 1] - vu_db_tab[i]);
   return 1.0f;
}

static void draw_vu_one(int cx, int ch, float pos)
{
   unsigned short c_arc    = get_color(29, 58, 29);
   unsigned short c_inner  = get_color(9, 20, 16);
   unsigned short c_red    = get_color(31, 16, 5);
   unsigned short c_cyan   = get_color(6, 46, 31);
   unsigned short c_green  = get_color(8, 50, 12);
   unsigned short c_needle = get_color(10, 48, 31);
   int x0 = cx - VU_BOX_W / 2;
   int y0 = VU_CY - VU_BOX_H / 2;
   int px = cx;
   int py = y0 + VU_BOX_H - 12;
   int i, rr;
   float a, rad, s, c, bestd = 1.0f;
   int hit = -1;

   /* arco principal (vermelho a partir do 0) e arco interno discreto */
   for (a = -VU_ANG; a <= VU_ANG; a += 0.5f)
   {
      float t = (a + VU_ANG) / (2.0f * VU_ANG);
      rad = a * VU_PI / 180.0f;
      s = sinf(rad);
      c = cosf(rad);
      for (rr = VU_R - 1; rr <= VU_R; rr++)
         vu_px(px + (int)(s * rr + (s < 0 ? -0.5f : 0.5f)),
               py - (int)(c * rr + 0.5f), t >= 0.78f ? c_red : c_arc);
      if (t < 0.78f)
         vu_px(px + (int)(s * (VU_R - 7) + (s < 0 ? -0.5f : 0.5f)),
               py - (int)(c * (VU_R - 7) + 0.5f), c_inner);
   }

   /* numero para o qual o ponteiro aponta (fica preto) */
   for (i = 0; i < 9; i++)
   {
      float d = pos - vu_t_tab[i];
      if (d < 0.0f) d = -d;
      if (d <= VU_HIT_TOL && d < bestd)
      {
         bestd = d;
         hit   = i;
      }
   }

   /* marcas e numeros */
   for (i = 0; i < 9; i++)
   {
      unsigned short col = (vu_db_tab[i] >= 0.0f) ? c_red : c_cyan;
      float t = vu_t_tab[i];
      a   = -VU_ANG + t * 2.0f * VU_ANG;
      rad = a * VU_PI / 180.0f;
      s = sinf(rad);
      c = cosf(rad);
      draw_line(framebuffer, col,
                px + (int)(s * (VU_R + 1) + (s < 0 ? -0.5f : 0.5f)), py - (int)(c * (VU_R + 1) + 0.5f),
                px + (int)(s * (VU_R + 4) + (s < 0 ? -0.5f : 0.5f)), py - (int)(c * (VU_R + 4) + 0.5f));
      vu_text(vu_lbl[i],
              px + (int)(s * (VU_R + 11) + (s < 0 ? -0.5f : 0.5f)),
              py - (int)(c * (VU_R + 11) + 0.5f) - 2,
              i == hit ? get_color(0, 0, 0) : col);
      if (i < 8)
      {
         float tm = (t + vu_t_tab[i + 1]) * 0.5f;
         a   = -VU_ANG + tm * 2.0f * VU_ANG;
         rad = a * VU_PI / 180.0f;
         s = sinf(rad);
         c = cosf(rad);
         draw_line(framebuffer, c_inner,
                   px + (int)(s * (VU_R + 1) + (s < 0 ? -0.5f : 0.5f)), py - (int)(c * (VU_R + 1) + 0.5f),
                   px + (int)(s * (VU_R + 3) + (s < 0 ? -0.5f : 0.5f)), py - (int)(c * (VU_R + 3) + 0.5f));
      }
   }

   vu_text("VU", px, py - 24, c_green);
   vu_text(ch ? "R" : "L", ch ? x0 + VU_BOX_W - 8 : x0 + 7, y0 + VU_BOX_H - 19, c_cyan);

   /* ponteiro */
   a   = -VU_ANG + pos * 2.0f * VU_ANG;
   rad = a * VU_PI / 180.0f;
   s = sinf(rad);
   c = cosf(rad);
   for (i = 0; i < 2; i++)
      draw_line(framebuffer, c_needle,
                px + i, py,
                px + i + (int)(s * (VU_R - 3) + (s < 0 ? -0.5f : 0.5f)), py - (int)(c * (VU_R - 3) + 0.5f));
   /* cubo do ponteiro: meia-lua magenta centralizada no pivo */
   {
      unsigned short c_hub = ch ? get_color(17, 10, 28) : get_color(6, 26, 30); /* R: roxo, L: azul */
      int dy, hx, half;
      for (dy = 0; dy < 6; dy++)
      {
         half = (int)sqrtf(30.25f - (float)(dy * dy));
         for (hx = px - half + 1; hx <= px + half; hx++)
            vu_px(hx, py + 2 - dy, c_hub);
      }
   }
}

static void draw_vu(const short *audio, int frames)
{
   int i, ch;

   for (ch = 0; ch < 2; ch++)
   {
      double sum = 0.0;
      float ms, db, target;
      for (i = 0; i < frames; i++)
      {
         float v = (float)audio[i * 2 + ch] * (1.0f / 32768.0f);
         sum += (double)(v * v);
      }
      ms     = (float)(sum / (double)(frames > 0 ? frames : 1));
      db     = 10.0f * log10f(ms + 1e-10f) + VU_REF_DB;
      target = vu_db_to_pos(db);

      /* balistica: sobe rapido, desce mais devagar */
      if (target > vu_pos[ch])
         vu_pos[ch] += (target - vu_pos[ch]) * 0.35f;
      else
         vu_pos[ch] += (target - vu_pos[ch]) * 0.10f;
      if (vu_pos[ch] < 0.0f) vu_pos[ch] = 0.0f;
      if (vu_pos[ch] > 1.0f) vu_pos[ch] = 1.0f;
   }

   draw_vu_one(VU_L_CX, 0, vu_pos[0]);
   draw_vu_one(VU_R_CX, 1, vu_pos[1]);
}

/* ---- navegador: janela com o conteudo do .zip atual ----
 * Largura reduzida em 30% e centralizada na tela (horizontal e vertical). */
#define BROWSER_ROWS   10   /* era 14: ~30% menos linhas visiveis */
#define BROWSER_ROW_H  26
#define BROWSER_BG_ALPHA  190  /* 0 = totalmente transparente, 255 = totalmente solido */
#define BROWSER_GLYPH_H   16   /* altura do glifo desenhado (fonte 8x8 em escala 2x) */
#define BROWSER_W      ((UI_W * 7) / 10)   /* largura reduzida em 30% */

static void draw_browser(void)
{
   int i, first, y;
   int visible = BROWSER_ROWS;
   int box_w = BROWSER_W, box_h;
   int box_x0, box_y0;
   char line[300];

   if (browser_count_ui < visible)
      visible = browser_count_ui;
   box_h = 40 + visible * BROWSER_ROW_H;

   /* centraliza a caixa na tela inteira (640x480) */
   box_x0 = (640 - box_w) / 2;
   box_y0 = (480 - box_h) / 2;

   first = browser_cursor - visible / 2;
   if (first > browser_count_ui - visible) first = browser_count_ui - visible;
   if (first < 0) first = 0;

   draw_shape_alpha(framebuffer, get_color(2,4,5),   BROWSER_BG_ALPHA, box_x0,   box_y0,   box_w,   box_h);
   draw_shape_alpha(framebuffer, get_color(6,12,14), BROWSER_BG_ALPHA, box_x0+2, box_y0+2, box_w-4, box_h-4);

   y = box_y0 + 20 + (BROWSER_ROW_H - BROWSER_GLYPH_H) / 2;
   for (i = first; i < first + visible && i < browser_count_ui; i++)
   {
      int is_sel = (i == browser_cursor);
      int is_dir = browser_entry_is_dir(i);
      int is_zip = browser_entry_is_zip(i);
      unsigned short col = is_sel ? get_color(31,63,20) : (is_dir ? get_color(28,50,31) : get_color(20,40,22));
      if (is_sel)
         draw_shape_alpha(framebuffer, get_color(10,20,15), BROWSER_BG_ALPHA, box_x0+4, y-4, box_w-8, BROWSER_ROW_H);
      snprintf(line, sizeof(line), "%s%s%s%s", is_sel ? "> " : "  ",
               browser_entry_name(i), is_dir ? "/" : "", is_zip ? "  [ZIP]" : "");
      draw_text_prop_clip(line, box_x0 + 12, y, col, box_x0 + box_w - 12);
      y += BROWSER_ROW_H;
   }
}

/* ---- janela de creditos (botao B, fora do navegador) ----
 * Mesma caixa centralizada/translucida do navegador de zip, com o texto
 * subindo de baixo pra cima continuamente, estilo creditos de filme. */

/* ------------------------------------------------------------------
 * TEXTO DAS INSTRUCOES (B) -- edite as linhas abaixo livremente.
 * Uma string por linha; "" = linha em branco (espaco entre paragrafos).
 * Linhas longas quebram sozinhas; 3+ espacos apos o 1o termo = 2 colunas.
 * A ordem de cima para baixo neste array e a ordem de exibicao (a
 * primeira linha e a primeira a subir e a primeira a sair pelo topo).
 * ------------------------------------------------------------------ */
static const char *credits_lines[] = {
   "=====================================================================================",
   "VGM'2 - INSTRUCTIONS",
   "=====================================================================================",
   "",
   "USE WITH JOYSTICK:",
   "",
   "Button START  = Pause/Play",
   "Button SELECT = Load Track",
   "Button X      = MT32/GM/Fluidsynth",
   "Button B      = Instructions",
   "UP/DOWN       = Track Selection",
   "Button R1     = Next Track",
   "Button L1     = Rewind Track",
   "Button R1     = Fast Forward (Holding)",
   "Button L1     = Fast Rewind (Holding)",
   "Button R2     = Vol. +",
   "Button L2     = Vol. -",
   "",
   "CORE: gme2_libretro (fork) - SUPPORTED SYSTEMS AND CHIPS",
   "",
   "============================================================",
   "- ARCADE (VGM multichip, libvgm) 48 Chip/Drivers",
   "",
   "* SEGA",
   "SN76489    Assorted Sega signs",
   "YM2151     Sega System 16, X-Board, Y-Board",
   "YM2610     SNK Neo Geo MVS (Metal Slug, King of Fighters)...",
   "YM2612     Sega System C / C2",
   "SegaPCM    Out Run, After Burner, Space Harrier, Hang-On...",
   "RF5C68     Sega System 18 (Shadow Dancer), Sega System 32",
   "MultiPCM   Sega Model 1 e 2 / System 32",
   "SCSP       Sega Model 2 e 3",
   "uPD7759    Various Sega boards",
   "YMW258     Sega MultiPCM, (Virtua Fighter...),",
   "           Sega Model 2 (Daytona USA...)",
   "",
   "* KONAMI",
   "YM2151     Several Konami boards",
   "K051649    SCC (Also on MSX cartridges)",
   "K054539    Xexex, The Simpsons and others...",
   "K053260    Late-80s Konami...",
   "K007232    Contra, Super Contra, Gradius III...",
   "K005289    Boards GX400 (Gradius, Salamander...)",
   "",
   "* CAPCOM",
   "YM2151     CPS-1 (Street Fighter II), System 16 (Altered Beast...)",
   "YM2203     Ghosts 'n Goblins, 1942, Commando and others...",
   "OKIM6295   CPS1, Final Fight, Ghouls 'n Ghosts...",
   "QSound     CPS2, Marvel vs. Capcom, S. Street Fighter II Turbo...",
   "           CPS-1, (Cadillacs and Dinosaurs...)",
   "",
   "* NAMCO",
   "YM2151     System 2",
   "C140       System 2 and System 21",
   "C352       System 22 and Super System 23",
   "",
   "* TAITO",
   "YM2203     Various boards...",
   "YM3812     Toaplan (Truxton), Data East (Robocop...)",
   "YM3526     Taito / Data East",
   "MSM5232    Boards Taito",
   "MSM5205    Double Dragon, Moon Patrol, Karate Champ...",
   "",
   "* IREM / SETA / JALECO / SEIBU",
   "GA20       Irem (M92 and others)",
   "X1-010     Seta",
   "YMF271     Jaleco / Seibu, IBM-PC Doom, (Sound Blaster Pro)",
   "",
   "* CAVE / IGS / Others",
   "YMZ280B    Cave (DonPachi, DoDonPachi) and others",
   "OKIM6295   Cave, Data East and many others",
   "ICS2115    IGS PGM (Oriental Legend, Knights of Valour)",
   "ES5506     Ensoniq and some arcade games",
   "BSMT2000   Battletoads, Pinballs Data East/Sega/Stern.",
   "",
   "* ATARI",
   "POKEY      Centipede, Missile Command and others",
   "",
   "* PSG GENERIC",
   "AY8910     Countless arcades and microcomputers",
   "",
   "=====================================================================================",
   "CONSOLES AND HANDHELDS (fixed audio chip)",
   "",
   "GBS        Game Boy                 Chip: DMG",
   "NSF/NSFE   Nintendo NES / Famicom   Chip: 2A03 + Expansions",
   "HES        PC Engine / TurboGrafx   Chip: HuC6280",
   "SPC        Super Nintendo           Chip: S-SMP + S-DSP",
   "GYM        Sega Genesis/Mega Drive  Chip: YM2612 + SN76489",
   "",
   "* ALSO VIA VGM (MULTICHIP)",
   "",
   "Master System / Game Gear - SN76489, YM2413",
   "Mega Drive - YM2612, SN76489",
   "Sega CD - RF5C164",
   "Sega 32X - PWM",
   "Sega Saturn - SCSP",
   "Sega Pico - VGM",
   "Game Boy - GB DMG",
   "Game Boy Color - VGM",
   "NES / Famicom - NES APU",
   "Famicom Disk System - VGM",
   "PC Engine - HuC6280",
   "PC Engine CD-ROM2 - VGM",
   "Atari Lynx - Mikey",
   "FM Towns - VGM",
   "Virtual Boy - VSU-VUE",
   "WonderSwan - WonderSwan",
   "Neo-Geo (AES/MVS) - YM2610",
   "Neo-Geo Pocket - VGM",
   "Vectrex - VGM",
   "",
   "=====================================================================================",
   "HOME COMPUTERS",
   "",
   "AY         ZX Spectrum / Amstrad CPC - Chip: AY-3-8910",
   "KSS        MSX 1/2 (SMS, Game Gear, ColecoVision)",
   "           Chips: PSG, SCC, FM-PAC",
   "SAP        Atari XL/XE (8 bits) = Chip: POKEY",
   "",
   "* ALSO VIA VGM (MULTICHIP)",
   "",
   "MSX 1/2 - AY8910, K051649 (SCC), YM2413 (MSX-MUSIC),",
   "    Y8950 (MSX-Audio)",
   "NEC PC-88 / PC-98 - YM2203, YM2608",
   "Sharp X1 Turbo - VGM",
   "Sharp X68000 - YM2151, OKIM6258",
   "Fujitsu FM Towns - RF5C68",
   "Apple IIGS - ES5503",
   "Atari-ST - VGM",
   "Sam Coupe - SAA1099",
   "IBM PC (AdLib / Sound Blaster) - YM3812 (OPL2), YMF262 (OPL3)",
   "MoonSound (MSX) - YMF278B (OPL4)",
   "BBC Micro / ColecoVision - SN76489",
   "",
   "=====================================================================================",
   "ADDITIONAL IMPLEMENTATIONS",
   "",
   "MOD. Amiga MODULES / Mixing Paula \"Autodetect\" (.mod.s3m.xm.it)",
   "MIDI. General MIDI / Roland-MT-32 / Fluidsynth \"Autodetect\" (.mid)",
   "MPEG-1 Audio Layer III (.mp3)",
   "",
   "* The VMG2 accepts .zip files for loading music, supporting multiple tracks within a single .zip file as well as folders and subfolders!",
   "",
   "### Created by Marcelo Tavares / marcelosofth@hotmail.com ###",
   "",
   "=====================================================================================",
   "VIDEO GAME MUSIC 2 (2026) ^_^",
   "=====================================================================================",
   "",
   "",
};
#define CREDITS_LINE_COUNT ((int)(sizeof(credits_lines) / sizeof(credits_lines[0])))

#define CREDITS_W          ((UI_W * 8) / 10)
#define CREDITS_H          360
#define CREDITS_MARGIN     14   /* margem lateral; as barras "=====" vao de x0+MARGIN ate x0+W-MARGIN */
#define CREDITS_INNER_W    (CREDITS_W - 2 * CREDITS_MARGIN)
#define CREDITS_LINE_H     ((CREDITS_TEXT_PX * 3) / 2)   /* 12 px com o texto em 8 px */
#define CREDITS_BG_ALPHA   210
#define CREDITS_IMG_ALPHA  253  /* alpha real do fundo22.png (0-255), lido do canal alfa do PNG;
                                    usado para misturar a imagem com o que ja esta no framebuffer */
#define CREDITS_SPEED_Q8   (CREDITS_TEXT_PX * 15)   /* ponto fixo 8.8: 120/256 = 0,469 px/frame (~28 px/s a 60 fps) com texto de 8 px (+25% sobre o *12 anterior).
                                                        Era *8 (64); aumente/diminua o multiplicador para acelerar/frear */

/* tamanho (em px de tela) de cada glifo do CORPO do texto dos creditos (fonte base 8x8).
   USE SOMENTE 8 (escala 1x) OU 16 (escala 2x): sao os unicos tamanhos em que cada pixel da
   fonte vira um numero inteiro de pixels de tela. Tamanhos intermediarios (ex.: 12) fazem a
   amostragem "do vizinho mais proximo" alternar pixels de 1 e 2 px -> letras tortas/entupidas.
   Altura da linha, 2a coluna e velocidade de rolagem se ajustam sozinhas a este valor.
   O TITULO tambem usa so escala inteira (ver credits_chip_draw_line mais abaixo). */
#define CREDITS_TEXT_PX 8

/* logo "Video Game Music 2" no topo do texto que sobe (credits_logo.h, 264x132 px).
   Medido na foto de referencia: logo centralizado na caixa e 75 px entre a base dele e a
   1a barra "=====" (o logo sobe junto com o texto). CREDITS_LOGO_ROWS = linhas em branco
   reservadas no inicio do texto para o logo caber acima da barra, com CREDITS_LOGO_TOP_MARGIN
   px livres acima dele. */
#define CREDITS_LOGO_GAP         75
#define CREDITS_LOGO_TOP_MARGIN  12
#define CREDITS_LOGO_ROWS  ((CREDITS_LOGO_TOP_MARGIN + CREDITS_LOGO_GAP + CREDITS_LOGO_H - (CREDITS_LINE_H / 2 - 1) + CREDITS_LINE_H - 1) / CREDITS_LINE_H)

/* como put_glyph (usado pelo cabecalho), mas em escala CREDITS_TEXT_PX/8 (amostragem do
   vizinho mais proximo, funciona para qualquer tamanho) e com clip vertical tambem
   (aqui o texto precisa ser cortado no topo/base da caixa, nao so nas laterais) */
static void credits_put_glyph(unsigned char ch, int px, int py, unsigned short color,
                               int cx0, int cx1, int cy0, int cy1)
{
   int ox, oy;
   int charx = (ch % 16) * 8;
   int chary = (ch >> 4) * 8;
   for (oy = 0; oy < CREDITS_TEXT_PX; oy++)
   {
      int yy = py + oy;
      int sy = (oy * 8) / CREDITS_TEXT_PX;
      if (yy < cy0 || yy >= cy1)
         continue;
      for (ox = 0; ox < CREDITS_TEXT_PX; ox++)
      {
         int xx = px + ox;
         int sx = (ox * 8) / CREDITS_TEXT_PX;
         if (xx < cx0 || xx >= cx1)
            continue;
         if (is_font_pixel(charx + sx, chary + sy))
            set_pixel(framebuffer, xx, yy, color);
      }
   }
}

/* largura proporcional de 'text' no tamanho do corpo (CREDITS_TEXT_PX), equivalente a
   text_width_prop() mas na escala menor usada pelos creditos */
static int credits_text_width(const char *text)
{
   int w = 0, x0, x1;
   for (; *text; text++)
   {
      glyph_extent((unsigned char)*text, &x0, &x1);
      w += (x1 < 0) ? 4 : (x1 - x0 + 2);
   }
   w = w > 0 ? w - 1 : 0;
   return (w * CREDITS_TEXT_PX) / 8;
}

/* desenha texto a partir de x (canto esquerdo), cortado na caixa (cx0..cx1, cy0..cy1) */
static void credits_draw_text_at(const char *text, int x, int y, unsigned short color,
                                  int cx0, int cx1, int cy0, int cy1)
{
   int x0, x1;
   for (; *text; text++)
   {
      unsigned char ch = (unsigned char)*text;
      glyph_extent(ch, &x0, &x1);
      if (x1 < 0)
      {
         x += (4 * CREDITS_TEXT_PX) / 8;
         continue;
      }
      credits_put_glyph(ch, x - (x0 * CREDITS_TEXT_PX) / 8, y, color, cx0, cx1, cy0, cy1);
      x += ((x1 - x0 + 2) * CREDITS_TEXT_PX) / 8;
   }
}

/* titulos. Mesma fonte/degrade do CHIP: no cabecalho (Press Start 2P, azul->branco no
   topo e dourado->creme na base, ver chip_grad_init()).
   - titulo inicial ("VGM'2 - INSTRUCTIONS", accent = 2): grande. Altura sempre
     2x (crisp); a LARGURA e a maior que cabe entre as barras (credits_title_hsq, Q8: 512 = 2x
     inteiro como antes, menor = fonte levemente condensada, calculada em credits_build_rows()).
     Todas as letras usam o mesmo padrao de colunas, entao ficam iguais entre si. Ocupa 2 linhas.
   - titulo final (accent = 1): 1x, com CREDITS_TITLE_TRACK px extras entre as letras,
     centralizado (nao precisa chegar ate as pontas das barras). */
#define CREDITS_TITLE_TRACK 2

static int credits_title_hsq = 512;   /* escala horizontal do titulo grande, ponto fixo Q8 */

/* texto usado so para DEFINIR o tamanho do titulo grande (o que o titulo antigo tinha). Nao aparece
   na tela: o que aparece e a 1a linha de credits_lines[] que comeca com "VGM'2" ou "VIDEO GAME MUSIC 2". */
#define CREDITS_TITLE_REF "VIDEO GAME MUSIC 2 - INSTRUCTIONS"

/* largura (px de tela) de 'text': cada letra avanca ((x1-x0+2)*hsq)>>8 px, mesma conta do desenho */
static int credits_chip_width(const char *text, int hsq, int track)
{
   int x0, x1, w = 0, n = (int)strlen(text);
   for (; *text; text++)
   {
      chip_extent((unsigned char)*text, &x0, &x1);
      w += (x1 < 0) ? (CHIP_SPACE * hsq) >> 8 : ((x1 - x0 + 2) * hsq) >> 8;
   }
   w -= (hsq >> 8);                      /* sem o espaco depois da ultima letra */
   return w + track * (n > 0 ? n - 1 : 0);
}

/* desenha a letra com origem ox (esquerda da tinta), colunas x0..x1 da fonte em escala hsq (Q8)
   na horizontal e vsc (inteiro) na vertical */
static void credits_chip_put_glyph(unsigned char ch, int x0, int ox, int py, int hsq, int vsc,
                                    int cx0, int cx1, int cy0, int cy1)
{
   int gx, gy, xx, yy;
   for (gx = x0; gx < 8; gx++)
   {
      int xa = ox + (((gx - x0) * hsq) >> 8);
      int xb = ox + (((gx - x0 + 1) * hsq) >> 8);
      for (gy = 0; gy < 8; gy++)
      {
         if (!chip_pixel(ch, gx, gy))
            continue;
         for (yy = py + gy * vsc; yy < py + (gy + 1) * vsc; yy++)
         {
            if (yy < cy0 || yy >= cy1)
               continue;
            for (xx = xa; xx < xb; xx++)
            {
               if (xx < cx0 || xx >= cx1)
                  continue;
               set_pixel(framebuffer, xx, yy, chip_grad[gy]);
            }
         }
      }
   }
}

/* centralizada entre as barras (left .. left + CREDITS_INNER_W) */
static void credits_chip_draw_line(const char *text, int left, int y, int big,
                                    int cx0, int cx1, int cy0, int cy1)
{
   int x0, x1;
   const int hsq   = big ? credits_title_hsq : 256;
   const int vsc   = big ? 2 : 1;
   const int track = big ? 0 : CREDITS_TITLE_TRACK;
   const int n     = (int)strlen(text);
   const int width = credits_chip_width(text, hsq, track);
   const int sx0   = left + (CREDITS_INNER_W - width) / 2;
   const int rowh  = big ? 2 * CREDITS_LINE_H : CREDITS_LINE_H;   /* o titulo grande ocupa 2 linhas */
   const int py    = y + (rowh - 8 * vsc) / 2;
   int u = 0, i;
   chip_grad_init();
   for (i = 0; i < n; i++)
   {
      unsigned char ch = (unsigned char)text[i];
      chip_extent(ch, &x0, &x1);
      if (x1 < 0)
      {
         u += (CHIP_SPACE * hsq) >> 8;
         continue;
      }
      credits_chip_put_glyph(ch, x0, sx0 + u + track * i, py, hsq, vsc, cx0, cx1, cy0, cy1);
      u += ((x1 - x0 + 2) * hsq) >> 8;
   }
}

/* fundo (imagem) da janela de creditos, RGB565 -> cor da plataforma via get_color(),
   com cache estatico decodificado uma unica vez (mesmo padrao de spectrum_draw_background
   em spectrum.c). CREDITS_BG_W/H (460x360, ver credits_bg.h) batem exatamente com
   CREDITS_W/CREDITS_H, entao a imagem preenche a caixa toda sem escalar.
   O fundo2.png original tem um canal alfa uniforme (246/255); em vez de gravar
   a imagem por cima do framebuffer, cada pixel e misturado (blend) com o que ja
   esta desenhado ali (fundo/espectro por baixo da janela) usando esse mesmo alfa,
   preservando a transparencia real da imagem. */
static void draw_credits_background(int x0, int y0)
{
   static unsigned short cache[CREDITS_BG_W * CREDITS_BG_H];
   static int ready = 0;
   int xx, yy;
   uint16_t *px = (uint16_t *)framebuffer->pixel_data;

   if (!ready)
   {
      int i;
      for (i = 0; i < CREDITS_BG_W * CREDITS_BG_H; i++)
      {
         unsigned short v = credits_bg_pixels[i];
         cache[i] = get_color((v >> 11) & 31, (v >> 5) & 63, v & 31);
      }
      ready = 1;
   }

   for (yy = 0; yy < CREDITS_BG_H; yy++)
      for (xx = 0; xx < CREDITS_BG_W; xx++)
      {
         unsigned short img = cache[xx + yy * CREDITS_BG_W];
         uint16_t *dst = &px[(x0 + xx) + (y0 + yy) * framebuffer->width];
         unsigned short bg = *dst;

         int ir = (img >> 11) & 31, ig = (img >> 5) & 63, ib = img & 31;
         int br = (bg  >> 11) & 31, bg_g = (bg >> 5) & 63, bb = bg & 31;

         int r = (ir * CREDITS_IMG_ALPHA + br   * (255 - CREDITS_IMG_ALPHA)) / 255;
         int g = (ig * CREDITS_IMG_ALPHA + bg_g * (255 - CREDITS_IMG_ALPHA)) / 255;
         int b = (ib * CREDITS_IMG_ALPHA + bb   * (255 - CREDITS_IMG_ALPHA)) / 255;

         *dst = get_color(r, g, b);
      }
}

/* logo com transparencia (alpha 0..255 por pixel), recortado a area y0..y1 da caixa */
static void draw_credits_logo(int x0, int y0, int cy0, int cy1)
{
   static unsigned short cache[CREDITS_LOGO_W * CREDITS_LOGO_H];
   static int ready = 0;
   int xx, yy;
   uint16_t *px = (uint16_t *)framebuffer->pixel_data;

   if (!ready)
   {
      int i;
      for (i = 0; i < CREDITS_LOGO_W * CREDITS_LOGO_H; i++)
      {
         unsigned short v = credits_logo_rgb[i];
         cache[i] = get_color((v >> 11) & 31, (v >> 5) & 63, v & 31);
      }
      ready = 1;
   }

   for (yy = 0; yy < CREDITS_LOGO_H; yy++)
   {
      int y = y0 + yy;
      if (y < cy0 || y >= cy1)
         continue;
      for (xx = 0; xx < CREDITS_LOGO_W; xx++)
      {
         int a = credits_logo_alpha[xx + yy * CREDITS_LOGO_W];
         unsigned short img, bg;
         uint16_t *dst;
         int ir, ig, ib, br, bg_g, bb;

         if (!a)
            continue;
         img = cache[xx + yy * CREDITS_LOGO_W];
         dst = &px[(x0 + xx) + y * framebuffer->width];
         bg  = *dst;

         ir = (img >> 11) & 31; ig = (img >> 5) & 63; ib = img & 31;
         br = (bg  >> 11) & 31; bg_g = (bg >> 5) & 63; bb = bg & 31;

         *dst = get_color((ir * a + br   * (255 - a)) / 255,
                          (ig * a + bg_g * (255 - a)) / 255,
                          (ib * a + bb   * (255 - a)) / 255);
      }
   }
}

/* sprite animado (credits_sonic.h, 56x63 px, 4 quadros de 60 ms). Sobe junto com o texto: a
   posicao vertical vem da secao (credits_secs[0], centralizado entre as barras); CREDITS_SONIC_X e a
   posicao horizontal (a partir do canto esquerdo da caixa, 460x360), usada por todos os sprites. */
#define CREDITS_SONIC_X       391
#define CREDITS_SONIC_ANCHOR  "USE WITH JOYSTICK:"
#define CREDITS_GREEN_TXT     CREDITS_SONIC_ANCHOR   /* linha desenhada em verde */
#define CREDITS_GREEN         get_color(5, 56, 14)   /* verde do equalizador */

static void draw_credits_sonic(int x0, int y0, int cx0, int cx1, int cy0, int cy1)
{
   static unsigned short cache[CREDITS_SONIC_FRAMES][CREDITS_SONIC_W * CREDITS_SONIC_H];
   static int ready = 0;
   int xx, yy;
   /* quadro atual: 60 ms = 3,6 frames de video a 60 fps */
   int f = (((credits_anim % 3600) * 1000 / 60) / CREDITS_SONIC_MS) % CREDITS_SONIC_FRAMES;   /* %3600 (60 s) evita estouro e emenda sem corte */

   if (!ready)
   {
      int k, i;
      for (k = 0; k < CREDITS_SONIC_FRAMES; k++)
         for (i = 0; i < CREDITS_SONIC_W * CREDITS_SONIC_H; i++)
         {
            unsigned short v = credits_sonic_rgb[k][i];
            cache[k][i] = get_color((v >> 11) & 31, (v >> 5) & 63, v & 31);
         }
      ready = 1;
   }

   for (yy = 0; yy < CREDITS_SONIC_H; yy++)
   {
      int y = y0 + yy;
      if (y < cy0 || y >= cy1)
         continue;
      for (xx = 0; xx < CREDITS_SONIC_W; xx++)
      {
         int x = x0 + xx;
         if (x < cx0 || x >= cx1)
            continue;
         if (credits_sonic_alpha[f][xx + yy * CREDITS_SONIC_W])
            set_pixel(framebuffer, x, y, cache[f][xx + yy * CREDITS_SONIC_W]);
      }
   }
}

/* ---- layout do texto: quebra automatica de linha e colunas ----
 * O texto em credits_lines[] e "cru" (igual ao .txt). Na primeira vez que a janela
 * abre ele e convertido em linhas de tela (credits_rows) que cabem na caixa:
 *  - 3+ espacos seguidos apos o 1o termo  -> 2 colunas (ex.: "YM2151     Sega ...")
 *  - "=====" / "-----"                    -> linha horizontal
 *  - "VGM'2 ..." / "VIDEO GAME MUSIC 2 ..."     -> titulo (fonte do CHIP) centralizado
 *  - amarelo so em "* ..." e no titulo logo abaixo de uma barra
 *  - linhas longas quebram no espaco (com recuo nas continuacoes) */
#define CREDITS_COL2_X     (CREDITS_TEXT_PX * 7)   /* inicio da 2a coluna, em px a partir da margem (84 com texto de 12 px, 56 com 8 px) */
#define CREDITS_MAX_ROWS   500
#define CREDITS_ROW_CHARS  96

enum { CR_TEXT, CR_TITLE, CR_RULE, CR_BLANK };

typedef struct
{
   unsigned char kind;
   unsigned char accent;
   int  x;                       /* recuo de c2 em px, a partir da margem */
   char c1[16];                  /* coluna 1 (opcional), na margem */
   char c2[CREDITS_ROW_CHARS];   /* texto principal */
} credits_row_t;

static credits_row_t credits_rows[CREDITS_MAX_ROWS];
static int credits_row_count = 0;
static int credits_rows_built = 0;

static void credits_add_row(int kind, int accent, int x, const char *c1, const char *c2)
{
   credits_row_t *r;
   if (credits_row_count >= CREDITS_MAX_ROWS)
      return;
   r = &credits_rows[credits_row_count++];
   memset(r, 0, sizeof(*r));
   r->kind = (unsigned char)kind;
   r->accent = (unsigned char)accent;
   r->x = x;
   if (c1)
      strncpy(r->c1, c1, sizeof(r->c1) - 1);
   if (c2)
      strncpy(r->c2, c2, sizeof(r->c2) - 1);
}

/* quebra 's' em linhas de no maximo 'avail' px (por palavras) */
static void credits_wrap(const char *s, int avail, int kind, int first_x, int cont_x,
                          int accent, const char *c1)
{
   char line[CREDITS_ROW_CHARS];
   char trial[CREDITS_ROW_CHARS * 2];
   char word[CREDITS_ROW_CHARS];
   int first = 1, n = 0;

   line[0] = 0;
   while (*s)
   {
      int wl = 0;
      while (*s == ' ')
         s++;
      if (!*s)
         break;
      while (*s && *s != ' ' && wl < CREDITS_ROW_CHARS - 1)
         word[wl++] = *s++;
      word[wl] = 0;

      strcpy(trial, line);
      if (n)
         strcat(trial, " ");
      strcat(trial, word);

      if (n && (credits_text_width(trial) > avail || strlen(trial) >= CREDITS_ROW_CHARS - 1))
      {
         credits_add_row(kind, accent, first ? first_x : cont_x, first ? c1 : NULL, line);
         first = 0;
         strcpy(line, word);
         n = (int)strlen(line);
      }
      else
      {
         strcpy(line, trial);
         n = (int)strlen(line);
      }
   }
   if (n)
      credits_add_row(kind, accent, first ? first_x : cont_x, first ? c1 : NULL, line);
}

/* 2+ espacos viram 1; 3+ espacos viram 'sep' (se sep != NULL); 4+ pontos viram "..." */
static void credits_squeeze(const char *src, char *dst, int cap, const char *sep)
{
   int n = 0;
   while (*src && n < cap - 5)
   {
      if (*src == ' ')
      {
         int run = 0;
         while (src[run] == ' ')
            run++;
         if (run >= 3 && sep)
         {
            const char *q = sep;
            while (*q && n < cap - 5)
               dst[n++] = *q++;
         }
         else
            dst[n++] = ' ';
         src += run;
      }
      else if (src[0] == '.' && src[1] == '.' && src[2] == '.' && src[3] == '.')
      {
         while (*src == '.')
            src++;
         dst[n++] = '.';
         dst[n++] = '.';
         dst[n++] = '.';
      }
      else
         dst[n++] = *src++;
   }
   dst[n] = 0;
}

static int credits_is_rule(const char *s)
{
   int n = 0;
   for (; *s; s++, n++)
      if (*s != '=' && *s != '-')
         return 0;
   return n >= 5;
}

static void credits_build_rows(void)
{
   int i, after_rule = 0, first_title_done = 0;
   credits_row_count = 0;

   /* espaco no topo para o logo (desenhado em draw_credits) */
   for (i = 0; i < CREDITS_LOGO_ROWS; i++)
      credits_add_row(CR_BLANK, 0, 0, NULL, "");

   for (i = 0; i < CREDITS_LINE_COUNT; i++)
   {
      const char *s = credits_lines[i];
      char c1[16];
      char rest[256];
      int k, accent, lead = 0;
      int below_rule = after_rule;   /* linha logo abaixo de uma barra "=====" */

      while (s[lead] == ' ')
         lead++;
      s += lead;

      if (!*s)
      {
         credits_add_row(CR_BLANK, 0, 0, NULL, "");
         after_rule = 0;
         continue;
      }
      if (credits_is_rule(s))
      {
         credits_add_row(CR_RULE, 0, 0, NULL, "");
         after_rule = 1;
         continue;
      }
      after_rule = 0;

      /* titulos ("VGM'2 ..." / "VIDEO GAME MUSIC 2 ..."): uma unica linha, sem quebra, na fonte do
         CHIP e centralizados; o tamanho vem de CREDITS_TITLE_REF (fim desta funcao) */
      if (strncmp(s, "VGM'2", 5) == 0 || strncmp(s, "VIDEO GAME MUSIC 2", 18) == 0)
      {
         if (!first_title_done)
         {
            credits_add_row(CR_TITLE, 2, 0, NULL, s);   /* grande (2x), ocupa esta linha + a proxima */
            credits_add_row(CR_BLANK, 0, 0, NULL, "");
            first_title_done = 1;
         }
         else
            credits_add_row(CR_TITLE, 1, 0, NULL, s);   /* pequeno (1x) */
         continue;
      }

      /* linha de continuacao (3+ espacos de recuo no .txt): fica alinhada na 2a coluna */
      if (lead >= 3)
      {
         credits_squeeze(s, rest, sizeof(rest), " ");
         credits_wrap(rest, CREDITS_INNER_W - CREDITS_COL2_X, CR_TEXT,
                      CREDITS_COL2_X, CREDITS_COL2_X, 0, NULL);
         continue;
      }

      /* 2 colunas: 1o termo + 3 ou mais espacos */
      for (k = 1; s[k]; k++)
         if (s[k] == ' ' && s[k + 1] == ' ' && s[k + 2] == ' ')
            break;
      if (s[k] && k < (int)sizeof(c1))
      {
         const char *p = s + k;
         memcpy(c1, s, k);
         c1[k] = 0;
         if (credits_text_width(c1) <= CREDITS_COL2_X - (CREDITS_TEXT_PX * 2) / 3)
         {
            while (*p == ' ')
               p++;
            credits_squeeze(p, rest, sizeof(rest), " - ");
            credits_wrap(rest, CREDITS_INNER_W - CREDITS_COL2_X, CR_TEXT,
                         CREDITS_COL2_X, CREDITS_COL2_X, 0, c1);
            continue;
         }
      }

      /* linha corrida (quebra com recuo nas continuacoes).
         Amarelo SOMENTE no titulo logo abaixo de uma barra; linhas que comecam com "* " sao verdes. */
      credits_squeeze(s, rest, sizeof(rest), " ");
      /* accent: 3 = linha "* ..." (verde), 1 = titulo logo abaixo de uma barra (amarelo), 0 = normal.
         As linhas de continuacao (quebra automatica) herdam o mesmo valor. */
      accent = (s[0] == '*' && s[1] == ' ') ? 3 : (below_rule ? 1 : 0);
      credits_wrap(rest, CREDITS_INNER_W, CR_TEXT, 0, CREDITS_TEXT_PX, accent, NULL);
   }

   /* titulo grande: mantem o tamanho (escala) que o titulo antigo "VIDEO GAME MUSIC 2 - INSTRUCTIONS"
      tinha (2x de altura, largura maxima que cabia entre as barras). O texto novo, mais curto, usa
      essa mesma escala e fica centralizado entre as barras. Se algum dia for maior que as barras,
      o laco abaixo ainda o condensa ate caber. */
   {
      int r;
      credits_title_hsq = 512;
      while (credits_title_hsq > 256 &&
             credits_chip_width(CREDITS_TITLE_REF, credits_title_hsq, 0) > CREDITS_INNER_W)
         credits_title_hsq--;
      for (r = 0; r < credits_row_count; r++)
         if (credits_rows[r].kind == CR_TITLE && credits_rows[r].accent == 2)
            while (credits_title_hsq > 256 &&
                   credits_chip_width(credits_rows[r].c2, credits_title_hsq, 0) > CREDITS_INNER_W)
               credits_title_hsq--;
   }
}

/* --- sprites extras (credits_sprites.h: 02 a 09, 11 e 12.gif), mesmo tamanho de celula do Sonic (56x63) ---
   Tambem sobem junto com o texto e ficam na mesma coluna do Sonic (CREDITS_SONIC_X).
   Cada SECAO (Sonic/instrucoes, ARCADE, CONSOLES, HOME COMPUTERS, ADDITIONAL) e o trecho entre a
   barra "=====" que a abre e a barra seguinte. Os sprites da secao ficam CENTRALIZADOS de cima a
   baixo entre as duas barras: com N sprites, os N+1 espacos (acima, entre eles e abaixo) sao iguais.
   A barra de inicio e achada pelo texto da secao (credits_secs[].txt = inicio do texto de uma linha
   dela; a barra e a primeira "=====" acima dessa linha), entao mudar o texto dos creditos nao quebra
   o alinhamento enquanto esses textos continuarem iguais.
   spr[] = indices em credits_sprites[] (0 = 02.gif ... 7 = 09.gif, 8 = 12.gif, 9 = 11.gif); -1 = o Sonic (credits_sonic.h). */
#define CREDITS_SEC_N        5
#define CREDITS_SEC_MAX_SPR  5

typedef struct
{
   const char *txt;       /* inicio do texto de uma linha da secao (acha a barra que a abre) */
   const char *end_txt;   /* se != NULL: a secao termina no TOPO da linha que comeca com este texto
                             (em vez de na proxima barra) */
   int n;
   signed char spr[CREDITS_SEC_MAX_SPR];
} credits_sec_t;

static const credits_sec_t credits_secs[CREDITS_SEC_N] = {
   { CREDITS_SONIC_ANCHOR, NULL, 1, { -1 } },              /* Sonic */
   { "- ARCADE",           NULL, 5, { 0, 1, 2, 3, 4 } },   /* Mario Kart, Dhalsim, Pac-Man, Chun-Li, Sakura */
   { "CONSOLES AND",       NULL, 2, { 5, 8 } },            /* Ken, Mega Man X (12.gif) */
   { "HOME COMPUTERS",     NULL, 2, { 6, 9 } },            /* 08.gif, 11.gif (nave) */
   /* Insert Coin: centralizado entre a barra e o texto amarelo "* The VMG2 accepts..." */
   { "ADDITIONAL IMPL",    "* The VMG2", 1, { 7 } }
};

/* desenha o sprite 'idx' na celula 56x63 cujo canto superior esquerdo e (x0, y0); o sprite fica
   centralizado na celula. A animacao usa a duracao de cada quadro do GIF original. */
static void draw_credits_sprite(int idx, int x0, int y0, int cx0, int cx1, int cy0, int cy1)
{
   const credits_sprite_t *sp = &credits_sprites[idx];
   int t = (int)((((long long)credits_anim * 1000) / 60) % sp->total_ms);
   int step = 0, xx, yy, ox, oy;
   const unsigned short *pix;

   while (step < sp->steps - 1 && t >= sp->end_ms[step])
      step++;
   pix = sp->pix + (unsigned)sp->seq[step] * sp->w * sp->h;
   ox  = x0 + (CREDITS_SPR_CELL_W - sp->w) / 2;
   oy  = y0 + (CREDITS_SPR_CELL_H - sp->h) / 2;

   for (yy = 0; yy < sp->h; yy++)
   {
      int y = oy + yy;
      if (y < cy0 || y >= cy1)
         continue;
      for (xx = 0; xx < sp->w; xx++)
      {
         unsigned short v = pix[xx + yy * sp->w];
         int x = ox + xx;
         if (!v || x < cx0 || x >= cx1)   /* 0 = transparente */
            continue;
         set_pixel(framebuffer, x, y, get_color((v >> 11) & 31, (v >> 5) & 63, v & 31));
      }
   }
}

static void draw_credits(void)
{
   int box_w = CREDITS_W, box_h = CREDITS_H;
   int box_x0 = (640 - box_w) / 2;
   int box_y0 = (480 - box_h) / 2;
   int left = box_x0 + CREDITS_MARGIN;
   int cx0 = box_x0 + 8, cx1 = box_x0 + box_w - 8;
   int cy0 = box_y0 + 4, cy1 = box_y0 + box_h - 4;
   int content_h, total_scroll, offset, i;

   if (!credits_rows_built)
   {
      credits_build_rows();
      credits_rows_built = 1;
   }

   content_h    = credits_row_count * CREDITS_LINE_H;
   total_scroll = box_h + content_h;   /* percurso: entra por baixo, sai por cima */
   offset = ((credits_frame * CREDITS_SPEED_Q8) / 256) % total_scroll;

   draw_credits_background(box_x0, box_y0);
   /* fundo2.png ja traz a transparencia/esmaecimento desejado embutido na
      propria imagem, entao nenhum escurecimento extra e aplicado por cima
      (o antigo overlay CREDITS_IMG_DARKEN_ALPHA foi removido). */
   draw_box(framebuffer, get_color(14, 28, 18), box_x0, box_y0, box_x0 + box_w - 1, box_y0 + box_h - 1);

   /* logo: centralizado na caixa, sobe junto com o texto; sua base fica CREDITS_LOGO_GAP px
      acima da 1a barra (que esta na linha CREDITS_LOGO_ROWS, a LINE_H/2-1 px do topo dela) */
   {
      int logo_y = box_y0 + box_h - offset + CREDITS_LOGO_ROWS * CREDITS_LINE_H
                 + (CREDITS_LINE_H / 2 - 1) - CREDITS_LOGO_GAP - CREDITS_LOGO_H;
      if (logo_y < cy1 && logo_y + CREDITS_LOGO_H > cy0)
         draw_credits_logo(box_x0 + (box_w - CREDITS_LOGO_W) / 2, logo_y, cy0, cy1);
   }

   for (i = 0; i < credits_row_count; i++)
   {
      const credits_row_t *r = &credits_rows[i];
      int y = box_y0 + box_h - offset + i * CREDITS_LINE_H;
      unsigned short color = r->accent ? get_color(31, 52, 6) : get_color(29, 58, 25);

      /* verde (mesmo do equalizador): linhas que comecam com "* " (accent 3) e a linha
         "USE WITH JOYSTICK:" */
      if (r->kind == CR_TEXT &&
          (r->accent == 3 || strncmp(r->c2, CREDITS_GREEN_TXT, strlen(CREDITS_GREEN_TXT)) == 0))
         color = CREDITS_GREEN;

      if (y < box_y0 - 2 * CREDITS_LINE_H || y > box_y0 + box_h)
         continue;

      switch (r->kind)
      {
         case CR_TITLE:
            /* fonte/degrade do CHIP: em vez da fonte normal (cor 'color' ignorada aqui) */
            credits_chip_draw_line(r->c2, left, y, r->accent == 2, cx0, cx1, cy0, cy1);
            break;
         case CR_RULE:
         {
            int ry = y + CREDITS_LINE_H / 2 - 1, rx;
            if (ry >= cy0 && ry < cy1)
               for (rx = left; rx < box_x0 + box_w - CREDITS_MARGIN; rx++)
                  set_pixel(framebuffer, rx, ry, get_color(14, 28, 18));
            break;
         }
         case CR_TEXT:
            if (r->c1[0])
               credits_draw_text_at(r->c1, left, y, color, cx0, cx1, cy0, cy1);
            credits_draw_text_at(r->c2, left + r->x, y, color, cx0, cx1, cy0, cy1);
            break;
         default:
            break;
      }
   }

   /* sprites animados: acompanham o texto, por cima dele */
   {
      static int sec_bar0[CREDITS_SEC_N];   /* linha (credits_rows) da barra que abre a secao, -1 = nao achou */
      static int sec_bar1[CREDITS_SEC_N];   /* linha que fecha a secao: barra seguinte ou linha de end_txt */
      static int sec_end_is_text[CREDITS_SEC_N];   /* 1 = sec_bar1 e uma linha de texto (usa o topo dela) */
      static int sec_ready = 0;
      int base = box_y0 + box_h - offset;
      int k, n;

      if (!sec_ready)
      {
         for (k = 0; k < CREDITS_SEC_N; k++)
         {
            int j, len = (int)strlen(credits_secs[k].txt);
            sec_bar0[k] = sec_bar1[k] = -1;
            for (j = 0; j < credits_row_count; j++)
               if (strncmp(credits_rows[j].c2, credits_secs[k].txt, len) == 0)
                  break;
            if (j >= credits_row_count)
               continue;
            while (j >= 0 && credits_rows[j].kind != CR_RULE)   /* barra acima da linha */
               j--;
            if (j < 0)
               continue;
            sec_bar0[k] = j;
            for (j = j + 1; j < credits_row_count; j++)          /* proxima barra */
               if (credits_rows[j].kind == CR_RULE)
               {
                  sec_bar1[k] = j;
                  break;
               }
            sec_end_is_text[k] = 0;
            if (credits_secs[k].end_txt)                          /* ... ou a linha de texto de fim */
            {
               int e, elen = (int)strlen(credits_secs[k].end_txt);
               for (e = sec_bar0[k] + 1; e < credits_row_count; e++)
                  if (strncmp(credits_rows[e].c2, credits_secs[k].end_txt, elen) == 0)
                  {
                     sec_bar1[k] = e;
                     sec_end_is_text[k] = 1;
                     break;
                  }
            }
         }
         sec_ready = 1;
      }

      for (k = 0; k < CREDITS_SEC_N; k++)
      {
         int bar0, bar1, span;
         if (sec_bar0[k] < 0 || sec_bar1[k] < 0)
            continue;
         /* y das linhas das barras (a barra e desenhada LINE_H/2-1 px abaixo do topo da linha); se a secao
            termina num texto, o limite e o topo da linha desse texto */
         bar0 = base + sec_bar0[k] * CREDITS_LINE_H + CREDITS_LINE_H / 2 - 1;
         bar1 = base + sec_bar1[k] * CREDITS_LINE_H + (sec_end_is_text[k] ? 0 : CREDITS_LINE_H / 2 - 1);
         span = bar1 - bar0;
         if (bar1 < cy0 || bar0 > cy1)
            continue;
         for (n = 0; n < credits_secs[k].n; n++)
         {
            int cnt = credits_secs[k].n;
            /* N sprites: N+1 espacos iguais entre as barras */
            int sy = bar0 + ((span - cnt * CREDITS_SPR_CELL_H) * (n + 1)) / (cnt + 1) + n * CREDITS_SPR_CELL_H;
            int id = credits_secs[k].spr[n];
            if (sy >= cy1 || sy + CREDITS_SPR_CELL_H <= cy0)
               continue;
            if (id < 0)
               draw_credits_sonic(box_x0 + CREDITS_SONIC_X, sy, cx0, cx1, cy0, cy1);
            else
               draw_credits_sprite(id, box_x0 + CREDITS_SONIC_X, sy, cx0, cx1, cy0, cy1);
         }
      }
   }
}

/* ---- sprites que cruzam o player: naves (Xevious, Galaga, extra) e o Arthur correndo ----
 * Desenhados logo depois do fundo e ANTES da interface, entao ficam por cima so do background e
 * por baixo de todo o resto. So avancam enquanto uma musica esta tocando (pausa/stop congelam o
 * relogio) e nao com a janela de creditos aberta. Todos usam a mesma transparencia.
 * Ciclo de XEV_SHIP_CYCLE frames (20 s):
 *  - no inicio, uma nave (Xevious, Galaga, extra, Xevious, ...) numa coluna x ALEATORIA, cruzando a
 *    tela de uma ponta a outra: Xevious e Galaga sobem (de baixo pra cima), a extra desce;
 *  - antes de tudo, os primeiros XEV_START_DELAY (10 s) de musica nao mostram nada;
 *  - depois (XEV_RUN_START), um corredor (Arthur, Mario, Sonic ou o carro) corre de uma ponta a outra da tela, vindo da esquerda ou da
 *    direita (sorteado), com os pes na linha do horizonte do fundo. Roda o GIF COMPLETO (42 quadros:
 *    sem armadura, prata, verde e dourada), em laco, enquanto atravessa a tela.
 * As velocidades sao numeros INTEIROS de px por frame: o passo e sempre igual, movimento liso. */
#define XEV_SHIP_CYCLE      (20 * 60)    /* uma nave + uma corrida a cada 20 s */
#define XEV_SHIP_SPEED      1            /* px por frame (60 px/s a 60 fps); use 2 pra ficar mais rapida */
#define XEV_SHIP_TRANSP     75           /* transparencia em % (75 = 25% opaca; era 60, +15%) */
#define XEV_SHIP_ALPHA      ((255 * (100 - XEV_SHIP_TRANSP)) / 100)

#define XEV_RUN_START       (7 * 60)     /* frame do ciclo em que o Arthur comeca a correr */
#define XEV_RUN_SPEED       1            /* px por frame (60 px/s a 60 fps; 2 = bem mais rapido) */
#define XEV_RUN_LINE_Y      286          /* y da linha do horizonte do fundo (640x480): os pes pousam nela */
#define XEV_RUN_SCALE_PCT   150          /* base de tamanho em % usada por Mario e Sonic (a altura deles parte da do Arthur) */
#define XEV_START_DELAY     (10 * 60)    /* nenhuma animacao (nave ou corredor) aparece antes de 10 s de musica tocando */
#define XEV_ARTHUR_SCALE_PCT 175         /* tamanho do Arthur em % (era 150; 100 = original, 200 = dobro) */
#define XEV_RUN_SMALL_PCT   85           /* Mario e Sonic: % da altura do Arthur (100 = igual, 85 = um pouco menores) */
#define XEV_RUN_SPEED_SONIC 4            /* px por frame do Sonic (o Sonic corre mais rapido que os outros) */
#define XEV_CAR_SCALE_PCT   62           /* tamanho do carro cruzando a tela, em % do sprite original (202x64); era 55, um pouco maior */
#define XEV_RUN_SPEED_CAR   4            /* velocidade do carro em MEIOS pixels por frame (11 = 5,5 px; 10 = 5 px, 12 = 6 px): ainda bem mais rapido que os corredores */
#define XEV_DIAG_ODDS       6            /* a nave sai na diagonal em 1 de cada 6 vezes (maior = mais raro) */
#define XEV_REPEAT_ODDS     6            /* sorteio inteligente: um sprite repete o lado/direcao da ultima vez em 1 de cada 6 */
#define XEV_MIN_COL_JUMP    160          /* a coluna da nave muda pelo menos isso (px) em relacao a ultima vez dela */
#define XEV_SIDE_LANE_Y     170          /* y (centro) por onde passam as naves horizontais (marcado em vermelho no print) */
#define XEV_SIDE_SPEED      2            /* px por frame das naves horizontais */
#define XEV_FLAME_TICKS     5            /* frames do jogo por frame da animacao do fogo (5 = ~80 ms a 60 fps) */
#define XEV_RUNNER_COUNT    14           /* corredores: Arthur, Mario, Sonic, Carro + 8 extras (revezam a cada ciclo) */

typedef struct
{
   int w, h;
   int scale;                   /* ampliacao do sprite em % (200 = 2x) */
   int down;                    /* 0 = sobe (de baixo pra cima), 1 = desce (de cima pra baixo) */
   int flipv;                   /* 1 = espelha o sprite na vertical */
   const unsigned short *pix;   /* RGB565, 0 = transparente */
   int side;                    /* 1 = nave horizontal (virada pra direita): cruza a tela da esquerda pra direita e vice-versa */
   int frames;                  /* quantos frames empilhados tem o sprite (animacao, ex. fogo da turbina); 0 ou 1 = sem animacao */
} ship_spr_t;

#define XEV_SHIP_COUNT 8
static const ship_spr_t ship_sprs[XEV_SHIP_COUNT] = {
   { XEVIOUS_SHIP_W, XEVIOUS_SHIP_H, 200, 0, 0, xevious_ship_rgb, 0, XEVIOUS_SHIP_FRAMES },   /* 16x17 a 200%, sobe, fogo vermelho animado */
   { GALAGA_SHIP_W,  GALAGA_SHIP_H,  200, 0, 0, galaga_ship_rgb,  0, GALAGA_SHIP_FRAMES },   /* 15x19 a 200%, sobe, fogo animado nas 2 turbinas */
   { RAIDEN_SHIP_W,  RAIDEN_SHIP_H,  170, 0, 0, raiden_ship_rgb,  0, RAIDEN_SHIP_FRAMES },   /* 22x27 a 170%, fogo animado nas 2 turbinas */
   { VIPER_SHIP_W,   VIPER_SHIP_H,   145, 0, 0, viper_ship_rgb,   1, VIPER_SHIP_FRAMES },   /* Vic Viper (Gradius): 32 px a 145% = 46 de largura, com fogo azul animado na traseira */
   { PULSTAR_SHIP_W, PULSTAR_SHIP_H, 125, 0, 0, pulstar_ship_rgb, 1, PULSTAR_SHIP_FRAMES },   /* Pulstar: 33 px a 125% = 41 de largura, com fogo azul animado */
   { RTYPE_SHIP_W,   RTYPE_SHIP_H,   125, 0, 0, rtype_ship_rgb,   1, RTYPE_SHIP_FRAMES },   /* R-9 (R-Type): 35 px a 125% = 43 de largura, com fogo azul animado */
   { CHOPLIFTER_SHIP_W, CHOPLIFTER_SHIP_H, 170, 0, 0, choplifter_ship_rgb, 1, CHOPLIFTER_SHIP_FRAMES },   /* Choplifter: helicoptero 41x16 a 170% = 69 de largura, helices (rotor principal e da cauda) animadas */
   { RALLYX_CARS_W,  RALLYX_CARS_HEIGHT, 200, 0, 0, rallyx_cars_rgb, 0 }  /* Rally-X: os 2 carros juntos (34x36 a 200% = 68x72, 1 pixel de brilho em cada roda), sobem/descem/diagonal como as naves */
};

static unsigned int xev_seed = 0;      /* 0 = ainda nao semeado: a 1a chamada sorteia a semente (muda a cada execucao) */
static unsigned int xev_rand(void)
{
   if (xev_seed == 0)
   {
      unsigned int s = (unsigned int)time(NULL) ^ ((unsigned int)clock() * 2654435761u) ^ (unsigned int)(size_t)&xev_seed;
      s ^= s >> 16; s *= 0x7feb352du; s ^= s >> 15; s *= 0x846ca68bu; s ^= s >> 16;   /* embaralha bem os bits */
      xev_seed = s ? s : 0x9E3779B9u;
   }
   xev_seed ^= xev_seed << 13;   /* xorshift32 */
   xev_seed ^= xev_seed >> 17;
   xev_seed ^= xev_seed << 5;
   return xev_seed >> 16;
}

/* desenha um sprite RGB565 (0 = transparente) com a transparencia XEV_SHIP_ALPHA; recorta nas bordas da tela.
 * pct = tamanho em % (100 = original, 150 = 1,5x), amostragem por vizinho mais proximo */
static void xev_draw_sprite_pct(const unsigned short *pix, int w, int h, int pct,
                                int flipv, int fliph, int x, int y)
{
   uint16_t *fb = (uint16_t *)framebuffer->pixel_data;
   int dw = w * pct / 100, dh = h * pct / 100;
   int dx, dy;

   for (dy = 0; dy < dh; dy++)
   {
      int py = y + dy, sy = dy * 100 / pct;
      if (py < 0 || py >= 480)
         continue;
      if (sy >= h) sy = h - 1;
      for (dx = 0; dx < dw; dx++)
      {
         int px = x + dx, sx = dx * 100 / pct;
         unsigned short v, c, bg;
         int ir, ig, ib, br, bgg, bb;
         uint16_t *dst;
         if (px < 0 || px >= 640)
            continue;
         if (sx >= w) sx = w - 1;
         v = pix[(fliph ? w - 1 - sx : sx) + (flipv ? h - 1 - sy : sy) * w];
         if (!v)
            continue;
         c  = get_color((v >> 11) & 31, (v >> 5) & 63, v & 31);
         ir = (c >> 11) & 31; ig = (c >> 5) & 63; ib = c & 31;
         dst = &fb[px + py * framebuffer->width];
         bg  = *dst;
         br = (bg >> 11) & 31; bgg = (bg >> 5) & 63; bb = bg & 31;
         *dst = get_color((ir * XEV_SHIP_ALPHA + br  * (255 - XEV_SHIP_ALPHA)) / 255,
                          (ig * XEV_SHIP_ALPHA + bgg * (255 - XEV_SHIP_ALPHA)) / 255,
                          (ib * XEV_SHIP_ALPHA + bb  * (255 - XEV_SHIP_ALPHA)) / 255);
      }
   }
}

/* desenha um sprite RGB565 girado "deg" graus no sentido horario (0 = aponta pra cima), ampliado a "pct" %,
 * centrado em (cx, cy), com a mesma transparencia das naves. Amostragem por vizinho mais proximo. */
static void xev_draw_sprite_rot(const unsigned short *pix, int w, int h, int pct, int deg, int cx, int cy)
{
   uint16_t *fb = (uint16_t *)framebuffer->pixel_data;
   float rad = (float)deg * 3.14159265f / 180.0f;
   float cs = cosf(rad), sn = sinf(rad);
   int half = (int)(sqrtf((float)(w * w + h * h)) * (float)pct / 100.0f * 0.5f) + 2;
   int dx, dy;

   for (dy = -half; dy <= half; dy++)
   {
      int py = cy + dy;
      if (py < 0 || py >= 480)
         continue;
      for (dx = -half; dx <= half; dx++)
      {
         int px = cx + dx, sx, sy;
         float fx, fy;
         unsigned short v, c, bg;
         int ir, ig, ib, br, bgg, bb;
         uint16_t *dst;
         if (px < 0 || px >= 640)
            continue;
         /* gira o ponto de destino de volta pro sprite original */
         fx = ( (float)dx * cs + (float)dy * sn) * 100.0f / (float)pct + (float)w * 0.5f;
         fy = (-(float)dx * sn + (float)dy * cs) * 100.0f / (float)pct + (float)h * 0.5f;
         if (fx < 0.0f || fy < 0.0f)
            continue;
         sx = (int)fx; sy = (int)fy;
         if (sx >= w || sy >= h)
            continue;
         v = pix[sx + sy * w];
         if (!v)
            continue;
         c  = get_color((v >> 11) & 31, (v >> 5) & 63, v & 31);
         ir = (c >> 11) & 31; ig = (c >> 5) & 63; ib = c & 31;
         dst = &fb[px + py * framebuffer->width];
         bg  = *dst;
         br = (bg >> 11) & 31; bgg = (bg >> 5) & 63; bb = bg & 31;
         *dst = get_color((ir * XEV_SHIP_ALPHA + br  * (255 - XEV_SHIP_ALPHA)) / 255,
                          (ig * XEV_SHIP_ALPHA + bgg * (255 - XEV_SHIP_ALPHA)) / 255,
                          (ib * XEV_SHIP_ALPHA + bb  * (255 - XEV_SHIP_ALPHA)) / 255);
      }
   }
}

/* direcoes possiveis das naves: vx/vy (-1, 0, 1) e o giro do sprite (0 = aponta pra cima, sentido horario) */
typedef struct { int vx, vy, deg; } ship_head_t;
#define XEV_HEAD_COUNT 6
static const ship_head_t xev_heads[XEV_HEAD_COUNT] = {
   {  0, -1,   0 },   /* sobe */
   {  0,  1, 180 },   /* desce */
   {  1, -1,  45 },   /* diagonal: sobe indo pra direita */
   {  1,  1, 135 },   /* diagonal: desce indo pra direita */
   { -1,  1, 225 },   /* diagonal: desce indo pra esquerda */
   { -1, -1, 315 }    /* diagonal: sobe indo pra esquerda */
};

/* sorteio "inteligente": embaralha 0..n-1 num saco; o 1o do novo saco nunca e o ultimo do saco anterior */
static void xev_shuffle(int *bag, int n, int last)
{
   int i, j, tmp;
   for (i = 0; i < n; i++)
      bag[i] = i;
   for (i = n - 1; i > 0; i--)
   {
      j = (int)(xev_rand() % (unsigned)(i + 1));
      tmp = bag[i]; bag[i] = bag[j]; bag[j] = tmp;
   }
   if (last >= 0 && bag[0] == last)
   {
      j = 1 + (int)(xev_rand() % (unsigned)(n - 1));
      tmp = bag[0]; bag[0] = bag[j]; bag[j] = tmp;
   }
}

/* escolhe 0/1 pra um sprite: na 1a vez e aleatorio; depois quase sempre o OPOSTO da ultima vez (repete so ~1 em 6) */
static int xev_pick_side(int last)
{
   if (last < 0)
      return (int)((xev_rand() >> 3) & 1);
   return (xev_rand() % XEV_REPEAT_ODDS == 0) ? last : !last;
}

/* escolhe a direcao da nave: quase sempre VERTICAL (sobe/desce, alternando em relacao a ultima vez);
 * so raramente (1 em XEV_DIAG_ODDS) vai pra uma das 4 diagonais, tambem evitando repetir a ultima */
static int xev_pick_head(int last)
{
   if (xev_rand() % XEV_DIAG_ODDS == 0)
   {
      int r = 2 + (int)(xev_rand() % 4);
      if (r == last && xev_rand() % XEV_REPEAT_ODDS != 0)
         r = 2 + (r - 2 + 1 + (int)(xev_rand() % 3)) % 4;
      return r;
   }
   if (last == 0 || last == 1)
      return (xev_rand() % XEV_REPEAT_ODDS == 0) ? last : 1 - last;
   return (int)((xev_rand() >> 3) & 1);
}

/* ---- agendamento: nave e corredor INDEPENDENTES (podem aparecer juntos) ----
 * Cada um tem o seu proprio ciclo: entra, cruza a tela, sai, sorteia a SUA pausa (gap) e entra de novo. Nenhum espera o outro;
 * como as pausas e as velocidades sao diferentes, as entradas/saidas nunca ficam sincronizadas.
 * Nada aparece antes de 10 s de musica. */
#define XEV_SHIP_GAP_MIN    (4 * 60)     /* pausa depois que a nave sai da tela, ate a PROXIMA NAVE entrar: de 4 a 14 s (sorteada) */
#define XEV_SHIP_GAP_MAX    (14 * 60)
#define XEV_RUN_GAP_MIN     (3 * 60)     /* pausa depois que o corredor sai da tela, ate o PROXIMO CORREDOR entrar: de 3 a 11 s (sorteada) */
#define XEV_RUN_GAP_MAX     (11 * 60)

static void draw_xevious_ship(void)
{
   static int clock = 0, started = 0;
   static int ship_bag[XEV_SHIP_COUNT], ship_pos = XEV_SHIP_COUNT, ship_idx = -1, ship_head = 0, ship_x = 0;
   static int ship_active = 0, ship_t = 0, ship_seen = 0, ship_next = 0;
   static int run_bag[XEV_RUNNER_COUNT], run_pos = XEV_RUNNER_COUNT, run_idx = -1, run_right = 1;
   static int run_active = 0, run_t = 0, run_seen = 0, run_next = 0;
   static int ship_last_head[XEV_SHIP_COUNT] = { -1, -1, -1, -1, -1, -1, -1, -1 };
   static int ship_last_x[XEV_SHIP_COUNT]    = { -1, -1, -1, -1, -1, -1, -1, -1 };
   static int run_last_side[XEV_RUNNER_COUNT] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
   const ship_spr_t *sp;
   int w, h, x, vis;

   if (!get_is_playing() || credits_open_state)
      return;                      /* parado/pausado: congela (nem desenha) */

   if (!started)                   /* a cada execucao: nave e corredor tem 1o aparecimento proprio, entre 10 e 22 s de musica */
   {
      started   = 1;
      ship_next = XEV_START_DELAY + (int)(xev_rand() % (unsigned)(12 * 60 + 1));
      run_next  = XEV_START_DELAY + (int)(xev_rand() % (unsigned)(12 * 60 + 1));
   }
   clock++;

   /* nave: hora de sair -> sorteia qual (saco embaralhado), direcao e coluna */
   if (!ship_active && clock >= ship_next)
   {
      int tries;

      if (ship_pos >= XEV_SHIP_COUNT)
      {
         xev_shuffle(ship_bag, XEV_SHIP_COUNT, ship_idx);
         ship_pos = 0;
      }
      ship_idx  = ship_bag[ship_pos++];
      sp        = &ship_sprs[ship_idx];
      w         = sp->w * sp->scale / 100;
      if (sp->side)
         ship_head = xev_pick_side(ship_last_head[ship_idx]);   /* 1 = esquerda -> direita, 0 = direita -> esquerda; raramente repete */
      else
      {
         ship_head = xev_pick_head(ship_last_head[ship_idx]);   /* sobe/desce; diagonal so raramente */
         for (tries = 0; tries < 8; tries++)                    /* coluna: longe da ultima desta nave */
         {
            ship_x = (int)(xev_rand() % (unsigned)(640 - w + 1));
            if (ship_last_x[ship_idx] < 0 || xev_rand() % XEV_REPEAT_ODDS == 0 ||
                abs(ship_x - ship_last_x[ship_idx]) >= XEV_MIN_COL_JUMP)
               break;
         }
         ship_last_x[ship_idx] = ship_x;
      }
      ship_last_head[ship_idx] = ship_head;
      ship_active = 1;
      ship_t      = 0;
      ship_seen   = 0;
   }

   /* corredor: hora de sair -> sorteia quem (saco embaralhado) e o lado oposto ao da ultima vez dele */
   if (!run_active && clock >= run_next)
   {
      if (run_pos >= XEV_RUNNER_COUNT)
      {
         xev_shuffle(run_bag, XEV_RUNNER_COUNT, run_idx);
         run_pos = 0;
      }
      run_idx   = run_bag[run_pos++];
      run_right = xev_pick_side(run_last_side[run_idx]);            /* 1 = vem da esquerda, 0 = da direita */
      run_last_side[run_idx] = run_right;
      run_active = 1;
      run_t      = 0;
      run_seen   = 0;
   }

   /* nave horizontal (Vic Viper, Pulstar, R-9): cruza a tela na altura XEV_SIDE_LANE_Y, da esquerda pra direita ou vice-versa;
      nave vertical: cruza a tela subindo, descendo ou na diagonal (o sprite gira pra apontar pra onde vai) */
   if (ship_active)
   {
      vis = 0;
      sp  = &ship_sprs[ship_idx];
      w   = sp->w * sp->scale / 100;
      h   = sp->h * sp->scale / 100;
      if (sp->side)
      {
         x = ship_head ? -w + ship_t * XEV_SIDE_SPEED : 640 - ship_t * XEV_SIDE_SPEED;
         if (x < 640 && x + w > 0)
         {
            xev_draw_sprite_pct(sp->pix + ((sp->frames > 1) ? ((ship_t / XEV_FLAME_TICKS) % sp->frames) * sp->w * sp->h : 0),
                                sp->w, sp->h, sp->scale, 0, !ship_head, x, XEV_SIDE_LANE_Y - h / 2);
            vis = 1;
         }
      }
      else
      {
         const ship_head_t *hd = &xev_heads[ship_head];
         int hw, cx0, cy0, cx, cy;

         hw = (int)(sqrtf((float)(w * w + h * h)) * 0.5f) + 2;      /* raio do sprite girado */

         if (hd->vx == 0)
            cx0 = ship_x + w / 2;                                    /* coluna sorteada */
         else if (hd->vx > 0)
            cx0 = ship_x * 3 / 5 - hw;                               /* comeca fora da tela, a esquerda */
         else
            cx0 = 640 + hw - ship_x * 3 / 5;                         /* comeca fora da tela, a direita */
         cy0 = (hd->vy > 0) ? -hw : 480 + hw;

         cx = cx0 + hd->vx * ship_t * XEV_SHIP_SPEED;
         cy = cy0 + hd->vy * ship_t * XEV_SHIP_SPEED;
         if (cx > -hw && cx < 640 + hw && cy > -hw && cy < 480 + hw)
         {
            xev_draw_sprite_rot(sp->pix + ((sp->frames > 1) ? ((ship_t / XEV_FLAME_TICKS) % sp->frames) * sp->w * sp->h : 0),
                                sp->w, sp->h, sp->scale, hd->deg, cx, cy);
            vis = 1;
         }
      }
      if (vis)
         ship_seen = 1;
      ship_t++;
      if ((ship_seen && !vis) || ship_t > 1500)       /* saiu da tela: sorteia a pausa ate a PROXIMA NAVE (independe do corredor) */
      {
         ship_active = 0;
         ship_next   = clock + XEV_SHIP_GAP_MIN + (int)(xev_rand() % (unsigned)(XEV_SHIP_GAP_MAX - XEV_SHIP_GAP_MIN + 1));
      }
   }

   /* corredor (Arthur, Mario, Sonic, o carro e os extras) na linha do horizonte, vindo da esquerda ou da direita */
   if (run_active)
   {
      int tr = run_t;
      int who = run_idx;                                  /* corredor sorteado desta vez */
      int rw, rh, rfeet, rms, rframes, pct, f, aw, af, spd;
      const unsigned short *pix;

      switch (who)
      {
         case 1:  rw = MARIO_RUN_W;  rh = MARIO_RUN_H;  rfeet = MARIO_RUN_FEET;  rms = MARIO_RUN_MS;
                  rframes = MARIO_RUN_FRAMES;  break;
         case 2:  rw = SONIC_RUN_W;  rh = SONIC_RUN_H;  rfeet = SONIC_RUN_FEET;  rms = SONIC_RUN_MS;
                  rframes = SONIC_RUN_FRAMES;  break;
         case 3:  rw = CAR_RUN_W;   rh = CAR_RUN_H;   rfeet = CAR_RUN_FEET;   rms = CAR_RUN_MS;
                  rframes = CAR_RUN_FRAMES;  break;
         case 4:  rw = MARIO16_RUN_W;  rh = MARIO16_RUN_H;  rfeet = MARIO16_RUN_FEET;  rms = MARIO16_RUN_MS;
                  rframes = MARIO16_RUN_FRAMES;  break;
         case 5:  rw = FATGUY_RUN_W;  rh = FATGUY_RUN_H;  rfeet = FATGUY_RUN_FEET;  rms = FATGUY_RUN_MS;
                  rframes = FATGUY_RUN_FRAMES;  break;
         case 6:  rw = MEGAMAN_RUN_W;  rh = MEGAMAN_RUN_H;  rfeet = MEGAMAN_RUN_FEET;  rms = MEGAMAN_RUN_MS;
                  rframes = MEGAMAN_RUN_FRAMES;  break;
         case 7:  rw = MICKEY_RUN_W;  rh = MICKEY_RUN_H;  rfeet = MICKEY_RUN_FEET;  rms = MICKEY_RUN_MS;
                  rframes = MICKEY_RUN_FRAMES;  break;
         case 8:  rw = PACMAN_RUN_W;  rh = PACMAN_RUN_H;  rfeet = PACMAN_RUN_FEET;  rms = PACMAN_RUN_MS;
                  rframes = PACMAN_RUN_FRAMES;  break;
         case 9:  rw = RYU_RUN_W;  rh = RYU_RUN_H;  rfeet = RYU_RUN_FEET;  rms = RYU_RUN_MS;
                  rframes = RYU_RUN_FRAMES;  break;
         case 10:  rw = KEN_RUN_W;  rh = KEN_RUN_H;  rfeet = KEN_RUN_FEET;  rms = KEN_RUN_MS;
                  rframes = KEN_RUN_FRAMES;  break;
         case 11:  rw = RAYMAN_RUN_W;  rh = RAYMAN_RUN_H;  rfeet = RAYMAN_RUN_FEET;  rms = RAYMAN_RUN_MS;
                  rframes = RAYMAN_RUN_FRAMES;  break;
         case 12:  rw = SHINOBI_RUN_W;  rh = SHINOBI_RUN_H;  rfeet = SHINOBI_RUN_FEET;  rms = SHINOBI_RUN_MS;
                  rframes = SHINOBI_RUN_FRAMES;  break;
         case 13:  rw = ALEX_RUN_W;  rh = ALEX_RUN_H;  rfeet = ALEX_RUN_FEET;  rms = ALEX_RUN_MS;
                  rframes = ALEX_RUN_FRAMES;  break;
         default: rw = ARTHUR_RUN_W; rh = ARTHUR_RUN_H; rfeet = ARTHUR_RUN_FEET; rms = ARTHUR_RUN_MS;
                  rframes = ARTHUR_RUN_FRAMES; break;
      }
      f = (tr * 1000 / 60 / rms) % rframes;                     /* usa a duracao de cada GIF */
      switch (who)
      {
         case 1:  pix = mario_run_rgb[f];  break;
         case 2:  pix = sonic_run_rgb[f];  break;
         case 3:  pix = car_run_rgb[f];    break;
         case 4:  pix = mario16_run_rgb[f];  break;
         case 5:  pix = fatguy_run_rgb[f];  break;
         case 6:  pix = megaman_run_rgb[f];  break;
         case 7:  pix = mickey_run_rgb[f];  break;
         case 8:  pix = pacman_run_rgb[f];  break;
         case 9:  pix = ryu_run_rgb[f];  break;
         case 10:  pix = ken_run_rgb[f];  break;
         case 11:  pix = rayman_run_rgb[f];  break;
         case 12:  pix = shinobi_run_rgb[f];  break;
         case 13:  pix = alex_run_rgb[f];  break;
         default: pix = arthur_run_rgb[f]; break;
      }
      if (who == 0)
         pct = XEV_ARTHUR_SCALE_PCT * ARTHUR_RUN_H / rh;        /* Arthur */
      else if (who == 3)
         pct = XEV_CAR_SCALE_PCT;                                /* Carro: escala propria, nao relativa ao Arthur */
      else
         pct = XEV_RUN_SCALE_PCT * ARTHUR_RUN_H / rh * XEV_RUN_SMALL_PCT / 100;   /* Mario e Sonic: mesmo tamanho de antes */
      if (who >= 4 && who <= 11)
         pct = pct * runners_extra_scale[who - 4] / 100;
      if (who == 13)
         pct = pct * ALEX_RUN_SCALE / 100;                   /* Alex: tamanho proprio */
      if (who == 12)
         pct = pct * SHINOBI_RUN_SCALE / 100;                /* Shinobi: tamanho proprio */       /* ajuste de tamanho de cada corredor extra */
      spd = (who == 13) ? ALEX_RUN_SPEED : (who == 12) ? SHINOBI_RUN_SPEED : (who >= 4) ? runners_extra_speed[who - 4] : (who == 3) ? XEV_RUN_SPEED_CAR : 2 * ((who == 2) ? XEV_RUN_SPEED_SONIC : XEV_RUN_SPEED);   /* em meios pixels por frame */   /* Sonic/carro tem velocidade propria */
      aw  = rw * pct / 100;                                     /* largura ja ampliada */
      af  = (rfeet * pct + 50) / 100;                           /* altura dos pes ja ampliada */
      x = run_right ? -aw + tr * spd / 2 : 640 - tr * spd / 2;
      vis = (x < 640 && x + aw > 0);
      if (vis)
      {
         xev_draw_sprite_pct(pix, rw, rh, pct, 0, !run_right,
                             x, XEV_RUN_LINE_Y - af);   /* a sola do pe fica na linha */
         run_seen = 1;
      }
      run_t++;
      if ((run_seen && !vis) || run_t > 3000)         /* saiu da tela: sorteia a pausa ate o PROXIMO CORREDOR (independe da nave) */
      {
         run_active = 0;
         run_next   = clock + XEV_RUN_GAP_MIN + (int)(xev_rand() % (unsigned)(XEV_RUN_GAP_MAX - XEV_RUN_GAP_MIN + 1));
      }
   }
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name = "Game Music Emu 2";
   info->library_version = "v1.2";
   info->need_fullpath = true;
   info->valid_extensions = "ay|gbs|gym|hes|kss|nsf|nsfe|sap|spc|vgm|vgz|mod|s3m|xm|it|mp3|mid|midi|zip";
   info->block_extract = true;
}

/*
 * Tell libretro about the AV system; the fps, sound sample rate and the
 * resolution of the display.
 */
void retro_get_system_av_info(struct retro_system_av_info *info)
{
   int pixel_format = RETRO_PIXEL_FORMAT_RGB565;
   memset(info, 0, sizeof(*info));
   info->timing.fps            = 60.0f;
   info->timing.sample_rate    = 44100;
   info->geometry.base_width   = 640;
   info->geometry.base_height  = 480;
   info->geometry.max_width    = 640;
   info->geometry.max_height   = 480;
   info->geometry.aspect_ratio = 640.0f / 480.0f;
   environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &pixel_format);
}

void retro_init(void)
{
   unsigned level = 0;
   /* set up some logging */
   struct retro_log_callback log;
   if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      log_cb = log.log;
   else
      log_cb = NULL;

   // the performance level is guide to frontend to give an idea of how intensive this core is to run
   environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);
   framebuffer = create_surface(640,480,2);
}

// End of retrolib
void retro_deinit(void)
{
   if (framebuffer)
      free_surface(framebuffer);
   framebuffer = NULL;
}

// Reset gme
void retro_reset(void)
{
    start_track(0);
}

/* ---- musica da janela de creditos (botao B) ----
 * Enquanto a janela esta aberta, a faixa que estava tocando fica em pausa (play_scan() simplesmente
 * nao e chamado, entao ela continua exatamente de onde parou ao fechar) e este stream toca no lugar.
 * Os dados (credits_music.h) sao IMA ADPCM 4 bits, 44100 Hz estereo, em blocos de
 * CREDITS_MUSIC_BLOCK bytes (formato "IMA WAV"): cabecalho de 4 bytes por canal (predictor int16 +
 * indice + 1 byte de enchimento), depois grupos de 8 amostras por canal (4 bytes, nibble baixo
 * primeiro), canais intercalados. A musica reinicia do comeco a cada abertura e repete no fim. */
static const int cm_index_tab[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
static const short cm_step_tab[89] = {
   7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
   50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
   337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
   2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
   15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static short cm_pcm[CREDITS_MUSIC_SPB * 2];   /* bloco atual decodificado (estereo intercalado) */
static short cm_out[735 * 2];                 /* 1 frame de video de audio (735 amostras a 44,1 kHz / 60 fps) */
static int   cm_block = -1;                   /* bloco decodificado em cm_pcm (-1 = nenhum) */
static int   cm_next  = 0;                    /* proximo bloco a decodificar */
static int   cm_pos   = 0;                    /* amostra atual dentro de cm_pcm */
static int   cm_abs   = 0;                    /* amostras ja tocadas desde o inicio (para repetir no fim) */

static int cm_expand(int nib, int *pred, int *idx)
{
   int step = cm_step_tab[*idx];
   int diff = ((2 * (nib & 7) + 1) * step) >> 3;
   int p    = *pred;
   if (nib & 8)
      diff = -diff;
   p += diff;
   if (p >  32767) p =  32767;
   if (p < -32768) p = -32768;
   *pred = p;
   *idx += cm_index_tab[nib];
   if (*idx < 0)  *idx = 0;
   if (*idx > 88) *idx = 88;
   return p;
}

static void cm_decode_block(int b)
{
   const unsigned char *p = credits_music_data + (size_t)b * CREDITS_MUSIC_BLOCK;
   int pred[2], idx[2], ch, n, k;
   for (ch = 0; ch < 2; ch++)
   {
      pred[ch] = (short)(p[0] | (p[1] << 8));
      idx[ch]  = p[2] > 88 ? 88 : p[2];
      p += 4;
      cm_pcm[ch] = (short)pred[ch];
   }
   for (n = 1; n < CREDITS_MUSIC_SPB; n += 8)
      for (ch = 0; ch < 2; ch++)
         for (k = 0; k < 4; k++)
         {
            unsigned char v = *p++;
            cm_pcm[(n + k * 2)     * 2 + ch] = (short)cm_expand(v & 15, &pred[ch], &idx[ch]);
            cm_pcm[(n + k * 2 + 1) * 2 + ch] = (short)cm_expand(v >> 4,  &pred[ch], &idx[ch]);
         }
   cm_block = b;
}

/* chamada ao abrir a janela de creditos: recomeca a musica do inicio */
static void credits_music_start(void)
{
   cm_block = -1;
   cm_next  = 0;
   cm_pos   = 0;
   cm_abs   = 0;
}

/* 735 amostras estereo da musica dos creditos (mesmo formato de play_scan) */
static short *credits_music_frame(void)
{
   int i;
   for (i = 0; i < 735; i++)
   {
      if (cm_block < 0 || cm_pos >= CREDITS_MUSIC_SPB || cm_abs >= CREDITS_MUSIC_FRAMES)
      {
         if (cm_abs >= CREDITS_MUSIC_FRAMES)   /* fim da musica: repete */
         {
            cm_next = 0;
            cm_abs  = 0;
         }
         cm_decode_block(cm_next);
         cm_next++;
         cm_pos = 0;
      }
      cm_out[i * 2]     = cm_pcm[cm_pos * 2];
      cm_out[i * 2 + 1] = cm_pcm[cm_pos * 2 + 1];
      cm_pos++;
      cm_abs++;
   }
   return cm_out;
}

// Run a single frame
/* toque curto (< ~0,3 s a 60 fps) troca de faixa; segurar mais que isso vira avanco/volta acelerada */
#define SCAN_HOLD_FRAMES 18
static int l_hold_frames = 0;
static int r_hold_frames = 0;

/* repeticao ao segurar CIMA/BAIXO dentro do navegador de zip */
#define BROWSER_REPEAT_DELAY 20   /* frames antes de comecar a repetir (~0,33s a 60fps) */
#define BROWSER_REPEAT_RATE   6   /* frames entre repeticoes depois disso (~0,1s) */
static int down_hold_frames = 0;
static int up_hold_frames   = 0;

/* ---- volume do core (L2 abaixa, R2 aumenta) ----
 * Escala so o audio enviado ao RetroArch; espectro/onda/LED usam o sinal original. */
#define VOL_MAX           200   /* % (100 = volume original; acima disso amplifica com limite) */
#define VOL_STEP           10   /* % por toque */
#define VOL_REPEAT_DELAY   20   /* frames segurando antes de repetir (~0,33 s a 60 fps) */
#define VOL_REPEAT_RATE    4    /* frames entre repeticoes */
#define VOL_OSD_FRAMES     90   /* tempo do aviso na tela (~1,5 s a 60 fps) */
static int volume_pct      = 100;
static int vol_osd_frames  = 0;
static int vol_up_hold     = 0;
static int vol_down_hold   = 0;

static void volume_update(uint16_t pressed, uint16_t held)
{
   int up = 0, down = 0;

   if(pressed & (1<<RETRO_DEVICE_ID_JOYPAD_L2))
   {
      down = 1;
      vol_down_hold = 0;
   }
   else if(held & (1<<RETRO_DEVICE_ID_JOYPAD_L2))
   {
      vol_down_hold++;
      if(vol_down_hold >= VOL_REPEAT_DELAY &&
         (vol_down_hold - VOL_REPEAT_DELAY) % VOL_REPEAT_RATE == 0)
         down = 1;
   }
   else
      vol_down_hold = 0;

   if(pressed & (1<<RETRO_DEVICE_ID_JOYPAD_R2))
   {
      up = 1;
      vol_up_hold = 0;
   }
   else if(held & (1<<RETRO_DEVICE_ID_JOYPAD_R2))
   {
      vol_up_hold++;
      if(vol_up_hold >= VOL_REPEAT_DELAY &&
         (vol_up_hold - VOL_REPEAT_DELAY) % VOL_REPEAT_RATE == 0)
         up = 1;
   }
   else
      vol_up_hold = 0;

   if(up && !down)
      volume_pct += VOL_STEP;
   else if(down && !up)
      volume_pct -= VOL_STEP;
   else
      return;

   if(volume_pct > VOL_MAX) volume_pct = VOL_MAX;
   if(volume_pct < 0)       volume_pct = 0;
   vol_osd_frames = VOL_OSD_FRAMES;
}

/* devolve o buffer a enviar ao frontend (o original se o volume for 100%) */
static short *volume_apply(short *audio, int frames)
{
   static short vol_buf[2048 * 2];
   int i, n = frames * 2;

   if(volume_pct == 100 || n > (int)(sizeof(vol_buf) / sizeof(vol_buf[0])))
      return audio;

   for(i = 0; i < n; i++)
   {
      int v = ((int)audio[i] * volume_pct) / 100;
      if(v >  32767) v =  32767;
      if(v < -32768) v = -32768;
      vol_buf[i] = (short)v;
   }
   return vol_buf;
}

/* aviso "VOL xxx%" com barra, no centro da tela, por alguns instantes */
static void volume_osd_draw(void)
{
   char txt[24];
   int bw = 208, bh = 48;
   int bx = (640 - bw) / 2, by = (480 - bh) / 2;
   int tw, fill, tick;

   if(vol_osd_frames <= 0)
      return;
   vol_osd_frames--;

   snprintf(txt, sizeof(txt), "VOL %d%%", volume_pct);
   tw = text_width_prop(txt);

   draw_shape_alpha(framebuffer, get_color(2, 4, 5),   215, bx,     by,     bw,     bh);
   draw_shape_alpha(framebuffer, get_color(6, 12, 14), 215, bx + 2, by + 2, bw - 4, bh - 4);
   draw_text_prop(txt, bx + (bw - tw) / 2, by + 7, get_color(30, 58, 25));

   /* barra: cheia = VOL_MAX; marca branca = 100% (volume original) */
   draw_shape(framebuffer, get_color(4, 8, 9), bx + 12, by + bh - 16, bw - 24, 8);
   fill = ((bw - 24) * volume_pct) / VOL_MAX;
   if(fill > 0)
      draw_shape(framebuffer, get_color(8, 56, 17), bx + 12, by + bh - 16, fill, 8);
   tick = ((bw - 24) * 100) / VOL_MAX;
   draw_shape(framebuffer, get_color(31, 63, 31), bx + 12 + tick, by + bh - 18, 2, 12);
}

void retro_run(void)
{
   uint16_t input = 0;
   uint16_t realinput = 0;
   int i;
   short *audio;
   uint16_t released = 0;
   int scan_dir = 0;

   // input handling
   input_poll_cb();
   for(i=0;i<16;i++)
   {
      if(input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, i))
         realinput |= 1<<i;
   }
   input = realinput & ~previnput;
   released = previnput & ~realinput;
   previnput = realinput;

   /* L2/R2 = volume (vale em qualquer tela); depois some do "input" para nao
      fechar a janela de creditos nem disparar outras acoes */
   volume_update(input, realinput);
   input &= ~((1<<RETRO_DEVICE_ID_JOYPAD_L2) | (1<<RETRO_DEVICE_ID_JOYPAD_R2));

   /* janela de creditos (B, fora do navegador): enquanto aberta, B pausa/continua
      o texto subindo; qualquer outro botao fecha; nenhum outro controle
      (transporte, navegador) reage */
   if(credits_open_state)
   {
      if(input & (1<<RETRO_DEVICE_ID_JOYPAD_B))
         credits_paused = !credits_paused;
      if(input & ~(1<<RETRO_DEVICE_ID_JOYPAD_B))
         credits_open_state = 0;
   }
   else
   {
   /* SELECT abre/fecha o navegador do zip atual */
   if(input & (1<<RETRO_DEVICE_ID_JOYPAD_SELECT))
   {
      if(!browser_open_state)
      {
         int n = browser_open();
         if(n > 0)
         {
            browser_open_state = 1;
            browser_cursor     = browser_last_cursor();
            browser_count_ui   = n;
            down_hold_frames   = 0;
            up_hold_frames     = 0;
         }
      }
      else
      {
         browser_close();
         browser_open_state = 0;
         down_hold_frames   = 0;
         up_hold_frames     = 0;
      }
   }

   if(browser_open_state)
   {
      {
         int move_down = 0, move_up = 0;

         if(input & (1<<RETRO_DEVICE_ID_JOYPAD_DOWN))
         {
            move_down = 1;
            down_hold_frames = 0;
         }
         else if(realinput & (1<<RETRO_DEVICE_ID_JOYPAD_DOWN))
         {
            down_hold_frames++;
            if(down_hold_frames >= BROWSER_REPEAT_DELAY &&
               (down_hold_frames - BROWSER_REPEAT_DELAY) % BROWSER_REPEAT_RATE == 0)
               move_down = 1;
         }
         else
            down_hold_frames = 0;

         if(input & (1<<RETRO_DEVICE_ID_JOYPAD_UP))
         {
            move_up = 1;
            up_hold_frames = 0;
         }
         else if(realinput & (1<<RETRO_DEVICE_ID_JOYPAD_UP))
         {
            up_hold_frames++;
            if(up_hold_frames >= BROWSER_REPEAT_DELAY &&
               (up_hold_frames - BROWSER_REPEAT_DELAY) % BROWSER_REPEAT_RATE == 0)
               move_up = 1;
         }
         else
            up_hold_frames = 0;

         if(move_down)
         {
            browser_cursor++;
            if(browser_cursor >= browser_count_ui)
               browser_cursor = 0;
         }
         if(move_up)
         {
            browser_cursor--;
            if(browser_cursor < 0)
               browser_cursor = browser_count_ui - 1;
         }
      }
      /* L/R no navegador: pula BROWSER_PAGE_STEP linhas (R desce, L sobe);
         segurando, repete rapido. Passando do fim/inicio da lista, da a volta. */
      {
#define BROWSER_PAGE_STEP 5
         static int page_r_hold = 0, page_l_hold = 0;
         int page_r = 0, page_l = 0;

         if(input & (1<<RETRO_DEVICE_ID_JOYPAD_R))
         {
            page_r = 1;
            page_r_hold = 0;
         }
         else if(realinput & (1<<RETRO_DEVICE_ID_JOYPAD_R))
         {
            page_r_hold++;
            if(page_r_hold >= BROWSER_REPEAT_DELAY &&
               (page_r_hold - BROWSER_REPEAT_DELAY) % BROWSER_REPEAT_RATE == 0)
               page_r = 1;
         }
         else
            page_r_hold = 0;

         if(input & (1<<RETRO_DEVICE_ID_JOYPAD_L))
         {
            page_l = 1;
            page_l_hold = 0;
         }
         else if(realinput & (1<<RETRO_DEVICE_ID_JOYPAD_L))
         {
            page_l_hold++;
            if(page_l_hold >= BROWSER_REPEAT_DELAY &&
               (page_l_hold - BROWSER_REPEAT_DELAY) % BROWSER_REPEAT_RATE == 0)
               page_l = 1;
         }
         else
            page_l_hold = 0;

         if(browser_count_ui <= 0)
            ;   /* lista vazia: nada a mover */
         else if(page_r && !page_l)
            browser_cursor = (browser_cursor + BROWSER_PAGE_STEP) % browser_count_ui;
         else if(page_l && !page_r)
         {
            browser_cursor = (browser_cursor - BROWSER_PAGE_STEP) % browser_count_ui;
            if(browser_cursor < 0)
               browser_cursor += browser_count_ui;
         }
      }
      if(input & (1<<RETRO_DEVICE_ID_JOYPAD_B))
      {
         /* dentro de uma pasta: B sobe um nivel. na raiz: B fecha o navegador */
         int n = browser_up();
         if(n < 0)
         {
            browser_close();
            browser_open_state = 0;
         }
         else
         {
            browser_cursor   = browser_up_cursor();
            browser_count_ui = n;
         }
      }
      else if(input & (1<<RETRO_DEVICE_ID_JOYPAD_A))
      {
         int was_dir = browser_entry_is_dir(browser_cursor);
         browser_select(browser_cursor);
         if(was_dir)
         {
            /* entrou numa pasta: continua no navegador, com a nova listagem */
            browser_cursor   = 0;
            browser_count_ui = browser_count();
         }
         else
         {
            /* tocou um arquivo ou entrou num zip aninhado: fecha o navegador */
            browser_close();
            browser_open_state = 0;
         }
      }
   }
   else
   {
      /* L/R: toque curto troca de faixa (ao soltar); segurando vira voltar/avancar acelerado */
      if(released & (1<<RETRO_DEVICE_ID_JOYPAD_L))
      {
         if(l_hold_frames < SCAN_HOLD_FRAMES)
            prev_track();
         l_hold_frames = 0;
      }
      else if((realinput & (1<<RETRO_DEVICE_ID_JOYPAD_L)) && l_hold_frames < 100000)
         l_hold_frames++;

      if(released & (1<<RETRO_DEVICE_ID_JOYPAD_R))
      {
         if(r_hold_frames < SCAN_HOLD_FRAMES)
            next_track();
         r_hold_frames = 0;
      }
      else if((realinput & (1<<RETRO_DEVICE_ID_JOYPAD_R)) && r_hold_frames < 100000)
         r_hold_frames++;

      {
         int l_scan = (realinput & (1<<RETRO_DEVICE_ID_JOYPAD_L)) && l_hold_frames >= SCAN_HOLD_FRAMES;
         int r_scan = (realinput & (1<<RETRO_DEVICE_ID_JOYPAD_R)) && r_hold_frames >= SCAN_HOLD_FRAMES;
         if(r_scan && !l_scan)
            scan_dir = 1;
         else if(l_scan && !r_scan)
            scan_dir = -1;
      }

      if(input & (1<<RETRO_DEVICE_ID_JOYPAD_START))
         play_pause();

      /* Y = Stop: para a musica (Start volta a tocar do inicio da faixa) */
      if(input & (1<<RETRO_DEVICE_ID_JOYPAD_Y))
         stop_track();

      if(input & (1<<RETRO_DEVICE_ID_JOYPAD_A))
         toggle_loop();

      /* X = alterna o sintetizador MIDI ativo (MT-32 -> TSF -> FluidSynth -> ...);
         sem efeito se a faixa atual nao for um arquivo MIDI */
      if(input & (1<<RETRO_DEVICE_ID_JOYPAD_X))
         cycle_midi_synth();

      /* B (fora do navegador) abre a janela de creditos */
      if(input & (1<<RETRO_DEVICE_ID_JOYPAD_B))
      {
         credits_open_state = 1;
         credits_frame      = 0;
         credits_anim       = 0;
         credits_paused     = 0;
         credits_music_start();   /* pausa a faixa atual e toca a musica dos creditos */
      }
   }
   }

   l_active_ = (realinput & (1<<RETRO_DEVICE_ID_JOYPAD_L)) ? 1 : 0;
   r_active_ = (realinput & (1<<RETRO_DEVICE_ID_JOYPAD_R)) ? 1 : 0;

   //audio primeiro, para o espectro usar os samples deste frame
   /* com a janela de creditos aberta a faixa atual fica em pausa (play_scan nao e chamado)
      e toca a musica dos creditos; ao fechar, a faixa continua de onde parou */
   audio = credits_open_state ? credits_music_frame() : play_scan(scan_dir);
   led_update(audio, 1470);
   spectrum_push(audio, 735);
   spectrum_update();

   //graphic handling
   spectrum_draw_background(framebuffer);
   draw_xevious_ship();   /* acima so do fundo, abaixo de todo o resto */
   draw_ui();
   draw_waveform(audio, 735);
   spectrum_draw_bars(framebuffer);
   draw_vu(audio, 735);
   if(browser_open_state)
      draw_browser();
   if(credits_open_state)
   {
      draw_credits();
      credits_anim++;
      if(!credits_paused)
         credits_frame++;
   }
   volume_osd_draw();
   video_cb(framebuffer->pixel_data, framebuffer->width, framebuffer->height, framebuffer->bytes_per_pixel * framebuffer->width);
   //audio handling
   audio_batch_cb(volume_apply(audio, 735), 735);
}

// File Loading
bool retro_load_game(const struct retro_game_info *info)
{
   const char *sysdir = NULL;
   if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &sysdir) && sysdir)
      set_system_dir(sysdir);
   {
      const char *savedir = NULL;
      if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &savedir) && savedir && savedir[0])
         set_temp_dir(savedir);
      else if (sysdir)
         set_temp_dir(sysdir);
   }
   if (info && open_file(info->path, 44100))
      return true;
   return false;
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info)
{
   return false;
}

void retro_unload_game(void)
{
   close_file();
   player_cleanup_temp();
}
