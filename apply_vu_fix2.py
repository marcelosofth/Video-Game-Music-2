#!/usr/bin/env python3
# apply_vu_fix2.py - ajustes finos dos VU meters:
#   * letras L e R sobem (saem de cima da linha do fundo)
#   * ponteiro passa a nascer dentro da "tracinha" (base), encostando nela
# Uso (na raiz do repo):  python3 apply_vu_fix2.py [src/libretro.c]
# Valida tudo em memoria e aborta SEM alterar nada se algo nao bater.
import sys, os, shutil

path = sys.argv[1] if len(sys.argv) > 1 else "src/libretro.c"
if not os.path.isfile(path):
    sys.exit("ERRO: arquivo nao encontrado: %s (nada foi alterado)" % path)

raw = open(path, "rb").read().decode("latin-1")
crlf = "\r\n" in raw
nl = (lambda s: s.replace("\n", "\r\n")) if crlf else (lambda s: s)

edits = [
    ("posicao do L/R",
     'vu_text(ch ? "R" : "L", x0 + 7, y0 + VU_BOX_H - 8, c_cyan);',
     'vu_text(ch ? "R" : "L", x0 + 7, y0 + VU_BOX_H - 19, c_cyan);'),
    ("inicio do ponteiro",
     "px + i + (int)(s * 2 + (s < 0 ? -0.5f : 0.5f)), py - (int)(c * 2 + 0.5f),",
     "px + i - (int)(s * 5 + (s < 0 ? -0.5f : 0.5f)), py + (int)(c * 5 + 0.5f),"),
]

errors = []
for name, old, new in edits:
    n = raw.count(nl(old))
    if n != 1:
        errors.append("trecho '%s' encontrado %d vez(es) (esperado 1) - apply_vu.py/apply_vu_fix.py aplicados?" % (name, n))
if errors:
    print("ABORTADO - nada foi alterado:")
    for e in errors:
        print("  -", e)
    sys.exit(1)

out = raw
for name, old, new in edits:
    out = out.replace(nl(old), nl(new), 1)

shutil.copyfile(path, path + ".bak_vufix2")
open(path, "wb").write(out.encode("latin-1"))
print("OK: ajustes aplicados em %s (backup: %s.bak_vufix2)" % (path, path))
