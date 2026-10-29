"""How do concurrent wsl.exe launches scale? Start N at once, K rounds each; report wall per launch."""
import subprocess, sys, time, statistics
from concurrent.futures import ThreadPoolExecutor
def one():
    t = time.perf_counter()
    subprocess.run(["wsl.exe", "-d", "Ubuntu", "--exec", "true"], check=True)
    return time.perf_counter() - t
for n in (1, 2, 4, 8):
    rounds = 3
    t0 = time.perf_counter()
    with ThreadPoolExecutor(n) as ex:
        lat = list(ex.map(lambda _: one(), range(n * rounds)))
    wall = time.perf_counter() - t0
    print(f"concurrency {n}: {n*rounds} launches in {wall:.2f} s, {wall/(n*rounds):.3f} s per launch, latency median {statistics.median(lat):.2f} s max {max(lat):.2f} s")
