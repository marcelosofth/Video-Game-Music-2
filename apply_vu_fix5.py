#!/usr/bin/env python3
# apply_vu_fix5.py - acrescenta o "cubo" do ponteiro: meia-lua magenta embaixo
# do ponteiro, centralizada na origem (pivo) do ponteiro, nos 2 VUs.
# Uso (na raiz do repo):  python3 apply_vu_fix5.py [src/libretro.c]
# Valida tudo em memoria e aborta SEM alterar nada se algo nao bater.
import sys, os, shutil

path = sys.argv[1] if len(sys.argv) > 1 else "src/libretro.c"
if not os.path.isfile(path):
    sys.exit("ERRO: arquivo nao encontrado: %s (nada foi alterado)" % path)

raw = open(path, "rb").read().decode("latin-1")
crlf = "\r\n" in raw
nl = (lambda s: s.replace("\n", "\r\n")) if crlf else (lambda s: s)

anchor = ("                px + i + (int)(s * (VU_R - 3) + (s < 0 ? -0.5f : 0.5f)), "
          "py - (int)(c * (VU_R - 3) + 0.5f));")

hub = '''
   /* cubo do ponteiro: meia-lua magenta centralizada no pivo */
   {
      unsigned short c_hub = get_color(31, 8, 28);
      int dy, hx, half;
      for (dy = 0; dy < 6; dy++)
      {
         half = (int)sqrtf(30.25f - (float)(dy * dy));
         for (hx = px - half + 1; hx <= px + half; hx++)
            vu_px(hx, py + 2 - dy, c_hub);
      }
   }'''

errors = []
if "c_hub" in raw:
    errors.append("o cubo (c_hub) ja parece estar aplicado")
n = raw.count(nl(anchor))
if n != 1:
    errors.append("fim do desenho do ponteiro encontrado %d vez(es) (esperado 1) - apply_vu*.py anteriores foram aplicados?" % n)
if errors:
    print("ABORTADO - nada foi alterado:")
    for e in errors:
        print("  -", e)
    sys.exit(1)

out = raw.replace(nl(anchor), nl(anchor + hub), 1)
if out.count("c_hub") < 2:
    sys.exit("ABORTADO - verificacao final falhou; nada foi alterado.")

shutil.copyfile(path, path + ".bak_vufix5")
open(path, "wb").write(out.encode("latin-1"))
print("OK: cubo aplicado em %s (backup: %s.bak_vufix5)" % (path, path))
