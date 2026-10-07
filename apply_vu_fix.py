#!/usr/bin/env python3
# apply_vu_fix.py - ajusta os VU meters ja aplicados por apply_vu.py:
#   * remove o quadrado (contorno) e o fundo do painel
#   * sobe 4 px para a base ("tracinha") alinhar com a linha de fundo
#   * VU da direita espelhado com o da esquerda (centro 550)
# Uso (na raiz do repo):  python3 apply_vu_fix.py [src/libretro.c]
# Valida tudo em memoria e aborta SEM alterar nada se algo nao bater.
import sys, os, shutil

path = sys.argv[1] if len(sys.argv) > 1 else "src/libretro.c"
if not os.path.isfile(path):
    sys.exit("ERRO: arquivo nao encontrado: %s (nada foi alterado)" % path)

raw = open(path, "rb").read().decode("latin-1")
crlf = "\r\n" in raw
nl = (lambda s: s.replace("\n", "\r\n")) if crlf else (lambda s: s)

# (nome, texto antigo, texto novo)
edits = [
    ("VU_R_CX", "#define VU_R_CX    536", "#define VU_R_CX    550"),
    ("VU_CY", "#define VU_CY      250", "#define VU_CY      246"),
    ("decl painel", "   unsigned short c_panel  = get_color(1, 2, 3);\n", ""),
    ("decl borda", "   unsigned short c_border = get_color(4, 12, 14);\n", ""),
    ("desenho painel/borda",
     "   draw_shape_alpha(framebuffer, c_panel, 150, x0, y0, VU_BOX_W, VU_BOX_H);\n"
     "   draw_box(framebuffer, c_border, x0, y0, x0 + VU_BOX_W - 1, y0 + VU_BOX_H - 1);\n\n", ""),
]

errors = []
for name, old, new in edits:
    n = raw.count(nl(old))
    if n != 1:
        errors.append("trecho '%s' encontrado %d vez(es) (esperado 1) - o apply_vu.py foi aplicado?" % (name, n))
if errors:
    print("ABORTADO - nada foi alterado:")
    for e in errors:
        print("  -", e)
    sys.exit(1)

out = raw
for name, old, new in edits:
    out = out.replace(nl(old), nl(new), 1)

for bad in ("c_panel", "c_border"):
    if bad in out:
        sys.exit("ABORTADO - verificacao final falhou (%s ainda presente); nada foi alterado." % bad)

shutil.copyfile(path, path + ".bak_vufix")
open(path, "wb").write(out.encode("latin-1"))
print("OK: ajustes aplicados em %s (backup: %s.bak_vufix)" % (path, path))
