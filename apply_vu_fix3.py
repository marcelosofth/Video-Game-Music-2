#!/usr/bin/env python3
# apply_vu_fix3.py - ajustes finos dos VU meters:
#   * letra R vai para o lado direito do VU da direita (L continua a esquerda)
#   * remove a tracinha (base) debaixo dos ponteiros
# Uso (na raiz do repo):  python3 apply_vu_fix3.py [src/libretro.c]
# Valida tudo em memoria e aborta SEM alterar nada se algo nao bater.
import sys, os, shutil

path = sys.argv[1] if len(sys.argv) > 1 else "src/libretro.c"
if not os.path.isfile(path):
    sys.exit("ERRO: arquivo nao encontrado: %s (nada foi alterado)" % path)

raw = open(path, "rb").read().decode("latin-1")
crlf = "\r\n" in raw
nl = (lambda s: s.replace("\n", "\r\n")) if crlf else (lambda s: s)

edits = [
    ("posicao do R",
     'vu_text(ch ? "R" : "L", x0 + 7, y0 + VU_BOX_H - 19, c_cyan);',
     'vu_text(ch ? "R" : "L", ch ? x0 + VU_BOX_W - 8 : x0 + 7, y0 + VU_BOX_H - 19, c_cyan);'),
    ("desenho da tracinha",
     "   draw_shape(framebuffer, c_base, px - 9, py + 4, 20, 4);\n", ""),
    ("declaracao c_base",
     "   unsigned short c_base   = get_color(2, 22, 24);\n", ""),
]

errors = []
for name, old, new in edits:
    n = raw.count(nl(old))
    if n != 1:
        errors.append("trecho '%s' encontrado %d vez(es) (esperado 1) - apply_vu*.py anteriores foram aplicados?" % (name, n))
if errors:
    print("ABORTADO - nada foi alterado:")
    for e in errors:
        print("  -", e)
    sys.exit(1)

out = raw
for name, old, new in edits:
    out = out.replace(nl(old), nl(new), 1)

if "c_base" in out:
    sys.exit("ABORTADO - verificacao final falhou (c_base ainda presente); nada foi alterado.")

shutil.copyfile(path, path + ".bak_vufix3")
open(path, "wb").write(out.encode("latin-1"))
print("OK: ajustes aplicados em %s (backup: %s.bak_vufix3)" % (path, path))
