#!/usr/bin/env python3
# apply_tempo_fix3.py
#   * contador da direita (na barra): SEM transparencia e um pouco mais claro
#     que o cinza da barra
#   * tempo da esquerda: 0:00:00/0:00 (total sem centesimos) e com digitos de
#     largura fixa, para o "/0:00" nao tremer
# Uso (na raiz do repo):  python3 apply_tempo_fix3.py [src/libretro.c]
# Valida tudo em memoria e aborta SEM alterar nada se algo nao bater.
import sys, os, shutil

path = sys.argv[1] if len(sys.argv) > 1 else "src/libretro.c"
if not os.path.isfile(path):
    sys.exit("ERRO: arquivo nao encontrado: %s (nada foi alterado)" % path)

raw = open(path, "rb").read().decode("latin-1")
crlf = "\r\n" in raw
nl = (lambda s: s.replace("\n", "\r\n")) if crlf else (lambda s: s)

TAB = '''/* texto 2x com DIGITOS DE LARGURA FIXA (celula = digito mais largo), para o
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

'''

A_FUNC = "static void draw_ui(void)\n{"

edits = [
    ("define de transparencia",
     "#define SMALL_ALPHA 128   /* transparencia do contador na barra: 128 = 50% */\n\n", ""),
    ("pixel do texto pequeno",
     "draw_shape_alpha(framebuffer, color, SMALL_ALPHA, x + gx - x0, y + gy, 1, 1);",
     "set_pixel(framebuffer, x + gx - x0, y + gy, color);"),
    ("cor do contador da direita",
     "415, get_color(27, 54, 29));",
     "415, get_color(9, 19, 21));"),
    ("formato do tempo da esquerda",
     'snprintf(tbuf, sizeof(tbuf), "%d:%02d:%02d/%d:%02d:%02d", em, es, ecs, tmn, tsc, 0);',
     'snprintf(tbuf, sizeof(tbuf), "%d:%02d:%02d/%d:%02d", em, es, ecs, tmn, tsc);'),
    ("desenho do tempo da esquerda",
     "draw_text_prop(tbuf, UI_LEFT, 442, get_color(27, 54, 29));",
     "draw_text_tab(tbuf, UI_LEFT, 442, get_color(27, 54, 29));"),
]

errors = []
if "draw_text_tab" in raw:
    errors.append("ja parece aplicado (draw_text_tab encontrado)")
for name, old, new in edits:
    n = raw.count(nl(old))
    if n != 1:
        errors.append("trecho '%s' encontrado %d vez(es) (esperado 1) - apply_tempo_barra.py e apply_tempo_fix2.py foram aplicados?" % (name, n))
n = raw.count(nl(A_FUNC))
if n != 1:
    errors.append("ancora draw_ui encontrada %d vez(es) (esperado 1)" % n)
if errors:
    print("ABORTADO - nada foi alterado:")
    for e in errors:
        print("  -", e)
    sys.exit(1)

out = raw
for name, old, new in edits:
    out = out.replace(nl(old), nl(new), 1)
out = out.replace(nl(A_FUNC), nl(TAB) + nl(A_FUNC), 1)

for ck in ("draw_text_tab(tbuf", "SMALL_ALPHA"):
    if (ck == "SMALL_ALPHA") == (ck in out):
        sys.exit("ABORTADO - verificacao final falhou (%s); nada foi alterado." % ck)

shutil.copyfile(path, path + ".bak_tempo3")
open(path, "wb").write(out.encode("latin-1"))
print("OK: aplicado em %s (backup: %s.bak_tempo3)" % (path, path))
