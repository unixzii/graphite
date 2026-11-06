"""Compare existing macOS demos; arguments: C executable, Rust executable, output directory."""
import csv
from pathlib import Path
import re
import subprocess
import sys

executables = dict(zip(('c', 'rust'), sys.argv[1:3]))
out = Path(sys.argv[3]).resolve()
out.mkdir(parents=True, exist_ok=True)
rows = []
for scene in ('showcase', 'ui'):
    for mode in ('snapshot', 'selftest'):
        for repeat in range(5):
            for language in (('c', 'rust') if repeat % 2 == 0 else ('rust', 'c')):
                args = ['--snapshot', str(out / f'{language}-{scene}.png')] if mode == 'snapshot' else ['--selftest']
                result = subprocess.run(['/usr/bin/time', '-lp', executables[language], '--scene', scene, *args], capture_output=True, text=True, check=False)
                (out / f'{language}-{scene}-{mode}-{repeat}.log').write_text(result.stdout + result.stderr)
                times = {k: float(v) for k, v in re.findall(r'^(real|user|sys)\s+(\d+(?:\.\d+)?)', result.stderr, re.M)}
                rss = int(re.search(r'(\d+)\s+maximum resident set size', result.stderr)[1])
                rows.append([language, scene, mode, repeat, times['real'], times['user'], times['sys'], rss, result.returncode])
                print(language, scene, mode, repeat, flush=True)
with (out / 'native.csv').open('w') as f:
    writer = csv.writer(f)
    writer.writerow(['language', 'scene', 'mode', 'repeat', 'wall_s', 'user_s', 'system_s', 'peak_rss_bytes', 'exit_code'])
    writer.writerows(rows)
