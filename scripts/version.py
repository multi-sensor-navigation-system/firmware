"""PlatformIO pre-build script: stamp the firmware with the git revision."""
import subprocess

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

try:
    sha = subprocess.check_output(
        ["git", "rev-parse", "--short", "HEAD"], stderr=subprocess.DEVNULL
    ).decode().strip()
except Exception:
    sha = "unknown"

env.Append(CPPDEFINES=[("MSNS_GIT_SHA", env.StringifyMacro(sha))])  # noqa: F821
