CFLAGS := $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -fPIC -Iinclude

ifeq ($(ENV),development)
	CFLAGS := $(CFLAGS) -g -O0
else
	CFLAGS := $(CFLAGS) -O3 -DNDEBUG

	ifeq ($(shell uname),Darwin)
		LDFLAGS := $(LDFLAGS) -Wl,-x
	else
		LDFLAGS := $(LDFLAGS) -Wl,-x,-s,--version-script=libcrex.version
	endif
endif

$(shell mkdir -p build/src/tools bin/{tools,engine-tests} build/engine-tests/{framework,generators,harnesses})

LIBRARY_OBJECT := build/src/crex.o
DYNAMIC_LIBRARY := libcrex.so.1
STATIC_LIBRARY := libcrex.a

X64_ASSEMBLER := build/x64.h

ENGINE_TEST_HARNESSES := $(addprefix bin/engine-tests/,$(basename $(notdir $(wildcard engine-tests/harnesses/*.c))))
ENGINE_TEST_GENERATORS := $(addprefix build/engine-tests/generators/,$(basename $(notdir $(wildcard engine-tests/generators/*.c))))
ENGINE_TEST_SUITES := $(addprefix bin/engine-tests/,$(addsuffix .bin,$(basename $(notdir $(wildcard engine-tests/generators/*.c)))))

TOOLS := bin/tools/crexamine

DEPENDENCY_FILES := $(shell find build -name "*.in")

AR ?= ar
STRIP ?= strip

.PHONY: all tools engine-tests run-engine-tests clean
.SECONDARY:

all: $(STATIC_LIBRARY) $(DYNAMIC_LIBRARY)

tools: $(TOOLS)

$(STATIC_LIBRARY): $(LIBRARY_OBJECT)
	$(AR) rcs $@ $^
ifneq ($(ENV),development)
	$(STRIP) -x $@
endif

$(DYNAMIC_LIBRARY): $(LIBRARY_OBJECT)
	$(CC) $(LDFLAGS) $(CFLAGS) -shared -o $@ $^

ifeq ($(shell uname -m),x86_64)
$(LIBRARY_OBJECT): $(X64_ASSEMBLER)

$(X64_ASSEMBLER):
	x64/generate-assembler $@ $(@:.h=.in)
endif

build/%.o: %.c
	$(CC) -MMD -MF $(@:.o=.in) $(CFLAGS) -c -o $@ $<

bin/tools/%: build/src/tools/%.o $(STATIC_LIBRARY)
	$(CC) $(CFLAGS) -o $@ $^

engine-tests: $(ENGINE_TEST_HARNESSES) $(ENGINE_TEST_SUITES)

bin/engine-tests/%: build/engine-tests/harnesses/%.o build/engine-tests/framework/test-harness.o build/engine-tests/framework/harnesses.o $(STATIC_LIBRARY)
	$(CC) $(CFLAGS) -o $@ $^

bin/engine-tests/%.bin: build/engine-tests/generators/%
	$^ $@

build/engine-tests/generators/%: build/engine-tests/generators/%.o build/engine-tests/framework/builder.o
	$(CC) $(CFLAGS) -o $@ $^ 

run-engine-tests: bin/engine-tests/crex-correctness-default $(ENGINE_TEST_SUITES)
	bin/engine-tests/crex-correctness-default $(ENGINE_TEST_SUITES)

clean:
	rm -rf build bin $(STATIC_LIBRARY) $(DYNAMIC_LIBRARY)

include $(DEPENDENCY_FILES)
