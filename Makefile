##############################################################################
# nts1-mkii-lab
#
#   make            build every unit into dist/
#   make test       build and run the offline render tests (writes dist/test)
#   make clean      wipe build artefacts
#   make poly8      build a single unit
#

# A unit is any directory under units/ holding a config.mk, at whatever depth —
# units/<module>/<name>, or units/<module>/<family>/<name> for a family that
# shares an engine. The build target is the unit's PROJECT, so `make poly8`
# produces dist/poly8.nts1mkiiunit and the two always match. One shell call
# reads them all: PROJECT is unique across the tree because the artefacts it
# names have to be.
UNIT_MKS := $(wildcard units/*/*/config.mk units/*/*/*/config.mk)
UNIT_MAP := $(shell for f in $(UNIT_MKS); do \
                p=$$(sed -n 's/^PROJECT[[:space:]]*:=[[:space:]]*//p' $$f); \
                echo "$$p|$$(dirname $$f)"; done)
UNITS    := $(foreach m,$(UNIT_MAP),$(word 1,$(subst |, ,$(m))))
$(foreach m,$(UNIT_MAP),$(eval UNIT_PATH_$(word 1,$(subst |, ,$(m))) := $(word 2,$(subst |, ,$(m)))))

.PHONY: all test clean $(UNITS)

all: $(UNITS)

$(UNITS):
	@$(MAKE) --no-print-directory -C $(UNIT_PATH_$@) install

clean:
	@for d in $(foreach m,$(UNIT_MAP),$(word 2,$(subst |, ,$(m)))); do \
		$(MAKE) --no-print-directory -C $$d clean; done
	@rm -rf dist

##############################################################################
# Offline tests — plain host compiler, no cross toolchain needed
#

TEST_BIN := dist/test/render
TEST_FLAGS := -std=c++14 -O2 -Wall -Wextra -Icommon

test: lint $(TEST_BIN)
	@./$(TEST_BIN)

.PHONY: lint
lint:
	@echo "unit header descriptors"
	@python3 scripts/lint_headers.py

$(TEST_BIN): tests/render.cc $(wildcard common/*.h) \
             $(wildcard units/*/*/dsp.h) $(wildcard units/*/*/*.h) \
             $(wildcard units/*/*/*/dsp.h)
	@mkdir -p dist/test
	@$(CXX) $(TEST_FLAGS) tests/render.cc -o $@
