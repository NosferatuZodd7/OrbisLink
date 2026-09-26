#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Rough check of the workflows' PowerShell blocks: balanced braces,
# parentheses and quotes, and no "$var:" — which PowerShell reads
# as a scope qualifier.
#
# It does not replace an interpreter; it catches the class of error that is
# only found five minutes later, on a Windows runner.
import re
import sys

import yaml


def main() -> int:
    problemas = []
    for caminho in sys.argv[1:] or ['.github/workflows/ci.yml',
                                    '.github/workflows/release.yml']:
        doc = yaml.safe_load(open(caminho))
        for nome_job, job in doc['jobs'].items():
            for passo in job.get('steps', []):
                script = passo.get('run')
                if not script:
                    continue
                # Only the PowerShell blocks: the bash ones have other rules.
                if 'Write-Host' not in script and 'Test-Path' not in script:
                    continue
                etiqueta = f"{caminho}:{nome_job}:{passo.get('name', '(sem nome)')}"
                linhas = [l for l in script.split('\n')
                          if not l.strip().startswith('#')]
                corpo = '\n'.join(linhas)
                for abre, fecha, tipo in (('{', '}', 'chaves'),
                                          ('(', ')', 'parêntesis')):
                    if corpo.count(abre) != corpo.count(fecha):
                        problemas.append(
                            f"{etiqueta}: {tipo} desequilibrados "
                            f"({corpo.count(abre)} vs {corpo.count(fecha)})")
                for l in linhas:
                    if l.count('"') % 2:
                        problemas.append(
                            f"{etiqueta}: aspas ímpares em: {l.strip()[:70]}")
                for m in re.finditer(r'\$[A-Za-z_][A-Za-z0-9_]*:', corpo):
                    if not m.group(0).startswith('$env:'):
                        problemas.append(
                            f"{etiqueta}: {m.group(0)} é lido como qualificador "
                            f"de âmbito; usa ${{...}}")
    if problemas:
        print('\n'.join(problemas))
        return 1
    print('Workflows: PowerShell equilibrado e sem qualificadores por engano.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
