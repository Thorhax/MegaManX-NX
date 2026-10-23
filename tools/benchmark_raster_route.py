"""Balanced ROM-backed Magic Carpet A/B; headless simulation and raster capture.

This does not measure desktop presentation/audio delivery or certify Xbox speed.
All generated files, configuration, caches and temporary data stay under output.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import time


def process_cpu_seconds(process):
    if os.name == "nt":
        import ctypes
        from ctypes import wintypes
        times = [wintypes.FILETIME() for _ in range(4)]
        get_times = ctypes.WinDLL("kernel32", use_last_error=True).GetProcessTimes
        if not get_times(ctypes.c_void_p(int(process._handle)),
                         *(ctypes.byref(t) for t in times)):
            raise ctypes.WinError(ctypes.get_last_error())
        return sum((t.dwHighDateTime << 32) | t.dwLowDateTime for t in times[2:]) / 1e7
    return None


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ("reference", "candidate", "rom", "inputs", "output"):
        p.add_argument("--" + key, type=Path, required=True)
    p.add_argument("--pairs", type=int, default=5)
    p.add_argument("--cases", nargs="+", choices=("native", "widescreen", "ultrawide"),
                   default=["native", "widescreen", "ultrawide"])
    args = p.parse_args()
    if not 2 <= args.pairs <= 20:
        p.error("pairs must be 2..20")
    for key in ("reference", "candidate", "rom", "inputs"):
        setattr(args, key, getattr(args, key).resolve(strict=True))
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    if any(root.iterdir()):
        p.error("output must be empty to preserve earlier evidence")
    report = {"kind": "headless ROM simulation plus raster capture",
              "reference_sha256": sha(args.reference), "candidate_sha256": sha(args.candidate),
              "rom_sha256": sha(args.rom), "inputs_sha256": sha(args.inputs),
              "records": [], "summary": {}}
    expectations = {}

    def run(case, label, exe, tag):
        work = root / f"{case}-{tag}-{label}"
        work.mkdir()
        temp = work / "temp"
        temp.mkdir()
        env = {k: v for k, v in os.environ.items()
               if not k.startswith(("MMX_", "SNES_NET", "SNES_RB_", "RNET_", "SNESRECOMP_"))}
        env.update(TEMP=str(temp), TMP=str(temp), LOCALAPPDATA=str(work / "profile"),
                   APPDATA=str(work / "profile"), XDG_CACHE_HOME=str(work / "cache"),
                   XDG_DATA_HOME=str(work / "data"), SDL_AUDIODRIVER="dummy",
                   MMX_REPLAY_RECORD="0", SNESRECOMP_EXECUTION_MODE="lle",
                   SNESRECOMP_LLE_BOUNCE="1", MMX_MAGIC_CARPET_TEST=str(args.inputs))
        if case != "native":
            env["MMX_MAGIC_CARPET_WIDE"] = "32:9" if case == "ultrawide" else "1"
        start = time.perf_counter()
        process = subprocess.Popen([str(exe), str(args.rom)], cwd=work, env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            stdout, _ = process.communicate(timeout=90)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            raise
        seconds = time.perf_counter() - start
        cpu_seconds = process_cpu_seconds(process)
        (work / "run.log").write_bytes(stdout)
        text = stdout.decode(errors="replace")
        if process.returncode or "MMX MAGIC CARPET CHECKS PASSED" not in text:
            raise RuntimeError(f"{work}: failed ({process.returncode})\n{text[-3000:]}")
        files = {name: sha(work / name) for name in
                 ("magic-carpet.csv", "magic-carpet-setup.sav", "magic-carpet-active.sav")}
        if case in expectations and files != expectations[case]:
            raise RuntimeError(f"{work}: guest trace/snapshot differs from reference")
        expectations.setdefault(case, files)
        stat = re.findall(r"MMX_RASTER_STATS (\{[^\n]+\})", text)
        row = {"case": case, "label": label, "tag": tag, "seconds": seconds,
               "cpu_seconds": cpu_seconds,
               "files": files, "raster": json.loads(stat[-1]) if stat else None}
        report["records"].append(row)
        (root / "results.json").write_text(json.dumps(report, indent=2))
        print(f"{case} {tag} {label}: {seconds:.4f}s, CPU={cpu_seconds}", flush=True)
        return row

    for case in args.cases:
        run(case, "reference", args.reference, "warmup")
        run(case, "candidate", args.candidate, "warmup")
        gains, cpu_gains = [], []
        for pair in range(args.pairs):
            samples = {}
            order = (("reference", args.reference), ("candidate", args.candidate))
            if pair & 1:
                order = tuple(reversed(order))
            for label, exe in order:
                samples[label] = run(case, label, exe, f"pair{pair}")
            gains.append(100 * (samples["reference"]["seconds"] / samples["candidate"]["seconds"] - 1))
            if samples["reference"]["cpu_seconds"] and samples["candidate"]["cpu_seconds"]:
                cpu_gains.append(100 * (samples["reference"]["cpu_seconds"] / samples["candidate"]["cpu_seconds"] - 1))
        report["summary"][case] = {"paired_throughput_gain_percent": gains,
            "median_gain_percent": statistics.median(gains),
            "min_gain_percent": min(gains), "max_gain_percent": max(gains),
            "paired_cpu_gain_percent": cpu_gains,
            "median_cpu_gain_percent": statistics.median(cpu_gains) if cpu_gains else None}
    # Same-executable noise control, using the same route and process boundaries.
    aa, aa_cpu = [], []
    case = args.cases[0]
    for pair in range(2):
        a = run(case, "reference0", args.reference, f"aa{pair}")
        b = run(case, "reference1", args.reference, f"aa{pair}")
        aa.append(100 * (a["seconds"] / b["seconds"] - 1))
        if a["cpu_seconds"] and b["cpu_seconds"]:
            aa_cpu.append(100 * (a["cpu_seconds"] / b["cpu_seconds"] - 1))
    report["aa_gain_percent"] = aa
    report["aa_cpu_gain_percent"] = aa_cpu
    (root / "results.json").write_text(json.dumps(report, indent=2))
    print(json.dumps({"summary": report["summary"], "aa_gain_percent": aa}, indent=2))


if __name__ == "__main__":
    main()
