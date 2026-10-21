import subprocess, time, statistics, sys, os
def run(cmd, cwd, n=21):
    ts = []
    for k in range(n):
        t = time.perf_counter(); r = subprocess.run(cmd, cwd=cwd, capture_output=True); ts.append(time.perf_counter() - t)
    return r.returncode, (min(ts) * 1000, statistics.median(ts) * 1000)
rc8 = os.path.abspath('../zoo/rc8.json')
for name in ('one', 'many'):
    rc, t = run(['dotnet', 'exec', '--runtimeconfig', rc8, 'main.exe'], name)
    print(f'.NET 8    {name:5s}: exit {rc}, min {t[0]:.0f} ms, median {t[1]:.0f} ms')
    rc, t = run([os.path.join(name, 'main.exe')], '.')
    print(f'NETFX 4.8 {name:5s}: exit {rc}, min {t[0]:.0f} ms, median {t[1]:.0f} ms')
