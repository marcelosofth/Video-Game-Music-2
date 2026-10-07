#!/usr/bin/env python3
# apply_vu_fix7.py - clareia a cor do cubo (meia-lua) do ponteiro:
#   VU esquerdo = azul mais claro, VU direito = roxo mais claro
# Uso (na raiz do repo):  python3 apply_vu_fix7.py [src/libretro.c]
# Valida tudo em memoria e aborta SEM alterar nada se algo nao bater.
import sys, os, shutil

path = sys.argv[1] if len(sys.argv) > 1 else "src/libretro.c"
if not os.path.isfile(path):
    sys.exit("ERRO: arquivo nao encontrado: %s (nada foi alterado)" % path)

raw = open(path, "rb").read().decode("latin-1")
crlf = "\r\n" in raw
nl = (lambda s: s.replace("\n", "\r\n")) if crlf else (lambda s: s)

old = "unsigned short c_hub = ch ? get_color(3, 0, 4) : get_color(0, 4, 5); /* R: roxo, L: azul */"
new = "unsigned short c_hub = ch ? get_color(17, 10, 28) : get_color(6, 26, 30); /* R: roxo, L: azul */"

n = raw.count(nl(old))
if n != 1:
    print("ABORTADO - nada foi alterado:")
    print("  - cor do cubo encontrada %d vez(es) (esperado 1) - o apply_vu_fix6.py foi aplicado?" % n)
    sys.exit(1)

out = raw.replace(nl(old), nl(new), 1)
shutil.copyfile(path, path + ".bak_vufix7")
open(path, "wb").write(out.encode("latin-1"))
print("OK: cor aplicada em %s (backup: %s.bak_vufix7)" % (path, path))
