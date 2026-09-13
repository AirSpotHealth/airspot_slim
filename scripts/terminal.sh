# Terminal presentation shared by help and the interactive menu.
bold='' cyan='' dim='' reset=''
if [[ -t 1 && -z "${NO_COLOR:-}" && "${TERM:-}" != dumb ]]; then
    bold=$'\033[1m' cyan=$'\033[36m' dim=$'\033[2m' reset=$'\033[0m'
fi
heading() { printf '\n%s%s%s\n' "$bold$cyan" "$1" "$reset"; }
row() { printf '  %s%-26s%s %s\n' "$cyan" "$1" "$reset" "$2"; }
firmware_version() {
    awk '/^VERSION_MAJOR / {major=$3} /^VERSION_MINOR / {minor=$3} /^PATCHLEVEL / {patch=$3}
         END {printf "v%s.%s.%s\n", major, minor, patch}' "$AIRSPOT_PROJECT_DIR/VERSION"
}
show_help() {
    heading "AIRSPOT  /  Slim $(firmware_version)"
    printf '%s%s%s\n' "$dim" '────────────────────────────────────────────────────────────' "$reset"
    heading 'BUILD & FLASH'
    row 'build [--clean]' 'Build signed firmware with Bluetooth OTA and RTT'
    row 'check' 'Verify production build artifacts'
    row 'flash' 'Install firmware; back up and preserve settings bytes'
    row 'provision' 'Alias for flash'
    row 'bf / bfl' 'Build + flash / build + flash + live logs'
    heading 'DEVICE'
    row 'devices / info' 'List connected J-Link probes and workspace details'
    row 'reset' 'Restart the selected device'
    row 'erase [-y]' 'Erase internal firmware and settings (confirms first)'
    heading 'MONITOR & INSPECT'
    row 'logs / monitor' 'Stream RTT here; Ctrl-C stops'
    row 'ports' 'List serial ports'
    row 'memory' 'Show firmware memory usage'
    row 'version' 'Show firmware version'
    heading 'WORKSPACE'
    row 'clean' 'Remove the production build directory'
    row 'menu' 'Open the interactive command menu'
    row 'help' 'Show this command guide'
    printf '\n  Usage: airspot <command> [options]\n'
    printf '  Probe selection is automatic; use --serial SERIAL to choose explicitly.\n'
    printf '  Flash options: --reset-settings | --settings-source ADDRESS\n'
    printf '  Default: preserve settings bytes at 0x15c000; no old-firmware check.\n'
    printf '  Example: airspot bfl --clean\n\n'
}
show_menu() {
    [[ -t 0 ]] || { echo 'Interactive menu requires a terminal; use airspot help.' >&2; return 1; }
    local choice action
    while true; do
        heading "AIRSPOT  /  Slim $(firmware_version)"
        row '1  Build' '2  Flash                 3  Build + flash'
        row '4  Build + flash + logs' '5  Live logs             6  Reset'
        row '7  Erase' '8  Device info           9  Clean build'
        row 'v  Version' 'm  Memory                h  Help'
        row '0  Exit' ''
        printf '\n  Select command: '
        read -r choice || return 0
        case "$choice" in
            1) action=build ;; 2) action=flash ;; 3) action=bf ;;
            4) action=bfl ;; 5) action=logs ;; 6) action=reset ;;
            7) action=erase ;; 8) action=info ;; 9) action=clean ;;
            v) action=version ;; m) action=memory ;; h) action=help ;;
            0|q) return 0 ;; *) echo 'Choose an option from the menu.'; continue ;;
        esac
        if bash "$script_dir/airspot-production" "$action"; then :
        else printf '\nCommand stopped or failed.\n'; fi
    done
}
