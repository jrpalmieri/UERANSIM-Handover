
GREEN=\033[0;1;92m
NC=\033[0m

build: FORCE
	rm -fr logs # Old version log files
	mkdir -p build
	rm -fr build/*
	
	# cmake -DCMAKE_BUILD_TYPE=Debug -G "Unix Makefiles" . -B build
	cmake -DCMAKE_BUILD_TYPE=Release -G "Unix Makefiles" . -B build
	# cmake --build build --target all -j4
	cmake --build build --target all -j4
	
	cp tools/nr-binder build/

	@printf "${GREEN}UERANSIM successfully built.${NC}\n"

# Build fully static executables portable across Ubuntu 20.04 - 25.x
# Requires: libsctp-dev, libc6-dev (for static libraries)
build-static: FORCE
	rm -fr logs
	mkdir -p build
	rm -fr build/*
	
	cmake -DCMAKE_BUILD_TYPE=Release -DSTATIC_BUILD=ON -G "Unix Makefiles" . -B build
	cmake --build build --target all -j4
	
	cp tools/nr-binder build/

	@printf "${GREEN}UERANSIM successfully built (static).${NC}\n"

# debug build
debug: FORCE

	cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -G "Unix Makefiles" . -B build

	cmake --build build --target all -j4
	cp tools/nr-binder build/

	@printf "${GREEN}UERANSIM full debug build completed.${NC}\n"


# Incremental build: only changed files and affected targets are rebuilt.
fast: FORCE
	@if [ ! -f build/CMakeCache.txt ]; then \
		cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -G "Unix Makefiles" . -B build; \
	fi
	cmake --build build --target all -j4
	@if [ -f tools/nr-binder ]; then cp tools/nr-binder build/; fi

	@printf "${GREEN}UERANSIM incremental build completed.${NC}\n"


# Incremental debug build: only changed files and affected targets are rebuilt.
fast-debug: FORCE
	@if [ ! -f build/CMakeCache.txt ]; then \
		cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -G "Unix Makefiles" . -B build; \
	fi
	cmake --build build --target all -j4
	@if [ -f tools/nr-binder ]; then cp tools/nr-binder build/; fi

	@printf "${GREEN}UERANSIM incremental debug build completed.${NC}\n"
	
# Generate/refresh compile_commands.json for IDE IntelliSense.
intellisense: FORCE
#	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	cmake -S . -B build -DCMAKE_BUILD_TYPE="" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	@printf "${GREEN}compile_commands.json generated at build/compile_commands.json.${NC}\n"

FORCE:

clean:
	rm -fr build