#!/usr/bin/env python3
"""stack_guard.py — a PR's base: `future`, or the branch of a PR that is still open.

A PR stacked on another targets the other's branch. When that one is merged, GitHub does not
move the next PR onto `future` (branches are not deleted on merge here), and merged as it is,
the next PR lands in a branch nobody merges: so it was with #41–#43 and with #83–#84. The guard
puts a "Stack guard" status on the PR:
- its base is `future` or `main`: success;
- its base is the branch of an open PR: success, "stacked on #N";
- anything else: failure, "the base is a branch without an open PR: retarget to future".
When a PR is merged or closed, every open PR on its branch gets that failure at once, before
anyone merges it; moving it onto `future` (an edit of the PR) turns it green again.

In GitHub Actions (.github/workflows/stack-guard.yml): GITHUB_EVENT_PATH, GITHUB_REPOSITORY,
GH_TOKEN; it talks to GitHub through `gh api`. By hand, without posting anything:

    python3 .github/scripts/stack_guard.py --check 86 [--repo OpenTela/TelaOS]
"""

import json
import os
import subprocess
import sys

TRUNKS = ("future", "main")
CONTEXT = "Stack guard"


def verdict(base, open_heads):
    """(state, description) of a PR on `base`; open_heads: {branch: number} of the open PRs"""
    if base in TRUNKS:
        return "success", f"База — {base}"
    if base in open_heads:
        return "success", f"Стоит на #{open_heads[base]}: сливать после него, переведя на future"
    return "failure", f"База {base} — ветка без открытого PR: переведи на future"


def orphans(closed, open_prs):
    """[(pr, state, description)]: the open PRs on the branch of the PR just closed"""
    how = "слит" if closed.get("merged") else "закрыт без слияния"
    text = f"База — ветка #{closed['number']}, а он {how}: переведи на future"
    return [(p, "failure", text) for p in open_prs if p["base"]["ref"] == closed["head"]["ref"]]


def heads(open_prs, repo):
    """{branch: number} of the open PRs from this repository's own branches"""
    return {p["head"]["ref"]: p["number"] for p in open_prs
            if p["head"].get("repo") and p["head"]["repo"].get("full_name") == repo}


def gh(*args):
    return subprocess.run(["gh", "api", *args], check=True, capture_output=True, text=True).stdout


def status(repo, sha, state, text, url):
    """The "Stack guard" status of a commit; False if GitHub refused (a fork's token cannot write)"""
    try:
        gh("-X", "POST", f"repos/{repo}/statuses/{sha}", "-f", f"state={state}", "-f", f"context={CONTEXT}",
           "-f", f"description={text[:140]}", "-f", f"target_url={url}")
        return True
    except subprocess.CalledProcessError as e:
        print(f"stack_guard: no status on {sha[:7]}: {e.stderr.strip()[:200]}", file=sys.stderr)
        return False


def check(repo, number):
    """The verdict on one open PR, printed, nothing posted"""
    open_prs = json.loads(gh(f"repos/{repo}/pulls?state=open&per_page=100"))
    pr = next((p for p in open_prs if p["number"] == number), None)
    if pr is None:
        print(f"#{number} is not an open PR of {repo}")
        return 2
    state, text = verdict(pr["base"]["ref"], heads([p for p in open_prs if p is not pr], repo))
    print(f"#{number} on {pr['base']['ref']}: {state} — {text}")
    return 0 if state == "success" else 1


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if argv[:1] == ["--check"]:
        repo = argv[argv.index("--repo") + 1] if "--repo" in argv else "OpenTela/TelaOS"
        return check(repo, int(argv[1]))
    with open(os.environ["GITHUB_EVENT_PATH"], encoding="utf-8") as f:
        event = json.load(f)
    repo = os.environ["GITHUB_REPOSITORY"]
    url = (f"{os.environ.get('GITHUB_SERVER_URL', 'https://github.com')}/{repo}/actions/runs/"
           f"{os.environ.get('GITHUB_RUN_ID', '')}")
    pr = event["pull_request"]
    open_prs = [p for p in json.loads(gh(f"repos/{repo}/pulls?state=open&per_page=100"))
                if p["number"] != pr["number"]]
    if event.get("action") == "closed":
        for p, state, text in orphans(pr, open_prs):
            print(f"#{p['number']}: {text}")
            status(repo, p["head"]["sha"], state, text, url)
        return 0
    state, text = verdict(pr["base"]["ref"], heads(open_prs, repo))
    print(text)
    status(repo, pr["head"]["sha"], state, text, url)
    return 0 if state == "success" else 1


if __name__ == "__main__":
    sys.exit(main())
