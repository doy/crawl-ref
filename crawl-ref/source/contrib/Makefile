PREFIX := install

SUBDIRS = lua/src sqlite sdl sdl-image freetype libpng pcre zlib

all:
	@$(MAKE) $(SUBDIRS)

clean distclean:
	@for a in $(SUBDIRS); do \
	    $(MAKE) -C $$a distclean; \
	done
	rm -rf $(PREFIX)

$(SUBDIRS):
	@if [ ! -x $@ ]; then \
	    echo "**********************************************************"; \
	    echo; \
	    echo "The '$@' folder is missing. Have you run"; \
	    echo "   'git submodule update --init'?"; \
	    echo; \
	    echo "**********************************************************"; \
	    exit 1; \
	fi
	@if [ ! -f $@/Makefile ] && [ ! -f $@/makefile ]; then \
	    echo "**********************************************************"; \
	    echo; \
	    echo "The '$@' folder exists, but the Makefile is missing!"; \
	    echo "Did 'git submodule update' work properly?"; \
	    echo; \
	    echo "**********************************************************"; \
	    exit 1; \
	fi
	@cd $@ && \
	$(MAKE) all prefix=$(shell echo $@ | perl -pe 's/[^\/]+/../g')/$(PREFIX) && \
	$(MAKE) install prefix=$(shell echo $@ | perl -pe 's/[^\/]+/../g')/$(PREFIX)

libpng freetype: zlib
sdl-image: sdl libpng

.PHONY: $(SUBDIRS)
