#gcc -o crextest crex.c main.c -Wall -Wextra -std=c99 -pedantic -g -DCREX_DEBUG
clang -o crextest crex.c main.c -Wall -Wextra -std=c99 -pedantic -DCREX_DEBUG -g -fsanitize=undefined
