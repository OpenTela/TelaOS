#!/usr/bin/env python3
"""Tests for stack_guard.py, GitHub faked: python3 -m unittest discover -s .github/scripts -p 'test_*.py'"""

import json
import os
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import stack_guard as sg  # noqa: E402

REPO = "OpenTela/TelaOS"


def pr(number, head, base, merged=False, fork=False):
    return {"number": number, "merged": merged,
            "head": {"ref": head, "sha": f"{number:07d}" + "0" * 33,
                     "repo": {"full_name": "someone/TelaOS" if fork else REPO}},
            "base": {"ref": base}}


class Verdict(unittest.TestCase):
    def test_on_future_or_main(self):
        self.assertEqual(sg.verdict("future", {}), ("success", "База — future"))
        self.assertEqual(sg.verdict("main", {})[0], "success")

    def test_stacked_on_an_open_pr(self):
        state, text = sg.verdict("feature/sim-idle-redraw", {"feature/sim-idle-redraw": 82})
        self.assertEqual(state, "success")
        self.assertIn("#82", text)

    def test_on_a_branch_without_an_open_pr(self):
        state, text = sg.verdict("feature/sim-idle-redraw", {})
        self.assertEqual(state, "failure")
        self.assertIn("переведи на future", text)
        self.assertLessEqual(len(text), 140)  # GitHub's limit on a status description

    def test_only_this_repository_s_branches(self):
        open_prs = [pr(82, "feature/sim-idle-redraw", "future"), pr(11, "feature/sim-idle-redraw", "future", fork=True)]
        self.assertEqual(sg.heads(open_prs, REPO), {"feature/sim-idle-redraw": 82})


class Orphans(unittest.TestCase):
    def test_merged_the_prs_on_its_branch_turn_red(self):
        merged = pr(82, "feature/sim-idle-redraw", "future", merged=True)
        open_prs = [pr(83, "fix/launcher-clock-redraw", "feature/sim-idle-redraw"),
                    pr(84, "fix/sim-freertos-timers", "fix/launcher-clock-redraw"), pr(78, "fix/x", "future")]
        got = sg.orphans(merged, open_prs)
        self.assertEqual([(p["number"], state) for p, state, _ in got], [(83, "failure")])
        self.assertIn("#82, а он слит", got[0][2])

    def test_closed_unmerged_too(self):
        closed = pr(61, "fix/tabs", "future")
        got = sg.orphans(closed, [pr(70, "fix/y", "fix/tabs")])
        self.assertIn("закрыт без слияния", got[0][2])


class Main(unittest.TestCase):
    """main() with gh faked: what it posts and how it exits"""

    def run_main(self, action, this, open_prs):
        event = {"action": action, "pull_request": this}
        posted = []

        def gh(*args):
            if args[0].startswith(f"repos/{REPO}/pulls"):
                return json.dumps(open_prs + [this])
            posted.append(args)
            return "{}"
        with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
            json.dump(event, f)
        env = {"GITHUB_EVENT_PATH": f.name, "GITHUB_REPOSITORY": REPO, "GITHUB_RUN_ID": "7"}
        try:
            with mock.patch.dict(os.environ, env), mock.patch.object(sg, "gh", gh), \
                    mock.patch("sys.stdout"):
                code = sg.main([])
        finally:
            os.unlink(f.name)
        return code, posted

    def test_a_stacked_pr_whose_base_is_open(self):
        code, posted = self.run_main("opened", pr(83, "fix/launcher-clock-redraw", "feature/sim-idle-redraw"),
                                     [pr(82, "feature/sim-idle-redraw", "future")])
        self.assertEqual(code, 0)
        self.assertEqual(posted[0][2], f"repos/{REPO}/statuses/{pr(83, '', '')['head']['sha']}")
        self.assertIn("state=success", posted[0])
        self.assertIn(f"context={sg.CONTEXT}", posted[0])

    def test_a_pr_left_on_a_merged_branch_fails(self):
        code, posted = self.run_main("synchronize", pr(83, "fix/launcher-clock-redraw", "feature/sim-idle-redraw"), [])
        self.assertEqual(code, 1)
        self.assertIn("state=failure", posted[0])

    def test_retargeted_onto_future_turns_green(self):
        code, posted = self.run_main("edited", pr(83, "fix/launcher-clock-redraw", "future"), [])
        self.assertEqual(code, 0)
        self.assertIn("state=success", posted[0])

    def test_merging_marks_the_prs_on_its_branch(self):
        code, posted = self.run_main("closed", pr(82, "feature/sim-idle-redraw", "future", merged=True),
                                     [pr(83, "fix/launcher-clock-redraw", "feature/sim-idle-redraw"),
                                      pr(78, "fix/x", "future")])
        self.assertEqual(code, 0)
        self.assertEqual(len(posted), 1)
        self.assertEqual(posted[0][2], f"repos/{REPO}/statuses/{pr(83, '', '')['head']['sha']}")
        self.assertIn("state=failure", posted[0])
        self.assertIn(f"target_url=https://github.com/{REPO}/actions/runs/7", posted[0])


if __name__ == "__main__":
    unittest.main()
