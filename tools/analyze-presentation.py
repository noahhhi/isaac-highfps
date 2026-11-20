#!/usr/bin/env python3
import argparse
import json
import math
from pathlib import Path
import re


DECIMAL = r"(\d+\.\d+)"
PRESENT = re.compile(r"\[present-frame\] t=" + DECIMAL + r" dt=" + DECIMAL + r" gf=(\d+)")
WORK = re.compile(
    r"\[present-work\] serial=(\d+) sample=" + DECIMAL + r" ready=" + DECIMAL +
    r" submitted=" + DECIMAL + r" generateMs=" + DECIMAL + r" paceMs=" + DECIMAL + r" swapMs=" + DECIMAL
)


def frames(lines):
    pending = None
    previous = None
    for line in lines:
        present = PRESENT.search(line)
        if present:
            if pending is not None:
                raise ValueError("present without matching work record")
            pending = (float(present[1]), float(present[2]), int(present[3]))
        work = WORK.search(line)
        if work:
            if pending is None:
                raise ValueError("work without matching present record")
            serial = int(work[1])
            sample, ready, submitted, generation, pace, swap = map(float, work.groups()[1:])
            completed, interval, game_frame = pending
            pending = None
            if not 0 <= sample <= ready <= submitted <= completed:
                raise ValueError(f"invalid timestamps at present {serial}")
            if previous and (serial != previous[0] + 1 or completed <= previous[1] or
                             abs(interval - (completed - previous[1])) > 0.000003):
                raise ValueError(f"gap or inconsistent times at present {serial}")
            previous = (serial, completed)
            yield {
                "serial": serial, "completed": completed, "game_frame": game_frame,
                "sample": sample, "generation_ms": generation, "pace_ms": pace,
                "swap_ms": swap, "sample_age_ms": (completed - sample) * 1000,
            }
    if pending is not None:
        raise ValueError("log ends with incomplete present/work pair")


def summary(values):
    ordered = sorted(values)
    return {
        "p50": ordered[math.ceil(len(ordered) * 0.5) - 1],
        "p95": ordered[math.ceil(len(ordered) * 0.95) - 1],
        "p99": ordered[math.ceil(len(ordered) * 0.99) - 1],
        "max": ordered[-1],
    }


def analyze(samples, fixed_hz=None):
    if len(samples) < 2:
        raise ValueError("select at least two completed presentations")
    intervals = [(second["completed"] - first["completed"]) * 1000
                 for first, second in zip(samples, samples[1:])]
    duration = (samples[-1]["completed"] - samples[0]["completed"])
    result = {
        "source": "application SwapBuffers completions; not scanout or visual evidence",
        "frames": len(samples),
        "window": [samples[0]["completed"], samples[-1]["completed"]],
        "completion_hz": len(intervals) / duration,
        "game_frame_delta": samples[-1]["game_frame"] - samples[0]["game_frame"],
        "interval_ms": summary(intervals),
        "sample_backsteps": sum(second["sample"] < first["sample"]
                                for first, second in zip(samples, samples[1:])),
    }
    for field in ("generation_ms", "pace_ms", "swap_ms", "sample_age_ms"):
        result[field] = summary([sample[field] for sample in samples])
    if fixed_hz is not None:
        result["fixed_period_ms"] = 1000 / fixed_hz
        result["intervals_over_1_5_periods"] = sum(
            interval > 1.5 * result["fixed_period_ms"] for interval in intervals)
    return result


def main():
    parser = argparse.ArgumentParser(description="Analyze paired FrameTiming=1 presentation records.")
    parser.add_argument("log", type=Path)
    parser.add_argument("--start", type=float, help="inclusive QPC timestamp")
    parser.add_argument("--end", type=float, help="exclusive QPC timestamp")
    parser.add_argument("--fixed-hz", type=float, help="observed fixed output rate, not the Steam cap")
    args = parser.parse_args()
    if args.fixed_hz is not None and (not math.isfinite(args.fixed_hz) or args.fixed_hz <= 0):
        parser.error("--fixed-hz must be a finite positive number")
    try:
        with args.log.open(encoding="utf-8", errors="replace") as stream:
            selected = [sample for sample in frames(stream)
                        if (args.start is None or sample["completed"] >= args.start)
                        and (args.end is None or sample["completed"] < args.end)]
        print(json.dumps(analyze(selected, args.fixed_hz), indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
