#!/usr/bin/env python3
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time
import zlib


PROPERTIES = (
    "GAMESCOPE_DISPLAY_REFRESH_RATE_FEEDBACK",
    "GAMESCOPE_VRR_CAPABLE",
    "GAMESCOPE_VRR_ENABLED",
    "GAMESCOPE_VRR_FEEDBACK",
    "GAMESCOPE_LIMITER_FEEDBACK",
    "GAMESCOPE_FOCUSED_APP_GFX",
    "GAMESCOPE_XWAYLAND_SERVER_ID",
)


def run_query(arguments, environment):
    try:
        return subprocess.run(
            arguments, env=environment, capture_output=True, text=True,
            timeout=1, check=True,
        ).stdout
    except (OSError, subprocess.SubprocessError):
        return ""


def launch_app_id(environment):
    identifiers = [environment[name] for name in ("SteamAppId", "STEAM_COMPAT_APP_ID") if name in environment]
    if not identifiers or any(not re.fullmatch(r"[1-9][0-9]{0,9}", value) for value in identifiers):
        return None
    if len(set(identifiers)) != 1 or int(identifiers[0]) > 0xFFFFFFFF:
        return None
    return identifiers[0]


def read_mode(feedback, flags, expected_app=None):
    values = dict(re.findall(r"^(GAMESCOPE_[A-Z_]+)\(CARDINAL\) = (\d{1,10})$", feedback, re.MULTILINE))
    if values.get(PROPERTIES[6]) != "0":
        return None
    if expected_app is not None and values.get(PROPERTIES[5]) != expected_app:
        return None
    refresh = int(values.get(PROPERTIES[0], 0))
    if not 10 <= refresh <= 1000:
        return None
    capable, enabled, active = ({"0": 1, "1": 2}.get(values.get(name), 0) for name in PROPERTIES[1:4])
    if not flags & 4:
        capable = 1
    elif capable != 2:
        capable = 0
    if capable != 2 and active == 2:
        active = 0
    return refresh, (capable, enabled, active), int(values.get(PROPERTIES[4]) == "1")


def observe(environment, display, socket, expected_app=None):
    controller = environment | {"GAMESCOPE_WAYLAND_DISPLAY": socket, "LC_ALL": "C"}
    info = run_query(["gamescopectl"], controller)
    connector = re.search(r"^\s*- Connector Name: (.+)$", info, re.MULTILINE)
    flags = re.search(r"^\s*- Display Flags: (0x[0-9a-fA-F]+)$", info, re.MULTILINE)
    if not connector or not flags:
        return None
    x11 = environment | {"DISPLAY": display, "LC_ALL": "C"}
    result = read_mode(run_query(["xprop", "-root", *PROPERTIES], x11), int(flags[1], 16), expected_app)
    if result is None:
        return None
    refresh, states, paced = result
    output_id = zlib.crc32(connector[1].encode()) or 1
    now = time.time_ns() // 1_000_000
    return f"HFDS1 {now} {output_id} {refresh} 1 {' '.join(map(str, states))} {paced}\n"


def publish(path, record):
    if record is None:
        path.unlink(missing_ok=True)
        return
    temporary = path.with_name("state.next")
    temporary.write_text(record, encoding="ascii")
    temporary.replace(path)


def main():
    parser = argparse.ArgumentParser(description="Read the Deck Gamescope output for Isaac High FPS under Proton.")
    parser.add_argument("--display", default=":0", help="Steam's outer Gamescope Xwayland display")
    parser.add_argument("--socket", default=None, help="Outer Gamescope control socket name")
    parser.add_argument("--probe", action="store_true", help="Print a single observation without launching a game")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    environment = os.environ.copy()
    socket = args.socket or environment.get("GAMESCOPE_WAYLAND_DISPLAY")
    if args.probe:
        if args.command:
            parser.error("--probe does not run a command")
        record = observe(environment, args.display, socket or "gamescope-0")
        if record is not None:
            print(record, end="")
        return 0 if record is not None else 1
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("expected a launch command after --")
    environment.pop("ISAAC_HIGHFPS_DISPLAY_STATE", None)
    app_id = launch_app_id(environment)
    if app_id is None or not socket:
        return subprocess.call(command, env=environment)
    parent = environment.get("XDG_RUNTIME_DIR")
    with tempfile.TemporaryDirectory(prefix="isaac-highfps-display-", dir=parent) as directory:
        path = Path(directory) / "state"
        environment["ISAAC_HIGHFPS_DISPLAY_STATE"] = "Z:" + str(path).replace("/", "\\")
        publish(path, observe(environment, args.display, socket, app_id))
        child = subprocess.Popen(command, env=environment)
        while child.poll() is None:
            try:
                return child.wait(timeout=1)
            except subprocess.TimeoutExpired:
                publish(path, observe(environment, args.display, socket, app_id))
        return child.returncode


if __name__ == "__main__":
    raise SystemExit(main())
