.PHONY: profiles env sources build rebuild size map verify ci

PYTHON ?= python
JOBS ?= 4
BOARD ?= legacy-309
AFE ?= sh367309
PROFILE_ARGS := --board $(BOARD) --afe $(AFE)

profiles:
	$(PYTHON) bms_tools/bms.py profiles

env:
	$(PYTHON) bms_tools/bms.py env $(PROFILE_ARGS)

sources:
	$(PYTHON) bms_tools/bms.py sources --check

build:
	$(PYTHON) bms_tools/bms.py build $(PROFILE_ARGS) --jobs $(JOBS)

rebuild:
	$(PYTHON) bms_tools/bms.py rebuild $(PROFILE_ARGS) --jobs $(JOBS)

size:
	$(PYTHON) bms_tools/bms.py size $(PROFILE_ARGS)

map:
	$(PYTHON) bms_tools/bms.py map $(PROFILE_ARGS)

verify:
	$(PYTHON) bms_tools/bms.py verify $(PROFILE_ARGS)

ci:
	$(PYTHON) bms_tools/bms.py ci $(PROFILE_ARGS) --jobs $(JOBS)
