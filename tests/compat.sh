#!/bin/sh
# compat.sh - run real programs in cui-repl, one window each, and record what
# the person at the keyboard sees. Not part of `make test`: it needs a person.
#
#   tests/compat.sh [-o FILE] [-l] [NAME...]
#
#   -o FILE  results file (default: build/compat-results.md)
#   -l       list the cases and exit
#   NAME     run only these cases
#
# Each case prints what to check, opens cui-repl, and asks pass, fail or skip
# once the window closes. Programs that are not installed are skipped.
# COMPAT_SSH_HOST=host enables the ssh case. CUI_REPL overrides the binary.
set -u

root=$(cd "$(dirname "$0")/.." && pwd)
bin=${CUI_REPL:-$root/build/cui-repl}
out=$root/build/compat-results.md
list=0

while getopts o:lh opt; do
    case $opt in
    o) out=$OPTARG ;;
    l) list=1 ;;
    h) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) exit 2 ;;
    esac
done
shift $((OPTIND - 1))
sel=$*
seen=

if [ "$list" = 0 ]; then
    if [ ! -x "$bin" ]; then
        echo "compat: no $bin; run make first" >&2
        exit 1
    fi
    tmp=$(mktemp -d "${TMPDIR:-/tmp}/cui-compat.XXXXXX") || exit 1
    trap 'rm -rf "$tmp"; tmux -L cui-compat kill-server 2>/dev/null' EXIT
    trap 'exit 130' INT TERM
    cp "$root/src/term_view.c" "$tmp/sample.c" # an editable copy for the editors
    mkdir -p "$(dirname "$out")"
    {
        echo "# cui-repl compatibility, $(date '+%Y-%m-%d %H:%M')"
        echo
        echo "cui-repl $(git -C "$root" rev-parse --short HEAD 2>/dev/null || echo '?'), $(uname -sm)"
        echo
        echo "| Case | Result | Notes |"
        echo "|-|-|-|"
    } >"$out"
else
    tmp=/tmp
fi

# Keeps a window open after its command ends, so the final screen can be read.
hold='; printf "\n\033[7m[exit %d] Enter closes the window\033[0m" $?; read _'

record() { # name result note
    printf '| %s | %s | %s |\n' "$1" "$2" "$(printf '%s' "$3" | sed 's/|/\\|/g')" >>"$out"
}

ask() { # prompt; the answer goes to $answer
    printf '%s' "$1"
    read -r answer </dev/tty || exit 1
}

# case NAME NEEDS COMMAND CHECKS: CHECKS separates items with ";".
case_() {
    name=$1 needs=$2 cmd=$3 checks=$4
    seen="$seen $name"
    if [ "$list" = 1 ]; then echo "$name"; return; fi
    if [ -n "$sel" ]; then
        case " $sel " in *" $name "*) ;; *) return ;; esac
    fi
    if ! command -v "$needs" >/dev/null 2>&1; then
        echo "-- $name: skipped, $needs not installed"
        record "$name" skip "$needs not installed"
        return
    fi
    if [ "$name" = ssh ] && [ -z "${COMPAT_SSH_HOST:-}" ]; then
        echo "-- $name: skipped, COMPAT_SSH_HOST not set"
        record "$name" skip "COMPAT_SSH_HOST not set"
        return
    fi
    printf '\n\033[1m== %s\033[0m\n' "$name"
    printf '%s\n' "$checks" | tr ';' '\n' | sed 's/^ */  - /'
    ask 'Enter opens the window, s skips: '
    if [ "$answer" = s ]; then
        record "$name" skip "skipped by tester"
        return
    fi
    "$bin" sh -c "$cmd"
    code=$?
    while :; do
        ask "$name: [p]ass, [f]ail, [s]kip? "
        case $answer in
        p*) result=pass; break ;;
        f*) result=fail; break ;;
        s*) result=skip; break ;;
        esac
    done
    note=
    if [ "$result" != pass ]; then
        ask 'what went wrong (one line): '
        note=$answer
    fi
    [ "$code" -ne 0 ] && note="${note:+$note; }exit status $code"
    record "$name" "$result" "$note"
}

case_ render sh "sh '$root/tests/compat-render.sh'$hold" \
    "truecolor gradient is smooth, no bands; the 256-color cube shows 6 distinct steps per channel; bold, faint, italic, underline, double underline, strikethrough and inverse each look different (italic draws upright: known); box drawing joins between rows with no gaps; CJK and emoji take 2 cells and the right border lines up"

case_ keys cat "cat -v" \
    "typing shows as typed; Ctrl+[ shows ^[ (CUI has no code for [: expect a fail); Alt+x shows ^[x off macOS, a composed character on macOS; F5 and the arrows show escape sequences; Ctrl+C quits"

case_ vim vim "vim -u DEFAULTS -c 'set mouse=a' -c 'syntax on' '$tmp/sample.c'" \
    "syntax colors are right; :set bg? reports background=dark (the OSC 11 reply); insert mode changes the cursor shape; a click moves the cursor and the wheel scrolls; Shift+drag selects and Cmd+C copies; Esc and Ctrl+[ leave insert mode"

case_ nvim nvim "nvim --clean -c 'set mouse=a' '$tmp/sample.c'" \
    "syntax colors in truecolor; :set bg? reports background=dark; insert mode changes the cursor shape; click, drag and wheel work; :checkhealth shows no terminal errors"

case_ tmux tmux "tmux -L cui-compat -f /dev/null new-session sh \\; set -g mouse on \\; split-window -h \\; split-window -v" \
    "pane borders draw without gaps; Ctrl+b o moves between panes; a click focuses a pane; dragging a border resizes it; the wheel scrolls a pane's history; Ctrl+b d detaches"

case_ htop htop "htop" \
    "meters and colors draw; a click on a column header sorts by it; the F-keys work; the wheel scrolls the process list; q quits"

case_ btop btop "btop" \
    "braille graphs and boxes draw cleanly; clicks on the menu work; redraws do not flicker; q quits"

case_ less less "less '$root/src/term_view.c'" \
    "Space and b page; /paint finds and highlights matches; the wheel scrolls; q restores the screen as it was before less"

case_ git-log git "git -C '$root' log --graph --stat -p --color=always" \
    "the diff is colored; the pager scrolls; q returns to a clean screen"

case_ fzf fzf "find '$root' -path '$root/build' -prune -o -type f -print | fzf --preview 'head -40 {}'$hold" \
    "the list filters as you type; arrows and Ctrl+n / Ctrl+p move; the preview pane draws beside the list; a click selects an item; Enter prints it, Esc cancels"

case_ lazygit lazygit "cd '$root' && lazygit" \
    "panels draw with correct borders and colors; clicks switch panels; the wheel scrolls; q quits"

case_ throughput seq "s=\$(date +%s); seq 1 2000000; echo \"seconds: \$((\$(date +%s) - s))\"$hold" \
    "the 2 million lines finish in a few seconds; the window stays responsive while they scroll; the last line is 2000000"

case_ interrupt yes "yes" \
    "output floods the window; Ctrl+C stops it within about a second; the window then closes"

case_ ssh ssh "ssh -t '${COMPAT_SSH_HOST:-}'" \
    "the login works; echo \$TERM shows xterm-256color; vim or htop on the remote host work; Ctrl+D ends the session"

[ "$list" = 1 ] && exit 0
for w in $sel; do
    case "$seen " in *" $w "*) ;; *) echo "compat: no case named $w (tests/compat.sh -l lists them)" >&2 ;; esac
done
echo
echo "results: $out"
echo "failures: $(grep -c '| fail |' "$out")"
