#!/usr/bin/env python3
"""Block commits that contain secrets.

Invoked from the pre-commit hook (see scripts/install_git_hooks.sh).
By default it only examines the lines ADDED by the current commit, so code
inherited from upstream does not produce false positives.

Usage:
    python3 scripts/secret_scan.py                 # what is staged
    python3 scripts/secret_scan.py --all           # every tracked file
    python3 scripts/secret_scan.py --history       # every blob in the history
    python3 scripts/secret_scan.py --check FILE... # specific files
"""

import argparse
import os
import re
import subprocess
import sys

# --------------------------------------------------------------------------
# Content rules: (label, compiled regex).
# --------------------------------------------------------------------------
# The patterns are assembled from fragments so that this file does not trip
# its own rules when it scans itself.
_DASH5 = "-" * 5
_PEM_RE = re.compile(
    re.escape(_DASH5 + "BEGIN ")
    + r"(?:RSA |EC |DSA |OPENSSH |PGP )?"
    + re.escape("PRIVATE KEY" + _DASH5)
)
_ROBOT_MAC_RE = re.compile(r"\b" + "90:70:69:" + "f6:69:80" + r"\b")

CONTENT_RULES = [
    ("PEM private key", _PEM_RE),
    ("AWS access key id", re.compile(r"\b(?:AKIA|ASIA)[0-9A-Z]{16}\b")),
    ("AWS secret access key",
     re.compile(r"(?i)aws_?secret_?access_?key\s*[:=]\s*[\"'][A-Za-z0-9/+=]{40}[\"']")),
    ("GitHub token", re.compile(r"\b(?:ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{36,}\b")),
    ("GitHub fine-grained PAT", re.compile(r"\bgithub_pat_[A-Za-z0-9_]{50,}\b")),
    ("Anthropic API key", re.compile(r"\bsk-ant-[A-Za-z0-9_\-]{20,}")),
    ("OpenAI-style API key", re.compile(r"\bsk-[A-Za-z0-9]{40,}")),
    ("Google API key", re.compile(r"\bAIza[0-9A-Za-z_\-]{35}\b")),
    ("Slack token", re.compile(r"\bxox[baprs]-[A-Za-z0-9-]{10,}")),
    ("npm token", re.compile(r"\bnpm_[A-Za-z0-9]{30,}\b")),
    ("JSON Web Token",
     re.compile(r"\beyJ[A-Za-z0-9_\-]{8,}\.[A-Za-z0-9_\-]{8,}\.[A-Za-z0-9_\-]{8,}")),
    ("Telegram bot token", re.compile(r"\b\d{8,10}:[A-Za-z0-9_-]{30,}\b")),
    ("API token in a settings file",
     re.compile(r"(?i)[\"'](?:ANTHROPIC|OPENAI|MINIMAX|GOOGLE|AZURE)_?(?:AUTH_)?(?:API_)?(?:KEY|TOKEN)[\"']\s*:\s*[\"'][^\"'\s]{16,}[\"']")),
    # Project specific values (see doc/03-hardware.md).
    ("Robot MAC address", _ROBOT_MAC_RE),
    ("Factory flash backup name", re.compile(r"robot-fabrica-[0-9a-f]{12}\.bin")),
]

# --------------------------------------------------------------------------
# File name rules.
# --------------------------------------------------------------------------
FILENAME_RULES = [
    (re.compile(r"\.pem$"), "PEM certificate or key"),
    (re.compile(r"\.(?:key|p12|pfx|jks|keystore)$"), "credential file"),
    (re.compile(r"(?:^|/)\.env(?:\.[^/]*)?$"), "environment variables file (.env)"),
    (re.compile(r"(?:^|/)\.ssh/"), "SSH key directory"),
    (re.compile(r"(?:^|/)\.aws/"), "AWS credentials directory"),
    (re.compile(r"(?:^|/)id_(?:rsa|ed25519|ecdsa|dsa)$"), "private SSH key"),
    (re.compile(r"(?:^|/)nvs[^/]*\.bin$"), "NVS dump (holds device credentials)"),
    (re.compile(r"(?:^|/)robot-fabrica-[^/]*\.bin$"), "factory flash backup"),
    (re.compile(r"(?:^|/)\.claude/settings(?:\.local)?\.json$"), "Claude settings with credentials"),
]

MAX_STAGED_FILE_BYTES = 2 * 1024 * 1024

# --------------------------------------------------------------------------
# Values that look like secrets but are only placeholders.
# --------------------------------------------------------------------------
PLACEHOLDER_RE = re.compile(
    r"""^(?:
          <[^>]*>          # <mqtt-password>
        | \{[^}]*\}        # {token}
        | \$\{[^}]*\}      # ${TOKEN}
        | %[sdv]           # %s
        | \.\.\.
        | x{3,}            # xxxx
        | your[_-].*
        | my[_-].*
        | changeme.*
        | example.*
        | placeholder.*
        | dummy.*
        | sample.*
        | insert.*
        | replace.*
        | bearer\s+$
    )$""",
    re.IGNORECASE | re.VERBOSE,
)

# Protocol strings that are not credentials.
KNOWN_NON_SECRETS = {
    "bearer ", "application/json", "text/plain", "multipart/form-data",
    "application/octet-stream", "audio/ogg, application/ogg",
}

# Assignments of the form key = "literal".
ASSIGNMENT_RE = re.compile(
    r"""(?ix)
    \b(?P<key>[A-Za-z0-9_]*(?:password|passwd|secret|token|api[_-]?key|apikey|
                            private[_-]?key|credential|access[_-]?key|
                            client[_-]?secret)[A-Za-z0-9_]*)
    \s*[:=]\s*
    (?P<q>["'])(?P<val>[^"'\n]{6,})(?P=q)
    (?!\s*\+)
    """
)


def looks_like_secret(value):
    """Return True when the literal has the shape of a real credential."""
    value = value.strip()
    if len(value) < 12:
        return False
    if value.lower() in KNOWN_NON_SECRETS:
        return False
    if PLACEHOLDER_RE.match(value):
        return False
    has_digit = any(c.isdigit() for c in value)
    has_upper = any(c.isupper() for c in value)
    has_lower = any(c.islower() for c in value)
    # Mixed with digits (typical key), or long enough and mixed case.
    if has_digit and (has_upper or has_lower):
        return True
    if len(value) >= 24 and has_upper and has_lower:
        return True
    return False


def run_git(args, cwd):
    return subprocess.run(
        ["git"] + args,
        cwd=cwd,
        capture_output=True,
        text=True,
        errors="replace",
        check=False,
    )


def scan_text(text, path, findings, start_line=1):
    """Apply the content rules to a block of text."""
    for offset, line in enumerate(text.splitlines()):
        line_no = start_line + offset
        if len(line) > 4000:
            line = line[:4000]
        for name, regex in CONTENT_RULES:
            match = regex.search(line)
            if match:
                findings.append(
                    (path, line_no, name, match.group(0)[:60])
                )
        match = ASSIGNMENT_RE.search(line)
        if match and looks_like_secret(match.group("val")):
            findings.append(
                (path, line_no, "Credential in a literal assignment",
                 f'{match.group("key")} = "{match.group("val")[:12]}..."')
            )


def scan_filename(path, findings):
    for regex, name in FILENAME_RULES:
        if regex.search(path):
            findings.append((path, 0, name, path))


def scan_staged(repo):
    """Scan the lines added by the commit in progress."""
    findings = []
    result = run_git(["diff", "--cached", "-U0", "--no-color"], repo)
    if result.returncode != 0:
        return findings

    current = None
    new_line = 1
    for line in result.stdout.splitlines():
        if line.startswith("+++ "):
            current = line[4:].split("\t")[0]
            if current.startswith("b/"):
                current = current[2:]
            continue
        if line.startswith("@@"):
            match = re.search(r"\+(\d+)", line)
            new_line = int(match.group(1)) if match else 1
            continue
        if line.startswith("+") and not line.startswith("+++"):
            scan_text(line[1:], current or "(unknown)", findings, new_line)
            new_line += 1
        elif not line.startswith("-"):
            new_line += 1
    return findings


def scan_files(repo, paths):
    """Scan whole files: staged content when available, otherwise from disk."""
    findings = []
    for path in paths:
        if path.endswith((".bin", ".png", ".jpg", ".jpeg", ".gif")):
            continue
        result = run_git(["show", f":{path}"], repo)
        if result.returncode == 0:
            content = result.stdout
        else:
            full = os.path.join(repo, path)
            if not os.path.isfile(full):
                continue
            try:
                with open(full, "r", encoding="utf-8", errors="replace") as handle:
                    content = handle.read()
            except OSError:
                continue
        scan_text(content, path, findings)
    return findings


def scan_tree(repo):
    """Scan every tracked file."""
    findings = []
    result = run_git(["ls-files"], repo)
    if result.returncode != 0:
        return findings
    for path in result.stdout.splitlines():
        path = path.strip()
        if not path:
            continue
        scan_filename(path, findings)
        if path.endswith((".md", ".txt", ".cc", ".c", ".h", ".py", ".yml", ".yaml",
                          ".json", ".sh", ".ini", ".cfg", ".conf")):
            blob = run_git(["show", f"HEAD:{path}"], repo)
            if blob.returncode == 0:
                scan_text(blob.stdout, path, findings)
    return findings


def scan_history(repo):
    """Scan the whole history: every unique blob, once."""
    findings = []
    result = run_git(["rev-list", "--objects", "--all"], repo)
    if result.returncode != 0:
        return findings
    seen = set()
    for line in result.stdout.splitlines():
        parts = line.split(None, 1)
        if len(parts) != 2:
            continue
        sha, path = parts
        if path in seen:
            continue
        seen.add(path)
        scan_filename(path, findings)
        blob = run_git(["cat-file", "-p", sha], repo)
        if blob.returncode != 0:
            continue
        scan_text(blob.stdout, path, findings)
    return findings


def check_staged_sizes(repo):
    findings = []
    result = run_git(["diff", "--cached", "--name-only"], repo)
    if result.returncode != 0:
        return findings
    for path in result.stdout.splitlines():
        path = path.strip()
        if not path:
            continue
        full = os.path.join(repo, path)
        if os.path.isfile(full):
            size = os.path.getsize(full)
            if size > MAX_STAGED_FILE_BYTES:
                findings.append((path, 0, "File too large", f"{size} bytes"))
    return findings


def report(findings):
    if not findings:
        return 0
    print("\n\033[1;31mPUSH BLOCKED: possible secrets in this commit\033[0m\n", file=sys.stderr)
    seen = set()
    for path, line, name, snippet in findings:
        key = (path, name, snippet)
        if key in seen:
            continue
        seen.add(key)
        where = f"{path}:{line}" if line else path
        print(f"  \033[1;33m{name}\033[0m", file=sys.stderr)
        print(f"    {where}", file=sys.stderr)
        print(f"    {snippet}", file=sys.stderr)
        print("", file=sys.stderr)
    print(
        "Review the content. If this is a false positive, allow it in\n"
        "scripts/secret_scan.py or use 'git commit --no-verify'.",
        file=sys.stderr,
    )
    return 1


def main():
    parser = argparse.ArgumentParser(description="Detect secrets before they are pushed.")
    parser.add_argument("--all", action="store_true", help="scan every tracked file")
    parser.add_argument("--history", action="store_true", help="scan the whole commit history")
    parser.add_argument("--check", nargs="+", metavar="FILE", help="scan specific files")
    parser.add_argument("--repo", default=".", help="repository root")
    args = parser.parse_args()

    repo = os.path.abspath(args.repo)
    findings = []

    if args.check:
        findings += scan_files(repo, args.check)
    elif args.history:
        findings += scan_history(repo)
    elif args.all:
        findings += scan_tree(repo)
    else:
        findings += scan_staged(repo)
        findings += check_staged_sizes(repo)

    return report(findings)


if __name__ == "__main__":
    sys.exit(main())
