#!/bin/sh
# compat-render.sh - a test card for tests/compat.sh: colors, styles, box
# drawing and wide characters. Non-ASCII glyphs are octal UTF-8 escapes.

title() { printf '\n\033[1m%s\033[0m\n' "$1"; }

title 'truecolor: red, green, blue, grey ramps; no visible bands'
for ch in r g b w; do
    i=0
    while [ $i -lt 64 ]; do
        v=$((i * 4 + 3))
        case $ch in r) c="$v;0;0" ;; g) c="0;$v;0" ;; b) c="0;0;$v" ;; w) c="$v;$v;$v" ;; esac
        printf '\033[48;2;%sm ' "$c"
        i=$((i + 1))
    done
    printf '\033[0m\n'
done

title '256 colors: 16 system colors, then the 6x6x6 cube, then 24 greys'
i=0; while [ $i -lt 16 ]; do printf '\033[48;5;%dm   ' $i; i=$((i + 1)); done; printf '\033[0m\n'
i=16
while [ $i -lt 232 ]; do
    printf '\033[48;5;%dm  ' $i
    i=$((i + 1))
    [ $(((i - 16) % 36)) -eq 0 ] && printf '\033[0m\n'
done
i=232; while [ $i -lt 256 ]; do printf '\033[48;5;%dm  ' $i; i=$((i + 1)); done; printf '\033[0m\n'

title 'styles'
printf 'plain \033[1mbold\033[0m \033[2mfaint\033[0m \033[3mitalic\033[0m \033[4munderline\033[0m '
printf '\033[4:2mdouble\033[0m \033[4:3mcurly\033[0m \033[9mstrike\033[0m \033[53moverline\033[0m \033[7minverse\033[0m\n'
printf '\033[31mred \033[1mbold red\033[0m \033[38;5;208morange-256\033[0m \033[38;2;120;200;255mtruecolor-fg\033[0m '
printf '\033[8mhidden\033[0m <- hidden text, should be blank\n'

title 'box drawing: light, heavy, double, rounded; no gaps between rows'
printf '\342\224\214\342\224\200\342\224\200\342\224\200\342\224\200\342\224\254\342\224\200\342\224\200\342\224\200\342\224\200\342\224\220  \342\224\217\342\224\201\342\224\201\342\224\201\342\224\201\342\224\223  \342\225\224\342\225\220\342\225\220\342\225\220\342\225\220\342\225\227  \342\225\255\342\224\200\342\224\200\342\224\200\342\224\200\342\225\256\n'
printf '\342\224\202 ab \342\224\202 cd \342\224\202  \342\224\203 ab \342\224\203  \342\225\221 ab \342\225\221  \342\224\202 ab \342\224\202\n'
printf '\342\224\234\342\224\200\342\224\200\342\224\200\342\224\200\342\224\274\342\224\200\342\224\200\342\224\200\342\224\200\342\224\244  \342\224\203    \342\224\203  \342\225\221    \342\225\221  \342\224\202    \342\224\202\n'
printf '\342\224\202 ef \342\224\202 gh \342\224\202  \342\224\203 cd \342\224\203  \342\225\221 cd \342\225\221  \342\224\202 cd \342\224\202\n'
printf '\342\224\224\342\224\200\342\224\200\342\224\200\342\224\200\342\224\264\342\224\200\342\224\200\342\224\200\342\224\200\342\224\230  \342\224\227\342\224\201\342\224\201\342\224\201\342\224\201\342\224\233  \342\225\232\342\225\220\342\225\220\342\225\220\342\225\220\342\225\235  \342\225\260\342\224\200\342\224\200\342\224\200\342\224\200\342\225\257\n'

title 'blocks, shades, halves, quadrants'
printf '\342\226\221\342\226\221\342\226\222\342\226\222\342\226\223\342\226\223\342\226\210\342\226\210 \342\226\200\342\226\200\342\226\204\342\226\204 \342\226\214\342\226\220 \342\226\226\342\226\227\342\226\230\342\226\235\n'

title 'wide characters: the right | lines up in every row'
printf '|abcdefgh|  ASCII, 8 cells\n'
printf '|\346\227\245\346\234\254\350\252\236  |  CJK, 3 x 2 cells\n'
printf '|\360\237\231\202\360\237\232\200\342\230\225  |  emoji, 3 x 2 cells\n'
printf '|e\314\201a\314\200  ab  |  combining marks, 1 cell each\n'

title 'cursor: the next prompt should sit on a fresh line'
