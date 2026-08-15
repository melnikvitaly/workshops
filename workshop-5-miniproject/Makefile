# Mini Project 5 - closed-loop laser tracking.
#
# Thin wrappers over the commands this project actually uses: PlatformIO for
# the ESP32-S3 firmware, Python for the PC vision pipeline, npm for the docs.
# Recipes are single commands so they run under both cmd.exe and sh.

PIO     ?= pio
NPM     ?= npm
ENV     ?= esp32-s3-devkitc-1
PORT    ?=

# camera/.venv is gitignored, so prefer it when it is there and fall back to the
# system launcher for a fresh clone. Both venv layouts, since the interpreter
# lands in Scripts/ on Windows and bin/ everywhere else.
ifneq ($(wildcard camera/.venv/Scripts/python.exe),)
PY      ?= camera/.venv/Scripts/python.exe
else ifneq ($(wildcard camera/.venv/bin/python),)
PY      ?= camera/.venv/bin/python
else
PY      ?= py -3
endif

PIO_ENV := -e $(ENV)
ifneq ($(PORT),)
PIO_PORT := --upload-port $(PORT)
MON_PORT := --port $(PORT)
else
PIO_PORT :=
MON_PORT :=
endif

.DEFAULT_GOAL := help

.PHONY: help build upload monitor flash clean fullclean \
        vision vision-dry py-deps node-deps deps \
        format-md lint-md fix-md check-md docs

# Unquoted, single-spaced, no blank lines: sh collapses runs of spaces and
# `echo.` is a cmd.exe-ism, so neither alignment nor blank lines survive both.
help:
	@echo --- firmware ---
	@echo make build - compile the firmware
	@echo make upload - compile and flash the board
	@echo make monitor - open the serial monitor at 115200
	@echo make flash - upload, then monitor
	@echo make clean - remove build output
	@echo make fullclean - also drop the cached toolchain/framework
	@echo --- vision, PC side ---
	@echo make vision - run the tracker and stream to the board
	@echo make vision-dry - run detection only, send nothing
	@echo --- docs ---
	@echo make format-md - prettify markdown tables in place
	@echo make lint-md - run markdownlint-cli2
	@echo make fix-md - auto-fix what markdownlint can fix
	@echo make check-md - read-only check for CI, fails if unformatted
	@echo make docs - format, then lint
	@echo --- setup ---
	@echo make deps - install Python and Node dependencies
	@echo --- variables ---
	@echo ENV=$(ENV) - build environment
	@echo PORT=COMx - pin the serial port, e.g. make flash PORT=COM10
	@echo PY=$(PY) - Python behind the vision targets

# --- firmware ---------------------------------------------------------------

build:
	$(PIO) run $(PIO_ENV)

upload:
	$(PIO) run $(PIO_ENV) -t upload $(PIO_PORT)

monitor:
	$(PIO) device monitor $(PIO_ENV) $(MON_PORT)

flash: upload monitor

clean:
	$(PIO) run $(PIO_ENV) -t clean

fullclean:
	$(PIO) run $(PIO_ENV) -t fullclean

# --- vision -----------------------------------------------------------------

# Bare --port makes the script find the board by USB VID/PID; PORT=COMx pins it.
vision:
	$(PY) camera/detect_dots.py --port $(PORT)

vision-dry:
	$(PY) camera/detect_dots.py

# --- docs -------------------------------------------------------------------

format-md:
	$(NPM) run format:md

lint-md:
	$(NPM) run lint:md

fix-md:
	$(NPM) run lint:md:fix

check-md:
	$(NPM) run check:md

docs: format-md lint-md

# --- setup ------------------------------------------------------------------

py-deps:
	$(PY) -m pip install -r camera/requirements.txt

node-deps:
	$(NPM) install

deps: py-deps node-deps
