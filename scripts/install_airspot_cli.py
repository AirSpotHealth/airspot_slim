#!/usr/bin/env python3
"""Explicit, idempotent installation of the AirSpot command for this user."""
from pathlib import Path
import shlex
import shutil
import subprocess

WORKSPACE = Path(__file__).resolve().parents[3]
MARKER = "# >>> AirSpot ecosystem CLI >>>"
HEADER = "# AirSpot ecosystem CLI wrapper"


def install():
    home = Path.home()
    targets = [home / ".local/bin/airspot", home / "airspot/airspot"]
    text = "#!/usr/bin/env bash\n" + HEADER + "\nexec " + shlex.quote(str(WORKSPACE / "airspot")) + ' "$@"\n'
    # Validate all destinations before changing any existing command.
    for target in targets:
        if target.exists() and (not target.is_file() or HEADER not in target.read_text()):
            raise RuntimeError(f"Existing unrelated command preserved: {target}")
    for target in targets:
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text)
        target.chmod(0o755)
    profile = home / ".zshrc"
    existing = profile.read_text() if profile.exists() else ""
    if MARKER not in existing:
        with profile.open("a") as f:
            f.write('\n' + MARKER + '\nexport PATH="$HOME/.local/bin:$PATH"\n'
                    'airspot() { "$HOME/.local/bin/airspot" "$@"; }\n'
                    '# <<< AirSpot ecosystem CLI <<<\n')
    direnv = shutil.which("direnv")
    if direnv:
        envrc = WORKSPACE.parent / ".envrc"
        contents = envrc.read_text() if envrc.exists() else ""
        env_marker = "# >>> AirSpot command auto-load >>>"
        if env_marker not in contents:
            with envrc.open("a") as f:
                f.write('\n' + env_marker + '\nPATH_add "$HOME/.local/bin"\n'
                        'export AIRSPOT_ECOSYSTEM_ROOT=' + shlex.quote(str(WORKSPACE.parent)) + '\n'
                        '# <<< AirSpot command auto-load <<<\n')
        subprocess.run([direnv, "allow", str(envrc)], check=True)
        print("Installed airspot. With your direnv shell hook, entering airspothealth or any subfolder loads it automatically.")
    else:
        print("Installed airspot. Open a new terminal to pick up the global command.")
    print(f"Folder launcher: {targets[1]}")


if __name__ == "__main__":
    install()
