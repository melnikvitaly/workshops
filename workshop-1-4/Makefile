SHELL   := powershell
# Makefile — run PC-lint on project source files
# Usage:
#   make lint        — lint all source files
#   make lint-clean  — remove lint output file
#   make clean       — alias for lint-clean

LINT     := lint-nt.exe
LINTFLAGS := lint/project.lnt

SRCS := \
    src/main.cpp \
    src/TrafficLights.cpp \
    src/hardware/AdditionalLED.cpp \
    src/hardware/DebugOut.cpp \
    src/hardware/LEDs.cpp

LINT_OUT := lint/lint_output.txt

.PHONY: lint lint-clean clean

lint: $(LINT_OUT)

$(LINT_OUT): $(SRCS) lint/project.lnt lint/co-gcc.lnt lint/options.lnt lint/suppress.lnt
	$(LINT) $(LINTFLAGS) $(SRCS) 2>&1 | tee $(LINT_OUT)

lint-clean clean:
	@rm -f $(LINT_OUT)
