gcc -o crextest crex.c main.c -Wall -Wextra -std=c99 -pedantic -g
# clang -o crextest crex.c main.c -Wall -Wextra -std=c99 -pedantic -DCREX_DEBUG -g -fsanitize=address
