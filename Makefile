# cwal - Makefile
#
# Plain Makefile, no autoconf, no cmake. Builds the `cwal` binary.
#
#   make          -> build
#   make clean    -> remove build artifacts
#   make install  -> install binary + templates
#   make uninstall
#   make test     -> run a quick smoke test against the bundled image

CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Wno-format-truncation -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
LDFLAGS ?= -lm

# Install path for the built-in templates. Override with
#   make TEMPLATE_DIR=/your/path
# At runtime, $CWAL_TEMPLATES_DIR env var beats this, and ./templates
# in the cwd beats both (handy for running from the source tree).
TEMPLATE_DIR ?= $(PREFIX)/share/cwal/templates

SRC := src/main.c src/color.c src/image.c src/quantize.c \
       src/palette.c src/cache.c src/sequences.c src/templates.c
HDR := src/cwal.h vendor/stb_image.h

TPL := $(wildcard templates/*)

BIN := cwal
PREFIX ?= /usr/local

.PHONY: all clean install uninstall test

all: $(BIN)

$(BIN): $(SRC) $(HDR)
	$(CC) $(CFLAGS) -DCWAL_TEMPLATE_DIR=\"$(TEMPLATE_DIR)\" -Isrc -Ivendor -o $@ $(SRC) $(LDFLAGS)

clean:
	rm -f $(BIN)

install: $(BIN)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 $(BIN) $(DESTDIR)$(PREFIX)/bin/
	install -d $(DESTDIR)$(TEMPLATE_DIR)
	for t in $(TPL); do \
		install -m 0644 $$t $(DESTDIR)$(TEMPLATE_DIR)/; \
	done

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)
	rm -rf $(DESTDIR)$(TEMPLATE_DIR)

test: $(BIN)
	./$(BIN) -v
	./$(BIN) -i test/sample.jpg -q
