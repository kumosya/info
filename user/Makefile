# Standalone entry point; compilation rules are shared with the root build.
.DEFAULT_GOAL := all
.PHONY: all applications services libraries check

all:
	+$(MAKE) -C .. USER_SUBBUILD=1 applications services $(if $(CROSS),CROSS=$(CROSS))

applications services libraries check:
	+$(MAKE) -C .. USER_SUBBUILD=1 $@ $(if $(CROSS),CROSS=$(CROSS))
