"""MMX stage attribution with exact state/pixel checks; no speedup assertions.

The C harness reports wall-time stages, including preemption. Process CPU time
is recorded separately. This is a headless source-frame/compositor probe, not
desktop presentation or audio-delivery qualification.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess

from benchmark_raster_route import process_cpu_seconds


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("exe", "rom", "highway", "storm", "carpet", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--runs", type=int, choices=range(1, 5), default=2)
    args = parser.parse_args()
    for name in ("exe", "rom", "highway", "storm", "carpet"):
        setattr(args, name, getattr(args, name).resolve(strict=True))
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    if any(root.iterdir()):
        parser.error("output must be empty; preserve earlier evidence")
    report = {"kind": "headless MMX stage attribution",
              "inputs": {name: {"path": str(getattr(args, name)),
                                "sha256": sha(getattr(args, name))}
                         for name in ("exe", "rom", "highway", "storm", "carpet")},
              "runs": []}
    expected_pixels = None
    for run in range(args.runs):
        work = root / f"run{run}"
        work.mkdir()
        (work / "temp").mkdir()
        env = {k: v for k, v in os.environ.items() if not k.startswith(
            ("MMX_", "SNESRECOMP_", "SNES_NET", "SNES_RB_", "RNET_"))}
        env.update(TEMP=str(work / "temp"), TMP=str(work / "temp"),
            LOCALAPPDATA=str(work / "profile"), APPDATA=str(work / "profile"),
            XDG_CACHE_HOME=str(work / "cache"), XDG_DATA_HOME=str(work / "data"),
            SDL_AUDIODRIVER="dummy", MMX_REPLAY_RECORD="0",
            SNESRECOMP_LLE_BOUNCE="1", MMX_FRAME_COST_TEST="1",
            MMX_FRAME_COST_HIGHWAY=str(args.highway),
            MMX_FRAME_COST_STORM=str(args.storm),
            MMX_FRAME_COST_CARPET=str(args.carpet))
        process = subprocess.Popen([str(args.exe), str(args.rom)], cwd=work,
            env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            stdout, _ = process.communicate(timeout=180)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            raise
        (work / "run.log").write_bytes(stdout)
        if process.returncode or b"MMX FRAME COST CHECKS PASSED" not in stdout:
            raise RuntimeError(f"{work}: failed ({process.returncode})\n"
                               + stdout.decode(errors="replace")[-4000:])
        with (work / "frame-costs.csv").open(newline="") as stream:
            rows = list(csv.DictReader(stream))
        if len(rows) != 9:
            raise RuntimeError("Expected three scenes at three aspects")
        pixels = {(row["scene"], row["aspect"]): row["pixel_hash"] for row in rows}
        if expected_pixels is not None and pixels != expected_pixels:
            raise RuntimeError("Repeat process produced different sampled pixels")
        expected_pixels = pixels
        report["runs"].append({"cpu_seconds": process_cpu_seconds(process), "rows": rows})
        (root / "results.json").write_text(json.dumps(report, indent=2))
        print(f"Run {run}: state/pixel checks passed; CPU="
              f"{report['runs'][-1]['cpu_seconds']}", flush=True)
        for row in rows:
            print(json.dumps(row), flush=True)


if __name__ == "__main__":
    main()
