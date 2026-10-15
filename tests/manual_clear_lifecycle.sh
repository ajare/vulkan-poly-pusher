#!/usr/bin/env bash
# Human-in-the-loop check: never synthesize input or bypass desktop activation policy.
# Usage: bash tests/manual_clear_lifecycle.sh [x11|wayland] [build-directory]
set -euo pipefail
driver="${1:-wayland}"
build="${2:-build}"
case "$driver" in x11|wayland) ;; *) echo 'Expected x11 or wayland' >&2; exit 2;; esac
output="$build/manual-lifecycle-$driver-$(date +%Y%m%d-%H%M%S)"
step() {
    printf '\n>>> %s\n' "$1"
    read -r -p '    [Enter when ready] ' _
}
capture() {
    local var="$1" question="$2" answer
    printf '\n>>> %s\n' "$question"
    read -r -p '    > ' answer
    printf -v "$var" '%s' "$answer"
}
step 'Keep the terminal visible. Follow each ACTION prompt: minimize using the window menu, then restore via dock/overview/window switcher. Repeat three times; each action has 30 seconds. Do not pause the process.'
status=0
SDL_VIDEODRIVER="$driver" python3 tests/run_clear_validation.py \
    "$build/bin/VppClearPresent" "$output.ppm" --manual-minimize || status=$?
capture OBSERVATION 'Did it remain hidden, reappear without rendering, or recover? Record immediate/delayed restores and any unexpected behavior.'
capture DESKTOP 'Record compositor/desktop and whether X11 was native or Xwayland.'
printf 'STATUS=%s\nDESKTOP=%s\nOBSERVATION=%s\n' "$status" "$DESKTOP" "$OBSERVATION" | tee "$output.observation.txt"
exit "$status"
