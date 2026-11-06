"""macOS core benchmark; all build output stays in a caller-selected directory."""
import csv
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)


def run(args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)


sources = sorted((root / 'src/core').glob('*.c'))
for metrics in (False, True):
    suffix = 'metrics' if metrics else 'time'
    flags = ['-O3', '-DNDEBUG', '-flto', '-I' + str(root / 'include')]
    objects = []
    for source in [*sources, root / 'benchmarks/core.c']:
        obj = out / (source.stem + '-' + suffix + '.o')
        extra = ['-DMETRICS', '-include', root / 'benchmarks/alloc.h'] if metrics else []
        run(['clang', *flags, *extra, '-c', source, '-o', obj])
        objects.append(obj)
    run(['clang', *flags, *objects, *([root / 'benchmarks/alloc.c'] if metrics else []), '-o', out / suffix])

rows = []
for n in (1000, 10000):
    for changed in (0, 1, n // 100, n):
        rounds = 1000 if changed == n else 10000
        for repeat in range(7):
            for mode in ('time', 'metrics') if repeat == 0 else ('time',):
                values = run([out / mode, n, changed, rounds], capture_output=True, text=True).stdout.strip().split(',')
                rows.append([mode, repeat, *values])
                print(mode, repeat, n, changed, flush=True)
with (out / 'core.csv').open('w') as f:
    writer = csv.writer(f)
    writer.writerow(['mode', 'repeat', 'layers', 'changed', 'rounds', 'create_ns', 'update_ns', 'live_bytes', 'allocations', 'remaining_bytes'])
    writer.writerows(rows)
