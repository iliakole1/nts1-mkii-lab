##############################################################################
# nts1-mkii-lab
#
#   make            build every unit into dist/
#   make test       build and run the offline render tests (writes dist/test)
#   make clean      wipe build artefacts
#   make UNIT=poly8 unit    build a single unit
#

UNITS := $(notdir $(wildcard units/*))

.PHONY: all test clean $(UNITS)

all: $(UNITS)

$(UNITS):
	@$(MAKE) --no-print-directory -C units/$@ install

clean:
	@for u in $(UNITS); do $(MAKE) --no-print-directory -C units/$$u clean; done
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

$(TEST_BIN): tests/render.cc $(wildcard common/*.h) $(wildcard units/*/dsp.h)
	@mkdir -p dist/test
	@$(CXX) $(TEST_FLAGS) tests/render.cc -o $@
