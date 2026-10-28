#!/usr/bin/env python3
"""Gera src/skills_seed.h a partir de skills/<nome>/SKILL.md.

As skills embutidas sao escritas em <core>/skills na primeira execucao (e so'
se faltarem), para o Delta nascer com elas no ambiente e o usuario poder ler,
editar ou apagar. Rode com `make skills` depois de editar qualquer SKILL.md.
"""
import io
import json
import os
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PASTA = os.path.join(RAIZ, "skills")


def main():
    nomes = sorted(
        n for n in os.listdir(PASTA)
        if os.path.isfile(os.path.join(PASTA, n, "SKILL.md"))
    )
    if not nomes:
        sys.exit("nenhum SKILL.md em skills/")

    saida = io.StringIO()
    saida.write("/* skills_seed.h - GERADO por tools/embed-skills.py. Nao edite.\n")
    saida.write(" * Texto das skills embutidas; delta_bootstrap() grava o que faltar em\n")
    saida.write(" * <core>/skills/<nome>/SKILL.md.                   */\n")
    saida.write("#ifndef DELTA_SKILLS_SEED_H\n#define DELTA_SKILLS_SEED_H\n\n")
    saida.write("typedef struct { const char *name; const char *body; } SkillSeed;\n\n")
    saida.write("static const SkillSeed SKILLS_SEED[] = {\n")
    for nome in nomes:
        caminho = os.path.join(PASTA, nome, "SKILL.md")
        texto = io.open(caminho, encoding="utf-8").read().rstrip("\n") + "\n"
        # string C: escapa o que quebra o literal e evita linha gigante
        linhas = texto.split("\n")
        saida.write("    { %s,\n" % json.dumps(nome))
        for i, linha in enumerate(linhas):
            if not linha and i == len(linhas) - 1:
                continue
            lit = linha.replace("\\", "\\\\").replace('"', '\\"')
            fim = "\\n\"" if i < len(linhas) - 2 else "\""
            if i == len(linhas) - 1:
                saida.write("      \"%s\\n\"\n" % lit)
            else:
                saida.write("      \"%s\\n\"\n" % lit)
        saida.write("    },\n")
    saida.write("    { 0, 0 }\n};\n\n")
    saida.write("#endif /* DELTA_SKILLS_SEED_H */\n")
    sys.stdout.write(saida.getvalue())


if __name__ == "__main__":
    main()
