#!/usr/bin/env python3
"""Runs the menu in the PC emulator by key scripts and compares screenshots.

Usage: run.py [--update] [SCENARIO...]
Each tests/scenarios/NAME.keys runs fpdoom/apps/apps_pc.bin (make pctest) in a
fresh copy of phone-data/ (make data). Key script syntax: fpdoom/pctest/syscode.c.
"shot X" saves tests/out/NAME/X.png, which must equal tests/golden/NAME/X.png;
--update accepts the new screenshots as the golden ones.
Budgets: a "#! read N" or "#! peak N" line limits, for the shots after it, the
bytes read from the card since the previous shot and how much the heap grew
in that time over its size at the previous shot (fpdoom/apps/pccard.c).
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

from PIL import Image, ImageChops

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TESTS = os.path.join(ROOT, "tests")
APP = os.path.join(ROOT, "fpdoom/apps/apps_pc.bin")
DATA = os.path.join(ROOT, "tests/out/card")


def run(name, update):
    keys = os.path.join(TESTS, "scenarios", name + ".keys")
    out = os.path.join(TESTS, "out", name)
    golden = os.path.join(TESTS, "golden", name)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    with tempfile.TemporaryDirectory() as tmp:
        card = os.path.join(tmp, "card")
        shutil.copytree(DATA, card)
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", FP_KEYS=keys, FP_SHOTS=out)
        r = subprocess.run([APP, "--res", "240", "320"], cwd=card, env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        log = r.stdout.decode("utf-8", "replace")
        open(os.path.join(out, "log.txt"), "w").write(log)
        if r.returncode:
            return [f"exit code {r.returncode}:\n{log[-2000:]}"]
    errors = check_budgets(keys, log)
    shots = sorted(f[:-4] for f in os.listdir(out) if f.endswith(".ppm"))
    for shot in shots:
        ppm = os.path.join(out, shot + ".ppm")
        img = Image.open(ppm).convert("RGB")
        os.remove(ppm)
        png = os.path.join(out, shot + ".png")
        img.save(png)
        ref = os.path.join(golden, shot + ".png")
        if update:
            os.makedirs(golden, exist_ok=True)
            shutil.copy(png, ref)
        elif not os.path.exists(ref):
            errors.append(f"{shot}: no golden screenshot")
        elif ImageChops.difference(img, Image.open(ref).convert("RGB")).getbbox():
            errors.append(f"{shot}: differs from golden")
    if update and os.path.isdir(golden):
        for f in os.listdir(golden):
            if f[:-4] not in shots:
                os.remove(os.path.join(golden, f))
    return errors


def check_budgets(keys, log):
    limits, budget = {}, {}
    for line in open(keys):
        m = re.match(r"#!\s*(read|peak)\s+(\d+)", line)
        if m:
            limits[m.group(1)] = int(m.group(2))
            continue
        for shot in re.findall(r"\bshot\s+(\S+)", line.split("#")[0]):
            budget[shot] = dict(limits)
    errors = []
    for m in re.finditer(r"^io (\S+) read=(\d+) write=(\d+) heap=(\d+) peak=(\d+)$", log, re.M):
        got = {"read": int(m.group(2)), "peak": int(m.group(5))}
        for k, v in budget.get(m.group(1), {}).items():
            if got[k] > v:
                errors.append(f"{m.group(1)}: {k} {got[k]} > {v}")
    return errors


def main():
    args = sys.argv[1:]
    update = "--update" in args
    names = [a for a in args if a != "--update"] or sorted(
        f[:-5] for f in os.listdir(os.path.join(TESTS, "scenarios")) if f.endswith(".keys"))
    failed = 0
    for name in names:
        errors = run(name, update)
        print(f"{'FAIL' if errors else 'ok  '} {name}")
        for e in errors:
            print("     " + e)
        failed += bool(errors)
    if failed:
        sys.exit(f"{failed} of {len(names)} scenarios failed, screenshots in tests/out/")


if __name__ == "__main__":
    main()
