
GREEN=\033[0;1;92m
NC=\033[0m

# Parallel compile jobs. Defaults to the number of cores on the machine.
# Override on the command line when a build has to leave headroom, for example:
#   make build JOBS=4
JOBS ?= $(shell nproc)

# Ninja schedules this tree's roughly 5,500 objects better than Unix Makefiles
# and reduces an already-up-to-date build from about a second to a fraction of
# one, which is what makes the incremental targets below worth using. Unix
# Makefiles remain the fallback so the build still works where ninja is absent.
GENERATOR := $(shell command -v ninja > /dev/null 2>&1 && echo Ninja || echo Unix Makefiles)

CMAKE_FLAGS = -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -G "$(GENERATOR)"

# Configure only when the build directory is missing or was configured
# differently. The signature passed as $(1) names the configuration, so that
# switching between, say, the release and debug targets reconfigures while
# repeating the same target does not. $(2) holds the extra CMake arguments.
#
# CMake cannot change generator in place, so a build directory left over from an
# older Unix Makefiles configuration is removed rather than reused.
define configure
	@if [ -f build/CMakeCache.txt ] && \
	   ! grep -q '^CMAKE_GENERATOR:INTERNAL=$(GENERATOR)$$' build/CMakeCache.txt; then \
		printf "Build directory was configured with a different generator; reconfiguring.\n"; \
		rm -fr build; \
	fi
	@if [ ! -f build/CMakeCache.txt ] || [ "$$(cat build/.config-signature 2> /dev/null)" != "$(1)" ]; then \
		cmake $(2) $(CMAKE_FLAGS) -S . -B build && printf '%s' '$(1)' > build/.config-signature; \
	fi
endef

# Build the release configuration. Only the sources that changed since the last
# build are recompiled; use the rebuild target to start from an empty tree.
build: FORCE
	$(call configure,release,-DCMAKE_BUILD_TYPE=Release)
	cmake --build build --target all -j$(JOBS)
	@if [ -f tools/nr-binder ]; then cp tools/nr-binder build/; fi

	@printf "${GREEN}UERANSIM successfully built.${NC}\n"

# Build fully static executables portable across Ubuntu 20.04 - 25.x
# Requires: libsctp-dev, libc6-dev (for static libraries)
build-static: FORCE
	$(call configure,static,-DCMAKE_BUILD_TYPE=Release -DSTATIC_BUILD=ON)
	cmake --build build --target all -j$(JOBS)
	@if [ -f tools/nr-binder ]; then cp tools/nr-binder build/; fi

	@printf "${GREEN}UERANSIM successfully built (static).${NC}\n"

# Build the debug configuration, again recompiling only what changed.
debug: FORCE
	$(call configure,debug,-DCMAKE_BUILD_TYPE=Debug)
	cmake --build build --target all -j$(JOBS)
	@if [ -f tools/nr-binder ]; then cp tools/nr-binder build/; fi

	@printf "${GREEN}UERANSIM full debug build completed.${NC}\n"

# Discard the build directory and build the release configuration from scratch.
# Use this when a build result is suspect; the ordinary targets above are
# incremental and are what day to day work should use. The two steps run as a
# recursive call rather than as two prerequisites so that their order holds even
# when make itself was invoked in parallel.
rebuild: FORCE
	rm -fr build
	@$(MAKE) --no-print-directory build

# Old targets, retained so existing habits and scripts keep working: the ordinary targets are
# now incremental, so these are the same builds under their previous names.
fast: build
fast-debug: debug

# Generate/refresh compile_commands.json for IDE IntelliSense. This configures
# with no build type, so the signature file is dropped to make the next ordinary
# target reconfigure rather than compile with these settings.
intellisense: FORCE
	cmake -S . -B build -DCMAKE_BUILD_TYPE="" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	@rm -f build/.config-signature
	@printf "${GREEN}compile_commands.json generated at build/compile_commands.json.${NC}\n"

FORCE:

clean:
	rm -fr build
	rm -fr logs # Old version log files
