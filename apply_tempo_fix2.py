#!/usr/bin/env python3
# apply_tempo_fix2.py
#   * contador da direita (dentro da barra) com 50% de transparencia
#   * tempo da esquerda passa de 00:00/00:00 para 0:00:00/0:00:00
#     (M:SS:CC - minuto com 1 digito, segundos e centesimos; com mais de 9
#      minutos o digito extra empurra o resto para a direita, a esquerda
#      continua alinhada)
# Uso (na raiz do repo):  python3 apply_tempo_fix2.py [src/libretro.c]
# Valida tudo em memoria e aborta SEM alterar nada se algo nao bater.
import sys, os, shutil

path = sys.argv[1] if len(sys.argv) > 1 else "src/libretro.c"
if not os.path.isfile(path):
    sys.exit("ERRO: arquivo nao encontrado: %s (nada foi alterado)" % path)

raw = open(path, "rb").read().decode("latin-1")
crlf = "\r\n" in raw
nl = (lambda s: s.replace("\n", "\r\n")) if crlf else (lambda s: s)

LEFT_NEW = '''   {
      int em = 0, es = 0, tmn = 0, tsc = 0, ecs = 0;
      int fr = get_track_elapsed_frames();
      char tbuf[64];
      get_time_text(message);
      if (sscanf(message, "%d:%d/%d:%d", &em, &es, &tmn, &tsc) == 4)
      {
         if (fr / 60 == em * 60 + es)
            ecs = (fr % 60) * 100 / 60;
         snprintf(tbuf, sizeof(tbuf), "%d:%02d:%02d/%d:%02d:%02d", em, es, ecs, tmn, tsc, 0);
         draw_text_prop(tbuf, UI_LEFT, 442, get_color(27, 54, 29));
      }
      else
         draw_text_prop(message, UI_LEFT, 442, get_color(27, 54, 29));
   }'''

edits = [
    ("define de transparencia",
     "static int small_adv(char ch)\n",
     "#define SMALL_ALPHA 128   /* transparencia do contador na barra: 128 = 50% */\n\nstatic int small_adv(char ch)\n"),
    ("pixel do texto pequeno",
     "set_pixel(framebuffer, x + gx - x0, y + gy, color);",
     "draw_shape_alpha(framebuffer, color, SMALL_ALPHA, x + gx - x0, y + gy, 1, 1);"),
    ("tempo da esquerda",
     "   draw_text_prop(get_time_text(message), UI_LEFT, 442, get_color(27, 54, 29));",
     LEFT_NEW),
]

errors = []
if "SMALL_ALPHA" in raw:
    errors.append("ja parece aplicado (SMALL_ALPHA encontrado)")
for name, old, new in edits:
    n = raw.count(nl(old))
    if n != 1:
        errors.append("trecho '%s' encontrado %d vez(es) (esperado 1) - apply_tempo_barra.py foi aplicado?" % (name, n))
if errors:
    print("ABORTADO - nada foi alterado:")
    for e in errors:
        print("  -", e)
    sys.exit(1)

out = raw
for name, old, new in edits:
    out = out.replace(nl(old), nl(new), 1)

for ck in ("draw_shape_alpha(framebuffer, color, SMALL_ALPHA", "%d:%02d:%02d/%d:%02d:%02d"):
    if ck not in out:
        sys.exit("ABORTADO - verificacao final falhou (%s); nada foi alterado." % ck)

shutil.copyfile(path, path + ".bak_tempo2")
open(path, "wb").write(out.encode("latin-1"))
print("OK: aplicado em %s (backup: %s.bak_tempo2)" % (path, path))
