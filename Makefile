# FIXME: figure out cross-platform story
CFLAGS := $(CFLAGS) -std=c99 -pedantic -Wall -Wextra -fPIC -Iinclude -D_GNU_SOURCE

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

$(shell mkdir -p build/src/tools bin/{tools,engine-tests} build/engine-tests/{execution-engines,generators,harnesses})

LIBRARY_OBJECT := build/src/crex.o
DYNAMIC_LIBRARY := libcrex.so.1
STATIC_LIBRARY := libcrex.a

X64_ASSEMBLER := build/x64.h

ENGINE_TEST_FRAMEWORK_OBJECTS := $(patsubst %.c,build/%.o,$(wildcard engine-tests/*.c))
ENGINE_TEST_EXECUTION_ENGINE_OBJECTS := $(patsubst %.c,build/%.o,$(wildcard engine-tests/execution-engines/*.c))
ENGINE_TEST_FRAMEWORK_STATIC_LIBRARY := build/engine-tests/framework.a

ENGINE_TEST_GENERATORS := $(patsubst %.c,build/%.o,$(wildcard engine-tests/generators/*.c))
ENGINE_TEST_SUITES := $(patsubst engine-tests/generators/%.c,bin/engine-tests/%.bin,$(wildcard engine-tests/generators/*.c))

ENGINE_TEST_HARNESSES := $(patsubst engine-tests/harnesses/%.c,bin/engine-tests/%,$(wildcard engine-tests/harnesses/*.c))

TOOLS := bin/tools/crexamine

DEPENDENCY_FILES := $(shell find build -name "*.in")

AR ?= ar
STRIP ?= strip

.PHONY: all tools engine-tests clean
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

engine-tests: $(ENGINE_TEST_SUITES) $(ENGINE_TEST_HARNESSES)

$(ENGINE_TEST_FRAMEWORK_STATIC_LIBRARY): $(ENGINE_TEST_FRAMEWORK_OBJECTS) $(ENGINE_TEST_EXECUTION_ENGINE_OBJECTS)
	$(AR) rcs $@ $^

build/engine-tests/generators/%: build/engine-tests/generators/%.o $(ENGINE_TEST_FRAMEWORK_STATIC_LIBRARY)
	$(CC) $(CFLAGS) -o $@ $^ 

bin/engine-tests/%.bin: build/engine-tests/generators/%
	$^ $@

bin/engine-tests/%: build/engine-tests/harnesses/%.o $(ENGINE_TEST_FRAMEWORK_STATIC_LIBRARY) $(STATIC_LIBRARY)
	$(CC) $(CFLAGS) $(shell pkg-config --cflags --libs libpcre2-8) -o $@ $^ 

clean:
	rm -rf build bin $(STATIC_LIBRARY) $(DYNAMIC_LIBRARY)

include $(DEPENDENCY_FILES)
