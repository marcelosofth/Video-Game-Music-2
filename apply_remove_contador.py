#!/usr/bin/env python3
# apply_remove_contador.py - remove o contador MM:SS.mmm de dentro da barra
# (lado direito) e as funcoes auxiliares de texto pequeno que so ele usava.
# Uso (na raiz do repo):  python3 apply_remove_contador.py [src/libretro.c]
# Valida tudo em memoria e aborta SEM alterar nada se algo nao bater.
import sys, os, shutil

path = sys.argv[1] if len(sys.argv) > 1 else "src/libretro.c"
if not os.path.isfile(path):
    sys.exit("ERRO: arquivo nao encontrado: %s (nada foi alterado)" % path)

raw = open(path, "rb").read().decode("latin-1")
crlf = "\r\n" in raw
txt = raw.replace("\r\n", "\n")

def abort(msgs):
    print("ABORTADO - nada foi alterado:")
    for m in msgs:
        print("  -", m)
    sys.exit(1)

# ---- 1) bloco do contador dentro de draw_ui
S1 = "\n   /* tempo da musica (MM:SS.mmm) dentro da barra, alinhado a direita."
errors = []
if txt.count(S1) != 1:
    errors.append("bloco do contador encontrado %d vez(es) (esperado 1) - ja removido?" % txt.count(S1))
if txt.count("/* ---- texto pequeno (fonte 8x8") != 1:
    errors.append("funcoes auxiliares (texto pequeno) nao encontradas 1 vez")
if txt.count("/* texto 2x com DIGITOS") != 1:
    errors.append("marcador do texto de largura fixa nao encontrado 1 vez (apply_tempo_fix3.py aplicado?)")
if errors:
    abort(errors)

i1 = txt.index(S1)
j = txt.index("draw_text_small(ms_text", i1)
END = "      }\n   }\n"
k = txt.index(END, j)
e1 = k + len(END)
seg1 = txt[i1:e1]
if "ms_text" not in seg1 or "get_rate_text" in seg1 or "draw_text_prop" in seg1 or len(seg1) > 1500:
    abort(["o bloco do contador nao tem o formato esperado"])

# ---- 2) funcoes auxiliares (entre o marcador de texto pequeno e o texto fixo)
i2 = txt.index("/* ---- texto pequeno (fonte 8x8")
e2 = txt.index("/* texto 2x com DIGITOS")
seg2 = txt[i2:e2]
if i2 >= e2 or "draw_text_small" not in seg2 or "small_adv" not in seg2 \
        or "draw_ui" in seg2 or "draw_text_tab" in seg2 or len(seg2) > 2500:
    abort(["as funcoes auxiliares nao tem o formato esperado"])

# remove o bloco 1 primeiro (esta depois do bloco 2 no arquivo)
if i1 < e2:
    abort(["ordem inesperada dos trechos no arquivo"])
out = txt[:i1] + txt[e1:]
out = out[:i2] + out[e2:]

for bad in ("draw_text_small", "small_text_width", "small_adv", "ms_text"):
    if bad in out:
        abort(["verificacao final falhou (%s ainda presente)" % bad])
if "draw_text_tab(tbuf" not in out:
    abort(["verificacao final falhou (tempo da esquerda sumiu)"])

if crlf:
    out = out.replace("\n", "\r\n")
shutil.copyfile(path, path + ".bak_semcontador")
open(path, "wb").write(out.encode("latin-1"))
print("OK: contador removido em %s (backup: %s.bak_semcontador)" % (path, path))
