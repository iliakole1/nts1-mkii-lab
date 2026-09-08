##############################################################################
# nts1-mkii-lab
#
#   make            build every unit into dist/
#   make test       build and run the offline render tests (writes dist/test)
#   make clean      wipe build artefacts
#   make UNIT=poly8 unit    build a single unit
#

# A unit is any directory under units/ holding a config.mk, at either depth —
# units/<name> or units/<family>/<name>. The target name flattens the path, so
# units/casio/piano builds with `make casio-piano` and cannot collide with the
# top-level `piano`.
UNIT_DIRS := $(patsubst units/%/,%,$(dir $(wildcard units/*/config.mk units/*/*/config.mk)))
UNITS     := $(subst /,-,$(UNIT_DIRS))
$(foreach d,$(UNIT_DIRS),$(eval UNIT_PATH_$(subst /,-,$(d)) := $(d)))

.PHONY: all test clean $(UNITS)

all: $(UNITS)

$(UNITS):
	@$(MAKE) --no-print-directory -C units/$(UNIT_PATH_$@) install

clean:
	@for d in $(UNIT_DIRS); do $(MAKE) --no-print-directory -C units/$$d clean; done
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

$(TEST_BIN): tests/render.cc $(wildcard common/*.h) $(wildcard units/*/dsp.h) \
             $(wildcard units/*/*.h) $(wildcard units/*/*/dsp.h)
	@mkdir -p dist/test
	@$(CXX) $(TEST_FLAGS) tests/render.cc -o $@
