#!/bin/sh
# Install this repository's git hooks.
#
#   sh scripts/install_git_hooks.sh
#
# The pre-commit hook runs scripts/secret_scan.py, which blocks the commit if
# the staged content contains credentials. Hooks live in .git/hooks/ and are
# not versioned, so run this once on every new clone.
set -e

repo_root=$(git rev-parse --show-toplevel)
hooks_dir="$repo_root/.git/hooks"

if [ ! -d "$hooks_dir" ]; then
    echo "ERROR: cannot find $hooks_dir" >&2
    echo "       Run this from inside a git repository." >&2
    exit 1
fi

cat > "$hooks_dir/pre-commit" <<'HOOK'
#!/bin/sh
# Installed by scripts/install_git_hooks.sh
# Blocks the commit if the staged content contains secrets.
exec python3 "$(git rev-parse --show-toplevel)/scripts/secret_scan.py"
HOOK

chmod +x "$hooks_dir/pre-commit"

echo "Pre-commit hook installed at $hooks_dir/pre-commit"
echo
echo "To bypass it (only after reviewing the content):"
echo "    git commit --no-verify"
echo
echo "To audit the whole repository:"
echo "    python3 scripts/secret_scan.py --all"
echo "To audit the whole history:"
echo "    python3 scripts/secret_scan.py --history"
