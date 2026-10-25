.DEFAULT_GOAL := build

CMAKE ?= cmake
CTEST ?= ctest
# Interactive play needs the optimized CPU geometry path; checks keep Debug.
BUILD_DIR ?= $(if $(filter run,$(MAKECMDGOALS)),$(RELEASE_BUILD_DIR),build/dev)
BUILD_TYPE ?= $(if $(filter run,$(MAKECMDGOALS)),Release,Debug)
GENERATOR ?= Ninja
PYTHON ?= python3
CMAKE_ARGS ?=
BUILD_JOBS ?= 6
BUILD_TARGETS ?=
RUN_ARGS ?=
TEST_JOBS ?= 6
TEST_PROFILE ?= auto
TESTS ?=
CTEST_ARGS ?=
SANITIZER_BUILD_DIR ?= build/sanitized
NETWORK_BUILD_DIR ?= build/network
RELEASE_BUILD_DIR ?= build/release
DIST_BUILD_DIR ?= build/dist
RELEASE_OUTPUT_DIR ?= dist
PACKAGE_ARGS ?=
.PHONY: configure build run run-original test check check-full check-network check-sanitizers release format-check

configure:
	"$(PYTHON)" tools/configure.py -- $(CMAKE) -S . -B "$(BUILD_DIR)" -G "$(GENERATOR)" -DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" -DPython3_EXECUTABLE="$(PYTHON)" -DTOCA_RUN_ARGS:STRING="$(RUN_ARGS)" $(CMAKE_ARGS)

build: configure
	$(CMAKE) --build "$(BUILD_DIR)" --config "$(BUILD_TYPE)" --parallel "$(BUILD_JOBS)" $(if $(strip $(BUILD_TARGETS)),--target $(BUILD_TARGETS))

run: configure
	$(CMAKE) --build "$(BUILD_DIR)" --config "$(BUILD_TYPE)" --parallel "$(BUILD_JOBS)" --target run

run-original:
	./tools/run-reference.sh --sound

test check: configure
	"$(PYTHON)" tools/check.py --build-dir "$(BUILD_DIR)" --config "$(BUILD_TYPE)" --cmake "$(CMAKE)" --ctest "$(CTEST)" --build-jobs "$(BUILD_JOBS)" --test-jobs "$(TEST_JOBS)" --profile "$(TEST_PROFILE)" $(foreach target,$(BUILD_TARGETS),--build-target "$(target)") -- $(if $(strip $(TESTS)),-R '$(TESTS)') $(CTEST_ARGS)

check-full:
	+$(MAKE) check TEST_PROFILE=full

# Short two-game acceptance, independent of optional Mega asset generation.
check-network:
	@"$(PYTHON)" -c "import sys; sys.exit(0 if sys.platform.startswith('linux') else 'check-network requires Linux/Xvfb')"
	+$(MAKE) check BUILD_DIR="$(NETWORK_BUILD_DIR)" CMAKE_ARGS="$(CMAKE_ARGS) -DTOCA_MEGA_MODELS=OFF -DBUILD_TESTING=ON" TEST_PROFILE=full TESTS='^(network-(protocol|transport|session|race-state|boost|motion|progress|results)|frontend-network-lobby|native-network-(lobby|smoke|recovery))$$'

check-sanitizers:
	+$(MAKE) check BUILD_DIR="$(SANITIZER_BUILD_DIR)" BUILD_TYPE=Debug CMAKE_ARGS="$(CMAKE_ARGS) -DTOCA_SANITIZERS=ON -DBUILD_TESTING=ON"

release:
	+$(MAKE) build BUILD_DIR="$(DIST_BUILD_DIR)" BUILD_TYPE=Release BUILD_TARGETS=toca-game CMAKE_ARGS="$(CMAKE_ARGS) -DTOCA_DISTRIBUTION=ON -DBUILD_TESTING=OFF -DTOCA_SANITIZERS=OFF -DTOCA_USE_SYSTEM_SDL3=OFF"
	"$(PYTHON)" tools/package_release.py --build-dir "$(DIST_BUILD_DIR)" --output-dir "$(RELEASE_OUTPUT_DIR)" $(PACKAGE_ARGS)

format-check: configure
	$(CMAKE) --build "$(BUILD_DIR)" --config "$(BUILD_TYPE)" --parallel "$(BUILD_JOBS)" --target format-check
