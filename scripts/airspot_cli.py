#!/usr/bin/env python3
"""One entry point for AirSpot device monitoring and existing project helpers."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

WORKSPACE = Path(__file__).resolve().parents[3]
ECOSYSTEM = Path(os.environ.get("AIRSPOT_ECOSYSTEM_ROOT", WORKSPACE.parent))
LIVE = Path(__file__).resolve().parent / "airspot-live"
PROJECTS = {
    "slim": (WORKSPACE / "firmware/airspot_slim", "nordic", "scripts/airspot-production"),
    "screen": (WORKSPACE / "firmware/airspot_screen", "nordic", "scripts/airspot-screen"),
    "hub": (ECOSYSTEM / "airspothub/airspot", "hub", "scripts/airspot"),
    "hub-10": (ECOSYSTEM / "airspothub-10in/airspot", "hub", "scripts/airspot"),
    "server": (ECOSYSTEM / "airspot_server", "web", None),
    "map": (ECOSYSTEM / "airspot-map", "web", None),
    "dashboard": (ECOSYSTEM / "airspot-dashboard", "web", None),
    "hub-web": (ECOSYSTEM / "airspot-hub-web", "web", None),
    "hub-web-ui": (ECOSYSTEM / "airspot-hub-web-ui", "web", None),
    "mobile": (WORKSPACE / "mobile", "flutter", None),
}


def help_text():
    return """AirSpot — devices and projects

  airspot                       Interactive component menu
  airspot scan                  Find nearby BLE devices
  airspot cli                   Coloured scan / select / connect menu
  airspot connect               Alias for the interactive device menu
  airspot watch [ID ...]         Direct live monitor (omit IDs for all devices)
  airspot watch --json           Stream JSONL for scripts or logging
  airspot setup                 Install the BLE monitor dependencies
  airspot install               Install the command for your zsh terminals
  airspot <component>            Open that component's action menu
  airspot <component> <action>   Run an existing project action
  airspot <component> path       Show its project directory

Components: slim, screen, hub, hub-10, server, map, dashboard,
            hub-web, hub-web-ui, mobile

Examples:
  airspot slim connect           Monitor Slims only
  airspot screen connect         Monitor Screens only
  airspot watch BE56FF 81A9DC     Monitor selected devices directly
  airspot slim build              Existing Nordic firmware build
  airspot screen logs             Existing J-Link logs
  airspot hub-10 ports            Existing Hub serial-port helper
  airspot server dev              Project's package.json dev script

BLE monitoring does not change sampling or calibration settings.
Disconnect devices from the phone app before connecting from the Mac.
"""


def web_actions(path):
    return json.loads((path / "package.json").read_text()).get("scripts", {})


def action_names(component):
    path, kind, _helper = PROJECTS[component]
    if kind == "web":
        return ["path", "shell", *web_actions(path)]
    if kind == "flutter":
        return ["path", "shell", "run", "test", "analyze", "doctor", "build"]
    if component == "screen":
        return ["connect", "scan", "path", "shell", "build", "flash", "logs", "memory",
                "ram-top", "bf", "bfl", "reset", "clean", "help"]
    if kind == "nordic":
        return ["connect", "scan", "path", "shell", "build", "flash", "logs", "devices",
                "ports", "version", "check", "bf", "bfl", "reset", "erase", "help"]
    return ["path", "shell", "build", "flash", "logs", "ports", "bf", "bfl", "clean", "help"]


def invocation(component, args):
    """Build argv without shell interpolation; never invent a project script."""
    path, kind, helper = PROJECTS[component]
    action, *rest = args
    if not path.is_dir():
        raise ValueError(f"Project missing: {path}")
    if action == "shell":
        return [os.environ.get("SHELL", "/bin/zsh")], path
    if kind == "nordic":
        if action in ("connect", "watch", "scan"):
            if action == "scan":
                return [str(LIVE), "scan", *rest], WORKSPACE
            return [str(LIVE), "watch", "--type", component, *rest], WORKSPACE
        return ["bash", str(path / helper), *args], path
    if kind == "hub":
        return ["bash", "-c", 'source "$1"; shift; airspot "$@"', "airspot-hub",
                str(path / helper), *args], path
    if kind == "web":
        if action not in web_actions(path):
            raise ValueError(f"No '{action}' script in {path / 'package.json'}")
        manifest = json.loads((path / "package.json").read_text())
        manager = manifest.get("packageManager", "").split("@")[0]
        if not manager:
            manager = "pnpm" if (path / "pnpm-lock.yaml").exists() else "npm"
        if manager not in ("npm", "pnpm", "yarn", "bun"):
            raise ValueError(f"Unsupported package manager: {manager}")
        options = (["--"] if manager == "npm" and rest else []) + rest
        return [manager, "run", action, *options], path
    if kind == "flutter":
        if action not in action_names(component):
            raise ValueError(f"Unknown Flutter action: {action}")
        command = ["flutter", *args]
        if action == "run" and not any(a.startswith("--dart-define-from-file") for a in rest):
            if (path / ".env").is_file():
                command.append("--dart-define-from-file=.env")
        return command, path
    raise ValueError(f"Unsupported component: {component}")


def execute(component, args):
    if component in ("slim", "screen") and args[0] in ("cli", "connect"):
        return device_menu(component)
    if args[0] == "path":
        print(PROJECTS[component][0])
        return 0
    if args[0] in ("help", "--help", "-h"):
        print(f"AirSpot {component}\nProject: {PROJECTS[component][0]}\nActions: " + ", ".join(action_names(component)))
        return 0
    argv, cwd = invocation(component, args)
    if not shutil.which(argv[0]):
        raise ValueError(f"'{argv[0]}' is not installed or not on PATH")
    return subprocess.call(argv, cwd=cwd)


def colour(text, code):
    return f"\033[{code}m{text}\033[0m" if sys.stdout.isatty() and "NO_COLOR" not in os.environ else text


def device_menu(device_type=None):
    if not sys.stdin.isatty():
        print("Open 'airspot cli' in a terminal. For scripts use 'airspot watch <IDs>'.")
        return 2
    args = [str(LIVE), "cli"]
    if device_type:
        args += ["--type", device_type]
    return subprocess.call(args)


def choose(title, options):
    print(colour(f"\n{title}", "1;36"))
    for index, option in enumerate(options, 1):
        print(colour(f"  {index:2}", "36") + f"  {option}")
    print("   0  Back/exit")
    try:
        value = input("Choose a number or name: ").strip()
    except EOFError:
        return None
    if value.lower() in ("0", "q", "quit", "exit"):
        return None
    if value in options:
        return value
    if value.isdigit() and 1 <= int(value) <= len(options):
        return options[int(value) - 1]
    print("Choose an option from the list.")
    return ""


def menu(component=None):
    if not sys.stdin.isatty():
        if component:
            return execute(component, ["help"])
        print(help_text())
        return 0
    while True:
        if component:
            selected = choose(f"AirSpot {component}", action_names(component))
            if selected is None:
                return 0
            if selected:
                try:
                    execute(component, [selected])
                except KeyboardInterrupt:
                    print("\nStopped.")
                except (ValueError, OSError) as error:
                    print(error, file=sys.stderr)
        else:
            selected = choose("AirSpot", ["cli", *PROJECTS])
            if selected is None:
                return 0
            if selected:
                try:
                    main([selected])
                except KeyboardInterrupt:
                    print("\nStopped.")


def main(argv=None):
    args = sys.argv[1:] if argv is None else argv
    if not args:
        return menu()
    command, *rest = args
    if command in ("help", "--help", "-h"):
        print(help_text())
        return 0
    if command == "install":
        return subprocess.call([sys.executable, str(Path(__file__).with_name("install_airspot_cli.py"))])
    if command in ("cli", "connect"):
        return device_menu()
    if command in ("watch", "scan", "setup"):
        return subprocess.call([str(LIVE), command, *rest])
    if command not in PROJECTS:
        print(f"Unknown component: {command}\nRun 'airspot help'.", file=sys.stderr)
        return 2
    return execute(command, rest) if rest else menu(command)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(0)
    except (ValueError, OSError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
