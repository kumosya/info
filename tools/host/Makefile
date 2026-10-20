CXX ?= g++
OUTPUT ?= ../osinfo-host
CPPFLAGS :=
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Werror
SOURCES := $(wildcard *.cc)
.PHONY: all
all: $(OUTPUT)
$(OUTPUT): $(SOURCES) $(wildcard *.h) Makefile
	@mkdir -p "$(@D)"
	@set -eu; temporary="$@.tmp.$$$$"; trap 'rm -f "$$temporary"' EXIT; \
		$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCES) -o "$$temporary"; mv "$$temporary" "$@"
