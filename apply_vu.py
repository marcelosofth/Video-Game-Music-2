#!/usr/bin/env python3
# apply_vu.py - acrescenta 2 VU meters estereo (L e R) ao gme2_libretro.
# Uso (na raiz do repo):  python3 apply_vu.py [src/libretro.c]
# Valida tudo em memoria e aborta SEM alterar nada se algo nao bater.
import sys, os, shutil

path = sys.argv[1] if len(sys.argv) > 1 else "src/libretro.c"

if not os.path.isfile(path):
    sys.exit("ERRO: arquivo nao encontrado: %s (nada foi alterado)" % path)

raw = open(path, "rb").read().decode("latin-1")
crlf = "\r\n" in raw

def nl(s):
    return s.replace("\n", "\r\n") if crlf else s

# ---------------------------------------------------------------- codigo novo
VU_CODE = r'''/* ---- VU meters estereo (2 ponteiros analogicos: L e R) ----
 * Posicao: um em cada lado, entre o osciloscopio e o espectro.
 * Ajuste fino: VU_L_CX / VU_R_CX / VU_CY (centro de cada medidor, em pixels).
 * VU_REF_DB: nivel (dBFS) que corresponde a "0" do VU; menor = ponteiro sobe mais. */
#define VU_L_CX    90
#define VU_R_CX    536
#define VU_CY      250
#define VU_BOX_W   124
#define VU_BOX_H   90
#define VU_R       58
#define VU_ANG     55.0f
#define VU_REF_DB  14.0f
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
   unsigned short c_panel  = get_color(1, 2, 3);
   unsigned short c_border = get_color(4, 12, 14);
   unsigned short c_arc    = get_color(29, 58, 29);
   unsigned short c_inner  = get_color(9, 20, 16);
   unsigned short c_red    = get_color(31, 16, 5);
   unsigned short c_cyan   = get_color(6, 46, 31);
   unsigned short c_green  = get_color(8, 50, 12);
   unsigned short c_needle = get_color(10, 48, 31);
   unsigned short c_base   = get_color(2, 22, 24);
   int x0 = cx - VU_BOX_W / 2;
   int y0 = VU_CY - VU_BOX_H / 2;
   int px = cx;
   int py = y0 + VU_BOX_H - 12;
   int i, rr;
   float a, rad, s, c;

   draw_shape_alpha(framebuffer, c_panel, 150, x0, y0, VU_BOX_W, VU_BOX_H);
   draw_box(framebuffer, c_border, x0, y0, x0 + VU_BOX_W - 1, y0 + VU_BOX_H - 1);

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
              py - (int)(c * (VU_R + 11) + 0.5f) - 2, col);
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
   vu_text(ch ? "R" : "L", x0 + 7, y0 + VU_BOX_H - 8, c_cyan);

   /* ponteiro */
   a   = -VU_ANG + pos * 2.0f * VU_ANG;
   rad = a * VU_PI / 180.0f;
   s = sinf(rad);
   c = cosf(rad);
   for (i = 0; i < 2; i++)
      draw_line(framebuffer, c_needle,
                px + i + (int)(s * 2 + (s < 0 ? -0.5f : 0.5f)), py - (int)(c * 2 + 0.5f),
                px + i + (int)(s * (VU_R - 3) + (s < 0 ? -0.5f : 0.5f)), py - (int)(c * (VU_R - 3) + 0.5f));
   draw_shape(framebuffer, c_base, px - 9, py + 4, 20, 4);
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

'''

# ---------------------------------------------------------------- ancoras
A_INC  = nl("#include <stdio.h>")
A_FUNC = nl("/* ---- navegador: janela com o conteudo do .zip atual ----")
A_RUN  = nl("   draw_waveform(audio, 735);\n   spectrum_draw_bars(framebuffer);")

errors = []
if "draw_vu(" in raw or "VU_L_CX" in raw:
    errors.append("o VU ja parece estar aplicado (draw_vu/VU_L_CX encontrado)")
for name, a in (("include stdio.h", A_INC), ("bloco do navegador", A_FUNC),
                ("chamada em retro_run", A_RUN)):
    n = raw.count(a)
    if n != 1:
        errors.append("ancora '%s' encontrada %d vez(es) (esperado 1)" % (name, n))

if errors:
    print("ABORTADO - nada foi alterado:")
    for e in errors:
        print("  -", e)
    sys.exit(1)

# ---------------------------------------------------------------- aplica (em memoria)
out = raw
if "#include <math.h>" not in out:
    out = out.replace(A_INC, A_INC + nl("\n#include <math.h>"), 1)
out = out.replace(A_FUNC, nl(VU_CODE) + A_FUNC, 1)
out = out.replace(A_RUN, A_RUN + nl("\n   draw_vu(audio, 735);"), 1)

checks = ["draw_vu_one(VU_L_CX", "draw_vu(audio, 735);", "#include <math.h>"]
for ck in checks:
    if ck not in out:
        sys.exit("ABORTADO - verificacao final falhou (%s); nada foi alterado." % ck)

# ---------------------------------------------------------------- grava
shutil.copyfile(path, path + ".bak_vu")
open(path, "wb").write(out.encode("latin-1"))
print("OK: VU meters aplicados em %s (backup: %s.bak_vu)" % (path, path))
print("Agora: make clean && make   (ou o comando de build que voce usa)")
