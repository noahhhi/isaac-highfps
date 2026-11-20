"""Exercise both launch scripts with local fixtures and an instrumented curl."""
from hashlib import sha256
import os
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
STEAM = "1.9.7.19.J510"
REPENTOGON = "1.9.7.12.J273"


def game_image(version, suffix=""):
    return f"MZ\x00Binding of Isaac: Repentance+ v{version}\x00{suffix}".encode()


def digest(contents):
    return sha256(contents).hexdigest()


class UpdateLauncherTest(unittest.TestCase):
    def run_script(self, platform):
        with TemporaryDirectory(prefix="isaac launcher test ") as tmp:
            root = Path(tmp)
            game = root / "游戏 files"
            game.mkdir()
            server = root / "downloads"
            server.mkdir()
            tools = root / "bin"
            tools.mkdir()
            calls = root / "requests"
            bin_curl = tools / "curl"
            bin_curl.write_text(
                "#!/usr/bin/env python3\n"
                "import os, pathlib, shutil, sys\n"
                "argv = sys.argv\n"
                "url = argv[-1]\n"
                "with open(os.environ['REQUESTS'], 'a') as trace: trace.write(url + '\\n')\n"
                "path = pathlib.Path(os.environ['DOWNLOADS'], *url.split('/releases/', 1)[-1].split('/'))\n"
                "if not path.is_file(): raise SystemExit(22)\n"
                "shutil.copyfile(path, argv[argv.index('--output')+1])\n"
            )
            bin_curl.chmod(0o755)
            (tools / "curl.exe").symlink_to(bin_curl)
            env = {**os.environ, "PATH": f"{tools}:{os.environ['PATH']}",
                   "REQUESTS": str(calls), "DOWNLOADS": str(server)}

            def target(url, contents):
                path = server / url
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(contents)

            def requested():
                return calls.read_text().splitlines() if calls.exists() else []

            ini = b"[highfps]\nAutoUpdate=1\n"
            old, new = b"steam DLL", b"repentogon DLL"
            target("download/steam-r1/winmm.dll", old)
            target("download/steam-r1/isaac-highfps.ini", ini)
            target("download/repentogon-r1/winmm.dll", new)
            manifest = (f"isaac-highfps-builds-v1\n"
                        f"{STEAM} {digest(game_image(STEAM))} steam-r1 {digest(old)} {digest(ini)}\n"
                        f"{REPENTOGON} {digest(game_image(REPENTOGON))} repentogon-r1 {digest(new)} {digest(ini)}\n")
            target("latest/download/highfps-builds.txt", manifest.encode())
            exe = game / "isaac-ng.exe"
            exe.write_bytes(game_image(STEAM))
            if platform == "bash":
                interpreter = ["bash"]
                ext, launch, trigger = "sh", ["--launch", "--"], "--check-after"
                install_dir = "--game-dir"
            else:
                pwsh = os.getenv("HIGHFPS_TEST_PWSH") or shutil.which("pwsh")
                if not pwsh:
                    self.skipTest("PowerShell is unavailable")
                interpreter = [pwsh, "-NoProfile", "-File"]
                ext, launch, trigger = "ps1", ["-Launch"], "-CheckAfter"
                install_dir = "-GameDir"

            def invoke(script, *arguments):
                return subprocess.run(interpreter + [str(script), *arguments], env=env, timeout=12,
                                      capture_output=True, text=True, check=False)

            installed = invoke(ROOT / "install" / f"install.{ext}", install_dir, str(game))
            self.assertEqual(installed.returncode, 0, installed.stdout + installed.stderr)
            launcher = game / f"isaac-highfps.{ext}"
            self.assertTrue(launcher.is_file())
            dll = game / "winmm.dll"
            self.assertEqual(dll.read_bytes(), old)
            state = game / ".isaac-highfps-game-version"
            self.assertEqual(state.read_text().strip(), STEAM)
            self.assertEqual(len(requested()), 3)

            result = invoke(launcher, *launch, "/bin/echo", "argument with spaces")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), "argument with spaces")
            self.assertEqual(len(requested()), 3)
            exe.write_bytes(game_image(STEAM, "same version, changed bytes"))
            result = invoke(launcher, trigger, str(os.getpid()))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(len(requested()), 3)

            # The fake game is a real child process. Its lifetime keeps the old DLL active.
            exe.write_bytes(game_image(REPENTOGON))
            with subprocess.Popen(["sleep", "20"]) as owner:
                try:
                    with subprocess.Popen(interpreter + [str(launcher), trigger, str(owner.pid)], env=env,
                                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) as worker:
                        try:
                            log = game / "isaac-highfps-update.log"
                            deadline = time.monotonic() + 8
                            while "Waiting for Isaac to close" not in (log.read_text(encoding="utf-8-sig") if log.exists() else ""):
                                if worker.poll() is not None:
                                    details = log.read_text(encoding="utf-8-sig") if log.exists() else ""
                                    self.fail(f"Updater exited before staging: {worker.communicate()}\n{details}")
                                self.assertLess(time.monotonic(), deadline)
                                time.sleep(.1)
                            self.assertEqual(dll.read_bytes(), old)
                            self.assertEqual(len(requested()), 5)
                            owner.terminate()
                            owner.wait(timeout=5)
                            stdout, stderr = worker.communicate(timeout=8)
                            self.assertEqual(worker.returncode, 0, stdout + stderr)
                        finally:
                            if worker.poll() is None:
                                worker.kill()
                finally:
                    if owner.poll() is None:
                        owner.terminate()
            self.assertEqual(dll.read_bytes(), new)
            self.assertEqual(next(game.glob("highfps-backup-*/winmm.dll")).read_bytes(), old)
            self.assertEqual(state.read_text().strip(), REPENTOGON)
            self.assertEqual((game / "isaac-highfps.ini").read_bytes(), ini)
            result = invoke(launcher, trigger, str(os.getpid()))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(len(requested()), 5)

            # Failed checks are still attempted only once per version.
            missing = "1.9.7.99.J999"
            exe.write_bytes(game_image(missing))
            result = invoke(launcher, trigger, str(os.getpid()))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(state.read_text().strip(), missing)
            result = invoke(launcher, trigger, str(os.getpid()))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(len(requested()), 6)
            self.assertEqual(dll.read_bytes(), new)

            exe.write_bytes(game_image(STEAM))
            (game / "isaac-highfps.ini").write_text("[highfps]\nAutoUpdate=0\n")
            result = invoke(launcher, trigger, str(os.getpid()))
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(len(requested()), 6)
            self.assertEqual(dll.read_bytes(), new)

    def test_linux_launcher(self):
        self.run_script("bash")

    def test_powershell_launcher(self):
        self.run_script("powershell")


if __name__ == "__main__":
    unittest.main()
