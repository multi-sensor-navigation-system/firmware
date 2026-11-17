"""PlatformIO pre-build script: apply the board manifest named by `custom_board`."""
import sys
from pathlib import Path

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

sys.path.insert(0, str(Path(env.subst("$PROJECT_DIR")) / "scripts"))  # noqa: F821
import boards  # noqa: E402

board_id = env.GetProjectOption("custom_board")  # noqa: F821
board = boards.load(board_id)

for name, value in boards.defines(board).items():
    if name == "MSNS_BOARD_ID":  # the only string; the rest are numeric literals
        value = env.StringifyMacro(value)  # noqa: F821
    env.Append(CPPDEFINES=[(name, value)])  # noqa: F821

print(f"Board manifest: {board['id']} — {board['name']} ({board['status']})")
