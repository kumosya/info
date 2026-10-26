CPP ?= g++
OUTPUT ?= ../osinfo-host
CPPFLAGS :=
CPPFLAGS := -std=c++20 -O2 -Wall -Wextra -Werror
SOURCES := $(wildcard *.cc)
.PHONY: all
all: $(OUTPUT)
$(OUTPUT): $(SOURCES) $(wildcard *.h) Makefile
	@mkdir -p "$(@D)"
	@set -eu; temporary="$@.tmp.$$$$"; trap 'rm -f "$$temporary"' EXIT; \
		$(CPP) $(CPPFLAGS) $(CPPFLAGS) $(SOURCES) -o "$$temporary"; mv "$$temporary" "$@"
