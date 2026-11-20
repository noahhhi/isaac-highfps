#!/usr/bin/env python3
"""Package a J510 build from build-wsl.sh or build.bat for GitHub Releases."""
import argparse
from hashlib import sha256
from pathlib import Path
import re
import shutil
from zipfile import ZIP_DEFLATED, ZipFile

ROOT = Path(__file__).resolve().parents[1]
GAME_SHA256 = "114673659401da17e55bd44b2c9f1cb7d40ce5fd31e15690588661a8a7c60ff6"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "release" / "j510")
    opts = parser.parse_args()
    version = (ROOT / "src" / "version.h").read_text()
    number = re.search(r"^#define HIGHFPS_J510_REVISION ([1-9][0-9]{0,8})\s*$", version, re.M)[1]
    name = re.search(r'^#define HIGHFPS_VERSION "([^"]+)"$', version, re.M)[1]
    assets = {
        "winmm.dll": ROOT / "build" / "winmm.dll",
        "isaac-highfps.ini": ROOT / "isaac-highfps.j510.ini.example",
        "install.sh": ROOT / "install" / "install.sh",
        "install.ps1": ROOT / "install" / "install.ps1",
    }
    if any(not file.is_file() for file in assets.values()):
        parser.error("Build with HIGHFPS_ENABLE_J510=1 before packaging.")
    opts.output.mkdir(parents=True, exist_ok=True)
    for target, source in assets.items():
        shutil.copyfile(source, opts.output / target)
    dll_hash, ini_hash = [sha256((opts.output / asset).read_bytes()).hexdigest()
                          for asset in ("winmm.dll", "isaac-highfps.ini")]
    manifest = opts.output / "highfps-builds.txt"
    rows = ["isaac-highfps-builds-v1",
            f"1.9.7.19.J510 {GAME_SHA256} j510-r{number} {dll_hash} {ini_hash}"]
    manifest.write_bytes(("\n".join(rows) + "\n").encode("ascii"))

    with ZipFile(opts.output / f"isaac-highfps-{name}.zip", "w", ZIP_DEFLATED) as archive:
        for asset in assets:
            archive.write(opts.output / asset, asset)
        archive.write(manifest, manifest.name)
        archive.write(ROOT / "README.md", "README.md")
    print(f"Tag: j510-r{number}, version: {name}, output: {opts.output}")


if __name__ == "__main__":
    main()
