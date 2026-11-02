#!/usr/bin/env bash
# publish_screens.sh — keeps PR screenshots in the sim-screens branch, so a PR comment can
# show them as images.
#
#   publish_screens.sh <remote-url> <pr> <sha> <dir>   put <dir> at pr-<pr>/<sha>/
#                                                     (replaces that PR's previous run)
#   publish_screens.sh <remote-url> <pr>               remove pr-<pr>/ (PR closed)
#
# The branch is one commit without history: old pictures do not pile up in the repository.
# Other PRs may publish at the same time: --force-with-lease refuses to overwrite theirs,
# and the script starts again from the new tip.
set -euo pipefail

remote=$1
pr=$2
sha=${3:-}
src=${4:+$(cd "$4" && pwd)}
branch=sim-screens

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
git -C "$work" init -q
git -C "$work" config user.name "telaos-sim"
git -C "$work" config user.email "41898282+github-actions[bot]@users.noreply.github.com"

for attempt in 1 2 3 4 5 6 7 8 9 10; do
    tip=""
    if git -C "$work" fetch -q --depth 1 "$remote" "$branch" 2>/dev/null; then
        tip=$(git -C "$work" rev-parse FETCH_HEAD)
        git -C "$work" checkout -q -f -B "$branch" FETCH_HEAD
        git -C "$work" clean -qfdx
    elif [ -n "$sha" ]; then
        git -C "$work" checkout -q --orphan "$branch"
        printf 'Simulator screenshots of open pull requests, written by CI (sim/tools/publish_screens.sh).\n' \
            > "$work/README.md"
    else
        exit 0  # nothing published yet, nothing to remove
    fi

    rm -rf "${work:?}/pr-$pr"
    if [ -n "$sha" ]; then
        mkdir -p "$work/pr-$pr/$sha"
        cp -R "$src/." "$work/pr-$pr/$sha/"
        message="PR #$pr: screens of $sha"
    else
        message="PR #$pr closed: screens removed"
    fi
    git -C "$work" add -A
    if [ -n "$tip" ] && git -C "$work" diff --cached --quiet; then
        exit 0
    fi
    commit=$(git -C "$work" commit-tree "$(git -C "$work" write-tree)" -m "$message")
    if git -C "$work" push -q --force-with-lease="refs/heads/$branch:$tip" "$remote" \
        "$commit:refs/heads/$branch" 2>/dev/null; then
        echo "publish_screens: $message"
        exit 0
    fi
    sleep $((attempt + RANDOM % 4))  # jitter: racing runs do not retry in lockstep
done
echo "publish_screens: could not push to $branch" >&2
exit 1
