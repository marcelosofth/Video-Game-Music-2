#!/usr/bin/env python3
# apply_vu_fix4.py - o ponteiro passa a nascer exatamente no pivo (sem descer
# abaixo dele), para nao tocar/cortar a linha do horizonte do fundo quando
# esta muito para a esquerda ou para a direita.
# Uso (na raiz do repo):  python3 apply_vu_fix4.py [src/libretro.c]
# Valida tudo em memoria e aborta SEM alterar nada se algo nao bater.
import sys, os, shutil

path = sys.argv[1] if len(sys.argv) > 1 else "src/libretro.c"
if not os.path.isfile(path):
    sys.exit("ERRO: arquivo nao encontrado: %s (nada foi alterado)" % path)

raw = open(path, "rb").read().decode("latin-1")
crlf = "\r\n" in raw
nl = (lambda s: s.replace("\n", "\r\n")) if crlf else (lambda s: s)

old = "px + i - (int)(s * 5 + (s < 0 ? -0.5f : 0.5f)), py + (int)(c * 5 + 0.5f),"
new = "px + i, py,"

n = raw.count(nl(old))
if n != 1:
    print("ABORTADO - nada foi alterado:")
    print("  - inicio do ponteiro encontrado %d vez(es) (esperado 1) - apply_vu*.py anteriores foram aplicados?" % n)
    sys.exit(1)

out = raw.replace(nl(old), nl(new), 1)
shutil.copyfile(path, path + ".bak_vufix4")
open(path, "wb").write(out.encode("latin-1"))
print("OK: ajuste aplicado em %s (backup: %s.bak_vufix4)" % (path, path))
