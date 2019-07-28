CFLAGS := $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -fPIC

ifeq ($(ENV),development)
	CFLAGS := $(CFLAGS) -g -O0
else
	CFLAGS := $(CFLAGS) -O3 -DNDEBUG
endif

LIBRARY_SOURCE := crex.c
LIBRARY_HEADERS := crex.h executor.h
LIBRARY_OBJECT := build/crex.o
DYNAMIC_LIBRARY := libcrex.so
STATIC_LIBRARY := libcrex.a

EXAMINER_SOURCE := crexamine.c
EXAMINER_OBJECT := build/crexamine.o
EXAMINER_BINARY := bin/crexamine

DUMPER_SOURCE := crexdump.c
DUMPER_OBJECT := build/crexdump.o
DUMPER_BINARY := bin/crexdump

ENGINE_TEST_SOURCE := test-engine.c
ENGINE_TEST_OBJECT := build/test-engine.o
ENGINE_TEST_BINARY := bin/test-engine
ENGINE_TEST_TESTCASES := build/engine-testcases.h

DEP_FILES := $(shell find build -name "*.in")

AR ?= ar

.PHONY: all clean test

.PRECIOUS: build/%.o bin/%

all: $(STATIC_LIBRARY) $(DYNAMIC_LIBRARY)

tools: $(EXAMINER_BINARY) $(DUMPER_BINARY)

test: $(ENGINE_TEST_BINARY)
	$(ENGINE_TEST_BINARY)

$(STATIC_LIBRARY): $(LIBRARY_OBJECT)
	$(AR) rcs $@ $^

$(DYNAMIC_LIBRARY): $(LIBRARY_OBJECT)
	$(CC) $(CFLAGS) -shared -o $@ $^

build/%.o: %.c
	$(CC) -MMD -MF $(@:.o=.in) $(CFLAGS) -c -o $@ $<

bin/%: build/%.o $(STATIC_LIBRARY)
	$(CC) $(CFLAGS) -o $@ $^

$(ENGINE_TEST_OBJECT): $(ENGINE_TEST_SOURCE) $(ENGINE_TEST_TESTCASES)
	$(CC) -MMD -MF $(@:.o=.in) $(CFLAGS) -c -o $@ $<

$(ENGINE_TEST_TESTCASES):
	test/engine/generate-testcases $@ $(@:.h=.in)

clean:
	rm -f $(LIBRARY_OBJECT) $(DYNAMIC_LIBRARY) $(STATIC_LIBRARY)
	rm -f $(EXAMINER_OBJECT) $(EXAMINER_BINARY)
	rm -f $(DUMPER_OBJECT) $(DUMPER_BINARY)
	rm -f $(ENGINE_TEST_TESTCASES) $(ENGINE_TEST_OBJECT) $(ENGINE_TEST_BINARY)
	rm -f $(DEP_FILES)

include $(DEP_FILES)
