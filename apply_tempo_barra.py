#!/usr/bin/env python3
# apply_tempo_barra.py - mostra, DENTRO da barra de progresso e alinhado a
# direita, o tempo da musica com minuto, segundo e milesimo (MM:SS.mmm).
# Uso (na raiz do repo):  python3 apply_tempo_barra.py [src/libretro.c]
# Valida tudo em memoria e aborta SEM alterar nada se algo nao bater.
import sys, os, shutil

path = sys.argv[1] if len(sys.argv) > 1 else "src/libretro.c"
if not os.path.isfile(path):
    sys.exit("ERRO: arquivo nao encontrado: %s (nada foi alterado)" % path)

raw = open(path, "rb").read().decode("latin-1")
crlf = "\r\n" in raw
nl = (lambda s: s.replace("\n", "\r\n")) if crlf else (lambda s: s)

A_FUNC = "static void draw_ui(void)\n{"
A_BAR  = "      draw_shape(framebuffer, get_color(12, 25, 28), UI_LEFT, 413, (UI_W * prog) / 1000, 12);\n"

HELPER = '''/* ---- texto pequeno (fonte 8x8 em escala 1; digitos com largura fixa) ----
 * usado para o tempo com milesimos dentro da barra de progresso */
static int small_adv(char ch)
{
   return (ch == ':' || ch == '.') ? 3 : 7;
}

static int small_text_width(const char *text)
{
   int w = 0;
   for (; *text; text++)
      w += small_adv(*text);
   return w;
}

static void draw_text_small(const char *text, int x, int y, unsigned short color)
{
   int gx, gy, x0, x1;
   for (; *text; text++)
   {
      unsigned char ch = (unsigned char)*text;
      int charx = (ch % 16) * 8;
      int chary = (ch >> 4) * 8;
      glyph_extent(ch, &x0, &x1);
      if (x1 >= 0)
         for (gy = 0; gy < 8; gy++)
            for (gx = 0; gx < 8; gx++)
               if (is_font_pixel(charx + gx, chary + gy))
                  set_pixel(framebuffer, x + gx - x0, y + gy, color);
      x += small_adv((char)ch);
   }
}

'''

TIME = '''
   /* tempo da musica (MM:SS.mmm) dentro da barra, alinhado a direita.
    * MM:SS vem do mesmo texto do tempo da esquerda; os milesimos vem dos
    * quadros (60/s) e so sao usados se baterem com os segundos. */
   {
      char ms_text[24];
      int mm = 0, ss = 0, ms = 0;
      int fr = get_track_elapsed_frames();
      get_time_text(message);
      if (sscanf(message, "%d:%d", &mm, &ss) == 2)
      {
         if (fr / 60 == mm * 60 + ss)
            ms = (fr % 60) * 1000 / 60;
         snprintf(ms_text, sizeof(ms_text), "%02d:%02d.%03d", mm, ss, ms);
         draw_text_small(ms_text, UI_RIGHT - 4 - small_text_width(ms_text), 415, get_color(27, 54, 29));
      }
   }
'''

errors = []
if "draw_text_small" in raw:
    errors.append("ja parece aplicado (draw_text_small encontrado)")
for name, a in (("draw_ui", A_FUNC), ("barra de progresso", A_BAR)):
    n = raw.count(nl(a))
    if n != 1:
        errors.append("ancora '%s' encontrada %d vez(es) (esperado 1)" % (name, n))
if errors:
    print("ABORTADO - nada foi alterado:")
    for e in errors:
        print("  -", e)
    sys.exit(1)

out = raw.replace(nl(A_FUNC), nl(HELPER) + nl(A_FUNC), 1)
out = out.replace(nl(A_BAR), nl(A_BAR) + nl(TIME), 1)

for ck in ("draw_text_small(ms_text", "static void draw_text_small"):
    if ck not in out:
        sys.exit("ABORTADO - verificacao final falhou (%s); nada foi alterado." % ck)

shutil.copyfile(path, path + ".bak_tempo")
open(path, "wb").write(out.encode("latin-1"))
print("OK: tempo MM:SS.mmm aplicado em %s (backup: %s.bak_tempo)" % (path, path))
