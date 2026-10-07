#!/usr/bin/env python3
# apply_vu_fix9.py - nos 2 VUs, quando o ponteiro aponta para um numero da
# escala, esse numero fica preto; ao sair da direcao ele volta a cor original.
# Uso (na raiz do repo):  python3 apply_vu_fix9.py [src/libretro.c]
# Valida tudo em memoria e aborta SEM alterar nada se algo nao bater.
# Sensibilidade: VU_HIT_TOL (0.04 = ~4,4 graus de cada lado do numero).
import sys, os, shutil

path = sys.argv[1] if len(sys.argv) > 1 else "src/libretro.c"
if not os.path.isfile(path):
    sys.exit("ERRO: arquivo nao encontrado: %s (nada foi alterado)" % path)

raw = open(path, "rb").read().decode("latin-1")
crlf = "\r\n" in raw
nl = (lambda s: s.replace("\n", "\r\n")) if crlf else (lambda s: s)

edits = [
    ("declaracoes",
     "   float a, rad, s, c;\n",
     "   float a, rad, s, c, bestd = 1.0f;\n   int hit = -1;\n"),
    ("calculo do numero apontado",
     "   /* marcas e numeros */\n",
     "   /* numero para o qual o ponteiro aponta (fica preto) */\n"
     "   for (i = 0; i < 9; i++)\n"
     "   {\n"
     "      float d = pos - vu_t_tab[i];\n"
     "      if (d < 0.0f) d = -d;\n"
     "      if (d <= VU_HIT_TOL && d < bestd)\n"
     "      {\n"
     "         bestd = d;\n"
     "         hit   = i;\n"
     "      }\n"
     "   }\n\n"
     "   /* marcas e numeros */\n"),
    ("cor do numero",
     "py - (int)(c * (VU_R + 11) + 0.5f) - 2, col);",
     "py - (int)(c * (VU_R + 11) + 0.5f) - 2,\n"
     "              i == hit ? get_color(0, 0, 0) : col);"),
    ("constante",
     "#define VU_REF_DB  14.0f\n",
     "#define VU_REF_DB  14.0f\n#define VU_HIT_TOL 0.04f   /* tolerancia (na escala 0..1) para o numero ficar preto */\n"),
]

errors = []
if "VU_HIT_TOL" in raw:
    errors.append("ja parece aplicado (VU_HIT_TOL encontrado)")
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

shutil.copyfile(path, path + ".bak_vufix9")
open(path, "wb").write(out.encode("latin-1"))
print("OK: numeros pretos aplicados em %s (backup: %s.bak_vufix9)" % (path, path))
