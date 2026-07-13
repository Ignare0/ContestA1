FILELIST ?= filelist.txt
TOP ?= tb
THREADS ?= 4
SIM_EXE ?= sim.out

SUBMISSION_DIR ?= $(abspath $(dir $(firstword $(MAKEFILE_LIST))))
BUILD_DIR ?= $(SUBMISSION_DIR)/.build
BUILD_JOBS ?= 4
CMAKE ?= cmake

IR_PROBE := $(BUILD_DIR)/bin/ir_probe

.PHONY: build compile_sim run parallel_run clean test
.DELETE_ON_ERROR:

build:
	$(CMAKE) -S "$(SUBMISSION_DIR)" -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build "$(BUILD_DIR)" --target ir_probe --parallel "$(BUILD_JOBS)"
	@test -x "$(IR_PROBE)"

compile_sim: build
	@echo "ERROR: compile_sim is not implemented in Phase 0" >&2
	@exit 2

run:
	@echo "ERROR: run is not implemented in Phase 0" >&2
	@exit 2

parallel_run:
	@echo "ERROR: parallel_run is not implemented in Phase 0" >&2
	@exit 2

test: build
	ctest --test-dir "$(BUILD_DIR)" --output-on-failure

clean:
	$(CMAKE) -E remove_directory "$(BUILD_DIR)"
