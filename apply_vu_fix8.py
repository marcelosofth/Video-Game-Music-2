#!/usr/bin/env python3
# apply_vu_fix8.py - desce os 2 VU meters 6 px (VU_CY 246 -> 252) para a base
# do ponteiro (a meia-lua) encostar na linha do horizonte do fundo.
# Uso (na raiz do repo):  python3 apply_vu_fix8.py [src/libretro.c]
# Valida tudo em memoria e aborta SEM alterar nada se algo nao bater.
import sys, os, shutil

path = sys.argv[1] if len(sys.argv) > 1 else "src/libretro.c"
if not os.path.isfile(path):
    sys.exit("ERRO: arquivo nao encontrado: %s (nada foi alterado)" % path)

raw = open(path, "rb").read().decode("latin-1")
crlf = "\r\n" in raw
nl = (lambda s: s.replace("\n", "\r\n")) if crlf else (lambda s: s)

old = "#define VU_CY      246"
new = "#define VU_CY      252"

n = raw.count(nl(old))
if n != 1:
    print("ABORTADO - nada foi alterado:")
    print("  - '#define VU_CY      246' encontrado %d vez(es) (esperado 1) - apply_vu_fix.py aplicado? ja rodou este?" % n)
    sys.exit(1)

out = raw.replace(nl(old), nl(new), 1)
shutil.copyfile(path, path + ".bak_vufix8")
open(path, "wb").write(out.encode("latin-1"))
print("OK: VUs descidos em %s (backup: %s.bak_vufix8)" % (path, path))
