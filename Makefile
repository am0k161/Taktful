CXX      ?= g++
AR       ?= ar
CXXFLAGS ?= -std=c++14 -O2 -Wall -Wextra -pedantic
CPPFLAGS  = -Iinclude -Idata
LDFLAGS  ?=

# NEON=0 disables kernels. NEON=1 enables them when the compiler defines __ARM_NEON.
# 32-bit ARM cross-builds need an FPU flag, e.g. make NEON=1 ARM_FPU=neon CXX=arm-linux-gnueabihf-g++
ifeq ($(NEON),0)
CPPFLAGS += -DTAKTFUL_ENABLE_NEON=0
endif
ifeq ($(NEON),1)
CPPFLAGS += -DTAKTFUL_ENABLE_NEON=1
endif
ifneq ($(ARM_FPU),)
CXXFLAGS += -mfpu=$(ARM_FPU)
endif

SRC = src/wav_io.cpp src/dsp.cpp src/fft.cpp src/onset.cpp \
      src/tempo.cpp src/chroma.cpp src/key.cpp src/api.cpp \
      src/stream.cpp src/c_api.cpp src/notes.cpp
OBJ = $(SRC:.cpp=.o)

.PHONY: all lib cli tests clean

all: lib

lib: libtaktful.a

libtaktful.a: $(OBJ)
	$(AR) rcs $@ $^

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

cli: lib taktful analyze_wav bpm_probe key_probe notes_probe run_benchmark

taktful: cli/taktful.cpp libtaktful.a
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -Icli $< -o $@ libtaktful.a $(LDFLAGS)

analyze_wav: cli/analyze_wav.cpp libtaktful.a
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -Icli $< -o $@ libtaktful.a $(LDFLAGS)

bpm_probe: cli/bpm_probe.cpp libtaktful.a
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -Icli $< -o $@ libtaktful.a $(LDFLAGS)

key_probe: cli/key_probe.cpp libtaktful.a
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -Icli $< -o $@ libtaktful.a $(LDFLAGS)

notes_probe: cli/notes_probe.cpp libtaktful.a
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -Icli $< -o $@ libtaktful.a $(LDFLAGS)

run_benchmark: cli/run_benchmark.cpp libtaktful.a
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -Icli -Itests/unit $< -o $@ libtaktful.a $(LDFLAGS)

tests: lib taktful_unit_tests taktful_integration_tests
	./taktful_unit_tests
	./taktful_integration_tests

taktful_unit_tests: tests/unit/test_all.cpp libtaktful.a
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -Ithird_party -Itests/unit $< -o $@ libtaktful.a $(LDFLAGS)

taktful_integration_tests: tests/integration/test_integration.cpp libtaktful.a
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -Ithird_party -Itests/unit -Itests/integration $< -o $@ libtaktful.a $(LDFLAGS)

clean:
	rm -f $(OBJ) libtaktful.a taktful analyze_wav bpm_probe key_probe notes_probe run_benchmark taktful_unit_tests taktful_integration_tests
