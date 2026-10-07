#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_car_runner.py

Adiciona o carro (Namco Rave War / Galaxian3) como um 4o "corredor" que
cruza a tela na linha do horizonte, igual Arthur/Mario/Sonic.

Precisa que car_run.h (gerado junto, mesma pasta ou --car-header) esteja
copiado para src/ junto com os outros *_run.h.

Uso:
    python apply_car_runner.py --root "G:\\Internet_Temp\\Batocera_add\\GME2\\gme2"
"""
import argparse
import os
import sys

PATCHES = [
    ("include car_run.h",
     '#include "sonic_run.h"',
     '#include "sonic_run.h"\n#include "car_run.h"'),

    ("XEV_RUNNER_COUNT 3 -> 4",
     '#define XEV_RUNNER_COUNT    3            /* corredores: Arthur, Mario, Sonic (revezam a cada ciclo) */',
     '#define XEV_RUNNER_COUNT    4            /* corredores: Arthur, Mario, Sonic, Carro (revezam a cada ciclo) */'),

    ("novas constantes do carro",
     '#define XEV_RUN_SPEED_SONIC 2            /* px por frame do Sonic (o Sonic corre mais rapido que os outros) */',
     '#define XEV_RUN_SPEED_SONIC 2            /* px por frame do Sonic (o Sonic corre mais rapido que os outros) */\n'
     '#define XEV_CAR_SCALE_PCT   45           /* tamanho do carro cruzando a tela, em % do sprite original (202x64) */\n'
     '#define XEV_RUN_SPEED_CAR   6            /* px por frame do carro (bem mais rapido que os corredores) */'),

    ("comentario do ciclo (intro do arquivo)",
     ' *  - depois (XEV_RUN_START), um corredor (Arthur, Mario ou Sonic) corre de uma ponta a outra da tela, vindo da esquerda ou da',
     ' *  - depois (XEV_RUN_START), um corredor (Arthur, Mario, Sonic ou o carro) corre de uma ponta a outra da tela, vindo da esquerda ou da'),

    ("switch rw/rh/rfeet/rms/rframes",
     '      switch (who)\n'
     '      {\n'
     '         case 1:  rw = MARIO_RUN_W;  rh = MARIO_RUN_H;  rfeet = MARIO_RUN_FEET;  rms = MARIO_RUN_MS;\n'
     '                  rframes = MARIO_RUN_FRAMES;  break;\n'
     '         case 2:  rw = SONIC_RUN_W;  rh = SONIC_RUN_H;  rfeet = SONIC_RUN_FEET;  rms = SONIC_RUN_MS;\n'
     '                  rframes = SONIC_RUN_FRAMES;  break;\n'
     '         default: rw = ARTHUR_RUN_W; rh = ARTHUR_RUN_H; rfeet = ARTHUR_RUN_FEET; rms = ARTHUR_RUN_MS;\n'
     '                  rframes = ARTHUR_RUN_FRAMES; break;\n'
     '      }',
     '      switch (who)\n'
     '      {\n'
     '         case 1:  rw = MARIO_RUN_W;  rh = MARIO_RUN_H;  rfeet = MARIO_RUN_FEET;  rms = MARIO_RUN_MS;\n'
     '                  rframes = MARIO_RUN_FRAMES;  break;\n'
     '         case 2:  rw = SONIC_RUN_W;  rh = SONIC_RUN_H;  rfeet = SONIC_RUN_FEET;  rms = SONIC_RUN_MS;\n'
     '                  rframes = SONIC_RUN_FRAMES;  break;\n'
     '         case 3:  rw = CAR_RUN_W;   rh = CAR_RUN_H;   rfeet = CAR_RUN_FEET;   rms = CAR_RUN_MS;\n'
     '                  rframes = CAR_RUN_FRAMES;  break;\n'
     '         default: rw = ARTHUR_RUN_W; rh = ARTHUR_RUN_H; rfeet = ARTHUR_RUN_FEET; rms = ARTHUR_RUN_MS;\n'
     '                  rframes = ARTHUR_RUN_FRAMES; break;\n'
     '      }'),

    ("comentario do corredor (dentro da funcao)",
     '   /* corredor (Arthur, Mario ou Sonic) na linha do horizonte, vindo da esquerda ou da direita */',
     '   /* corredor (Arthur, Mario, Sonic ou o carro) na linha do horizonte, vindo da esquerda ou da direita */'),

    ("switch pix do frame",
     '      switch (who)\n'
     '      {\n'
     '         case 1:  pix = mario_run_rgb[f];  break;\n'
     '         case 2:  pix = sonic_run_rgb[f];  break;\n'
     '         default: pix = arthur_run_rgb[f]; break;\n'
     '      }',
     '      switch (who)\n'
     '      {\n'
     '         case 1:  pix = mario_run_rgb[f];  break;\n'
     '         case 2:  pix = sonic_run_rgb[f];  break;\n'
     '         case 3:  pix = car_run_rgb[f];    break;\n'
     '         default: pix = arthur_run_rgb[f]; break;\n'
     '      }'),

    ("escala (pct) e velocidade (spd)",
     '      if (who == 0)\n'
     '         pct = XEV_ARTHUR_SCALE_PCT * ARTHUR_RUN_H / rh;        /* Arthur */\n'
     '      else\n'
     '         pct = XEV_RUN_SCALE_PCT * ARTHUR_RUN_H / rh * XEV_RUN_SMALL_PCT / 100;   /* Mario e Sonic: mesmo tamanho de antes */\n'
     '      spd = (who == 2) ? XEV_RUN_SPEED_SONIC : XEV_RUN_SPEED;   /* Sonic mais rapido */',
     '      if (who == 0)\n'
     '         pct = XEV_ARTHUR_SCALE_PCT * ARTHUR_RUN_H / rh;        /* Arthur */\n'
     '      else if (who == 3)\n'
     '         pct = XEV_CAR_SCALE_PCT;                                /* Carro: escala propria, nao relativa ao Arthur */\n'
     '      else\n'
     '         pct = XEV_RUN_SCALE_PCT * ARTHUR_RUN_H / rh * XEV_RUN_SMALL_PCT / 100;   /* Mario e Sonic: mesmo tamanho de antes */\n'
     '      spd = (who == 2) ? XEV_RUN_SPEED_SONIC : (who == 3) ? XEV_RUN_SPEED_CAR : XEV_RUN_SPEED;   /* Sonic/carro tem velocidade propria */'),

    ("run_last_side: 4 valores -1 (era so 3)",
     'static int run_last_side[XEV_RUNNER_COUNT] = { -1, -1, -1 };',
     'static int run_last_side[XEV_RUNNER_COUNT] = { -1, -1, -1, -1 };'),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True, help="Pasta raiz do repo (contem src/libretro.c)")
    ap.add_argument("--file", default=os.path.join("src", "libretro.c"))
    args = ap.parse_args()

    path = os.path.join(args.root, args.file)
    if not os.path.isfile(path):
        print(f"ERRO: arquivo nao encontrado: {path}")
        sys.exit(1)

    with open(path, "r", encoding="utf-8", newline=None) as f:
        src = f.read()

    for name, old, new in PATCHES:
        count = src.count(old)
        if count == 0:
            print(f"ERRO: ancora nao encontrada ({name}). Arquivo pode ja estar patchado ou mudou por fora.")
            sys.exit(1)
        if count > 1:
            print(f"ERRO: ancora '{name}' aparece {count} vezes (esperado 1). Abortando por seguranca.")
            sys.exit(1)
        src = src.replace(old, new)
        print(f"OK: aplicado -> {name}")

    with open(path, "w", encoding="utf-8", newline="\r\n") as f:
        f.write(src)

    print(f"\nPatch aplicado com sucesso em: {path}")
    print("Lembre-se de copiar car_run.h para a pasta src/ antes de compilar.")


if __name__ == "__main__":
    main()
