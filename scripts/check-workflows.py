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
        for job_name, job in doc['jobs'].items():
            for step in job.get('steps', []):
                script = step.get('run')
                if not script:
                    continue
                # Only the PowerShell blocks: the bash ones have other rules.
                if 'Write-Host' not in script and 'Test-Path' not in script:
                    continue
                tag = f"{path}:{job_name}:{step.get('name', '(no name)')}"
                lines = [l for l in script.split('\n')
                          if not l.strip().startswith('#')]
                body = '\n'.join(lines)
                for opening, closing, kind in (('{', '}', 'braces'),
                                          ('(', ')', 'parentheses')):
                    if body.count(opening) != body.count(closing):
                        problems.append(
                            f"{tag}: unbalanced {kind} "
                            f"({body.count(opening)} vs {body.count(closing)})")
                for l in lines:
                    if l.count('"') % 2:
                        problems.append(
                            f"{tag}: odd number of quotes in: {l.strip()[:70]}")
                for m in re.finditer(r'\$[A-Za-z_][A-Za-z0-9_]*:', body):
                    if not m.group(0).startswith('$env:'):
                        problems.append(
                            f"{tag}: {m.group(0)} is read as a scope "
                            f"qualifier; use ${{...}}")
    if problems:
        print('\n'.join(problems))
        return 1
    print('Workflows: PowerShell balanced and with no accidental qualifiers.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
