# Standalone entry point; compilation rules are shared with the root build.
.DEFAULT_GOAL := all
.PHONY: all applications services libraries check

all:
	+$(MAKE) -C .. applications services $(if $(CROSS),CROSS=$(CROSS))

applications services libraries check:
	+$(MAKE) -C .. $@ $(if $(CROSS),CROSS=$(CROSS))
