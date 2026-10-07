#!/usr/bin/env python3
# apply_vu_fix6.py - cor do cubo (meia-lua) do ponteiro:
#   VU esquerdo = azul escuro da bolinha da esquerda
#   VU direito  = roxo escuro da bolinha da direita
# Uso (na raiz do repo):  python3 apply_vu_fix6.py [src/libretro.c]
# Valida tudo em memoria e aborta SEM alterar nada se algo nao bater.
import sys, os, shutil

path = sys.argv[1] if len(sys.argv) > 1 else "src/libretro.c"
if not os.path.isfile(path):
    sys.exit("ERRO: arquivo nao encontrado: %s (nada foi alterado)" % path)

raw = open(path, "rb").read().decode("latin-1")
crlf = "\r\n" in raw
nl = (lambda s: s.replace("\n", "\r\n")) if crlf else (lambda s: s)

old = "unsigned short c_hub = get_color(31, 8, 28);"
new = "unsigned short c_hub = ch ? get_color(3, 0, 4) : get_color(0, 4, 5); /* R: roxo, L: azul */"

n = raw.count(nl(old))
if n != 1:
    print("ABORTADO - nada foi alterado:")
    print("  - cor do cubo encontrada %d vez(es) (esperado 1) - o apply_vu_fix5.py foi aplicado?" % n)
    sys.exit(1)

out = raw.replace(nl(old), nl(new), 1)
shutil.copyfile(path, path + ".bak_vufix6")
open(path, "wb").write(out.encode("latin-1"))
print("OK: cor aplicada em %s (backup: %s.bak_vufix6)" % (path, path))
