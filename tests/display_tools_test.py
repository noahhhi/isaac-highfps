import io
from pathlib import Path
import runpy
import unittest


TOOLS = Path(__file__).resolve().parents[1] / "tools"
DECK = runpy.run_path(str(TOOLS / "steam-deck-display.py"))
ANALYZER = runpy.run_path(str(TOOLS / "analyze-presentation.py"))


FEEDBACK = """GAMESCOPE_DISPLAY_REFRESH_RATE_FEEDBACK(CARDINAL) = 80
GAMESCOPE_VRR_CAPABLE(CARDINAL) = 1
GAMESCOPE_VRR_ENABLED(CARDINAL) = 1
GAMESCOPE_VRR_FEEDBACK(CARDINAL) = 1
GAMESCOPE_LIMITER_FEEDBACK(CARDINAL) = 1
GAMESCOPE_FOCUSED_APP_GFX(CARDINAL) = 250900
GAMESCOPE_XWAYLAND_SERVER_ID(CARDINAL) = 0
GAMESCOPE_FPS_LIMIT(CARDINAL) = 40
"""

RECORDS = """[present-frame] t=1.000000 dt=0.000000 gf=4
[present-work] serial=8 sample=0.983333 ready=0.997000 submitted=0.999000 generateMs=5.134 paceMs=2.000 swapMs=1.000
[present-frame] t=1.016667 dt=0.016667 gf=5
[present-work] serial=9 sample=1.000000 ready=1.015000 submitted=1.015000 generateMs=8.000 paceMs=0.000 swapMs=1.667
"""


class DeckObservation(unittest.TestCase):
    def test_nominal_rate_is_not_the_steam_cap(self):
        self.assertEqual(DECK["read_mode"](FEEDBACK, 4), (80, (2, 2, 2), 1))

    def test_positive_feedback_does_not_override_unsupported_output(self):
        self.assertEqual(DECK["read_mode"](FEEDBACK, 0), (80, (1, 2, 0), 1))

    def test_limiting_request_cannot_substitute_for_missing_mode(self):
        self.assertIsNone(DECK["read_mode"]("GAMESCOPE_FPS_LIMIT(CARDINAL) = 40", 4))

    def test_only_the_focused_steam_application_can_use_host_feedback(self):
        self.assertEqual(DECK["read_mode"](FEEDBACK, 4, "250900"), (80, (2, 2, 2), 1))
        self.assertIsNone(DECK["read_mode"](FEEDBACK, 4, "769"))
        self.assertIsNone(DECK["read_mode"](FEEDBACK.replace(
            "GAMESCOPE_FOCUSED_APP_GFX(CARDINAL) = 250900\n", ""), 4, "250900"))

    def test_game_xwayland_root_does_not_substitute_for_outer_feedback(self):
        self.assertIsNone(DECK["read_mode"](FEEDBACK.replace(
            "GAMESCOPE_XWAYLAND_SERVER_ID(CARDINAL) = 0", "GAMESCOPE_XWAYLAND_SERVER_ID(CARDINAL) = 1"), 4))

    def test_gpu_state_requires_an_unambiguous_steam_launch_id(self):
        self.assertEqual(DECK["launch_app_id"]({"SteamAppId": "250900", "STEAM_COMPAT_APP_ID": "250900"}), "250900")
        for environment in ({}, {"SteamAppId": "0"}, {"SteamAppId": "4294967296"},
                            {"SteamAppId": "250900", "STEAM_COMPAT_APP_ID": "769"}):
            self.assertIsNone(DECK["launch_app_id"](environment))


class PresentationTrace(unittest.TestCase):
    def test_reports_separate_work_and_swap_times(self):
        frames = list(ANALYZER["frames"](io.StringIO(RECORDS)))
        metrics = ANALYZER["analyze"](frames, 60)
        self.assertAlmostEqual(metrics["completion_hz"], 60, places=2)
        self.assertEqual(metrics["intervals_over_1_5_periods"], 0)
        self.assertEqual(metrics["generation_ms"]["p99"], 8.0)
        self.assertEqual(metrics["swap_ms"]["p99"], 1.667)

    def test_rejects_sequence_gaps_and_missing_work(self):
        for records in (RECORDS.replace("serial=9", "serial=10"), RECORDS.rsplit("\n", 2)[0]):
            with self.assertRaises(ValueError):
                list(ANALYZER["frames"](io.StringIO(records)))


if __name__ == "__main__":
    unittest.main()
