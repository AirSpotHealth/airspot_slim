# AirSpot Development Environment
# This file is loaded when opening a new terminal in VS Code

# Source the user's default zshrc first
[[ -f ~/.zshrc ]] && source ~/.zshrc

# Get workspace folder (parent of .vscode directory)
AIRSPOT_WORKSPACE="$(cd "$(dirname "${(%):-%x}")/.." && pwd)"

# Source the AirSpot development tool
if [[ -f "$AIRSPOT_WORKSPACE/scripts/airspot" ]]; then
    source "$AIRSPOT_WORKSPACE/scripts/airspot"
    echo ""
    echo -e "\033[0;32m✓\033[0m AirSpot development environment loaded"
    echo -e "  Type \033[1;36mairspot help\033[0m for available commands"
    echo -e "  Type \033[1;36mairspot menu\033[0m for interactive menu"
    echo ""
fi

