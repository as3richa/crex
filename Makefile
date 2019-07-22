CFLAGS=-std=c99 -pedantic -Wall -Wextra -O3 -fPIC
LDFLAGS=-Lbuild -lcrex
SOURCES=crex.c
HEADERS=crex.h executor.h
LIBRARY=build/libcrex.so

SPEC_SOURCE=spec/main.c
SPEC_BIN=build/spec

TESTCASES_YML=$(shell echo spec/*.yml)
TESTCASES_SOURCE=build/testcases.c

.PHONY: all clean spec

all: $(LIBRARY)

$(LIBRARY): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -shared -o $@ $(SOURCES)

clean:
	rm -f $(LIBRARY) $(TESTCASES_SOURCE) $(SPEC_BIN)

spec: $(SPEC_BIN)
	LD_LIBRARY_PATH=build $(SPEC_BIN) $(CASES)

$(SPEC_BIN): $(LIBRARY) $(SPEC_SOURCE) $(TESTCASES_SOURCE)
	gcc $(LDFLAGS) $(CFLAGS) -o $@ $^

$(TESTCASES_SOURCE): $(TESTCASES_YML)
	spec/generate-testcases > $@
