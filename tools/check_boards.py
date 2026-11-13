#!/usr/bin/env python3
"""CI check: every board manifest is valid and has a PlatformIO environment."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
import boards  # noqa: E402


def main() -> int:
    errors = []
    envs = boards.platformio_envs()
    try:
        manifests = boards.load_all()
    except boards.BoardError as e:
        print(f"ERROR: {e}")
        return 1

    for board in manifests:
        env = envs.get(board["id"])
        if env is None:
            errors.append(f"boards/{board['id']}.json has no [env:{board['id']}] in platformio.ini")
        elif env.get("custom_board") != board["id"]:
            errors.append(f"[env:{board['id']}] must set custom_board = {board['id']}")

    known = {b["id"] for b in manifests}
    for name, env in envs.items():
        if env.get("custom_board") not in known:
            errors.append(f"[env:{name}] custom_board = {env.get('custom_board')!r} has no manifest")

    for e in errors:
        print(f"ERROR: {e}")
    if not errors:
        print(f"{len(manifests)} board manifests OK: {', '.join(sorted(known))}")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
