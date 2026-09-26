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
    problems = []
    for path in sys.argv[1:] or ['.github/workflows/ci.yml',
                                    '.github/workflows/release.yml']:
        doc = yaml.safe_load(open(path))
        for nome_job, job in doc['jobs'].items():
            for step in job.get('steps', []):
                script = step.get('run')
                if not script:
                    continue
                # Only the PowerShell blocks: the bash ones have other rules.
                if 'Write-Host' not in script and 'Test-Path' not in script:
                    continue
                tag = f"{path}:{nome_job}:{step.get('name', '(sem name)')}"
                lines = [l for l in script.split('\n')
                          if not l.strip().startswith('#')]
                body = '\n'.join(lines)
                for openAt, closeAt, kind in (('{', '}', 'chaves'),
                                          ('(', ')', 'parêntesis')):
                    if body.count(openAt) != body.count(closeAt):
                        problems.append(
                            f"{tag}: {kind} desequilibrados "
                            f"({body.count(openAt)} vs {body.count(closeAt)})")
                for l in lines:
                    if l.count('"') % 2:
                        problems.append(
                            f"{tag}: aspas ímpares em: {l.strip()[:70]}")
                for m in re.finditer(r'\$[A-Za-z_][A-Za-z0-9_]*:', body):
                    if not m.group(0).startswith('$env:'):
                        problems.append(
                            f"{tag}: {m.group(0)} é lido como qualificador "
                            f"de âmbito; usa ${{...}}")
    if problems:
        print('\n'.join(problems))
        return 1
    print('Workflows: PowerShell equilibrado e sem qualificadores por engano.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
