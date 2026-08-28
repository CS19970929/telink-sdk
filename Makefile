.PHONY: env sources build rebuild size map verify ci

PYTHON ?= python
JOBS ?= 4

env:
	$(PYTHON) bms_tools/bms.py env

sources:
	$(PYTHON) bms_tools/bms.py sources --check

build:
	$(PYTHON) bms_tools/bms.py build --jobs $(JOBS)

rebuild:
	$(PYTHON) bms_tools/bms.py rebuild --jobs $(JOBS)

size:
	$(PYTHON) bms_tools/bms.py size

map:
	$(PYTHON) bms_tools/bms.py map

verify:
	$(PYTHON) bms_tools/bms.py verify

ci:
	$(PYTHON) bms_tools/bms.py ci --jobs $(JOBS)
