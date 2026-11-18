UNAME_S := $(shell uname -s)
LIBRAW_DIR ?= ../libraw-heraw
SVT_DIR ?= ../svt-jpeg-xs
WVDC_DIR ?= ../nraw-bayer-draft
WVDC_SRCS := $(WVDC_DIR)/src/nev_container.cpp $(WVDC_DIR)/src/arw_container.cpp \
	$(WVDC_DIR)/src/ticoraw_dec.cpp $(WVDC_DIR)/src/arw6_dec.cpp \
	$(WVDC_DIR)/src/dng_writer.cpp $(WVDC_DIR)/src/session_api.cpp $(WVDC_DIR)/src/c_api.cpp
WVDC_FLAGS := -DHERAW_HAS_WVDC -DNIJINWVDC_STATIC -I$(WVDC_DIR)/include

ifeq ($(UNAME_S),Linux)
CXX ?= g++
CXXFLAGS ?= -std=gnu++17 -O3 -mavx2 -mfma -ffp-contract=fast -DLIBRAW_NODLL $(WVDC_FLAGS) -Wall -Wextra -Werror
LIBRAW_LIB := $(shell [ -f $(LIBRAW_DIR)/build-linux64/libraw_r.a ] && echo $(LIBRAW_DIR)/build-linux64/libraw_r.a || echo $(LIBRAW_DIR)/lib/.libs/libraw_r.a)
SVT_HOST_LIB := $(SVT_DIR)/build-linux64/libSvtJpegxs.a
WVDC_HOST_LIB := build/wvdc/libnijinWVDC.a
LDFLAGS := -lz -ldl -lm -lpthread
HOST_GPU_OBJ :=
HOST_TARGETS := nijinLBS
else
CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O3 -mcpu=apple-m1 $(WVDC_FLAGS) -Wall -Wextra -Werror
LIBRAW_LIB := $(LIBRAW_DIR)/lib/.libs/libraw_r.a
SVT_HOST_LIB := $(SVT_DIR)/build-mac/libSvtJpegxs.a
WVDC_HOST_LIB := build/wvdc/libnijinWVDC.a
LCMS2_MAC_LIB := $(shell [ -f /opt/homebrew/opt/little-cms2/lib/liblcms2.a ] && echo /opt/homebrew/opt/little-cms2/lib/liblcms2.a || echo -llcms2)
LDFLAGS := $(LCMS2_MAC_LIB) -lz -lm -lpthread -framework Metal -framework Foundation
HOST_GPU_OBJ := heraw_gpu_metal.o
HOST_TARGETS := nijinLBS nijinLBS_gui nijinLBS.exe nijinLBS_gui.exe
endif

WIN_CXX ?= x86_64-w64-mingw32-g++
WIN_WINDRES ?= x86_64-w64-mingw32-windres
WIN_CXXFLAGS ?= -std=gnu++17 -O3 -mavx2 -mfma -ffp-contract=fast -Wa,-muse-unaligned-vector-move -DLIBRAW_NODLL $(WVDC_FLAGS) \
	-I$(SVT_DIR)/zlib-src -I$(SVT_DIR)/zlib-win64 -Wall -Wextra -Werror
WIN_LIBRAW := $(LIBRAW_DIR)/build-win64/libraw_r.a
WIN_SVT_LIB := $(SVT_DIR)/build-win64/libSvtJpegxs.a
WIN_WVDC_LIB := build/wvdc_win/libnijinWVDC.a
WIN_ZLIB := $(SVT_DIR)/zlib-win64/libzs.a
WIN_LDFLAGS := -static -static-libgcc -static-libstdc++ -Wl,--stack,33554432 -lws2_32 -lshell32 -lm -lpthread

HDRS := heraw_version.hpp heraw_cpu_cache.hpp heraw_exif_io.hpp heraw_burst_planner.hpp heraw_libraw_dyn.hpp \
	heraw_wvdc_router.hpp heraw_wavelet_noise.hpp \
	heraw_gpu_backend.hpp heraw_gpu_vulkan.hpp heraw_gpu_spirv.hpp heraw_cpu_stage45.hpp $(wildcard heraw_suite_*.hpp)
GUI_HDRS := heraw_brand_assets.hpp heraw_version.hpp heraw_cpu_cache.hpp heraw_gui_i18n.hpp
VERSION := $(shell awk '$$2 == "NIJIN_VERSION" { gsub(/"/, "", $$3); print $$3 }' heraw_version.hpp)

all: $(HOST_TARGETS)

build/wvdc/%.o: $(WVDC_DIR)/src/%.cpp $(WVDC_DIR)/include/nijinWVDC.h $(WVDC_DIR)/src/codec_internal.h $(WVDC_DIR)/src/quant_model_internal.h
	@mkdir -p build/wvdc
	$(CXX) -O3 -std=c++17 $(if $(filter Linux,$(UNAME_S)),-mavx2 -mfma -ffp-contract=fast,-mcpu=apple-m1) -Wall -Wextra -Werror -DNIJINWVDC_STATIC -I$(WVDC_DIR)/include -c $< -o $@

$(WVDC_HOST_LIB): $(patsubst $(WVDC_DIR)/src/%.cpp,build/wvdc/%.o,$(WVDC_SRCS))
	ar rcs $@ $^

build/wvdc_win/%.o: $(WVDC_DIR)/src/%.cpp $(WVDC_DIR)/include/nijinWVDC.h $(WVDC_DIR)/src/codec_internal.h $(WVDC_DIR)/src/quant_model_internal.h
	@mkdir -p build/wvdc_win
	$(WIN_CXX) -O3 -std=gnu++17 -mavx2 -mfma -ffp-contract=fast -Wa,-muse-unaligned-vector-move -Wall -Wextra -Werror -DNIJINWVDC_STATIC -I$(WVDC_DIR)/include -c $< -o $@

$(WIN_WVDC_LIB): $(patsubst $(WVDC_DIR)/src/%.cpp,build/wvdc_win/%.o,$(WVDC_SRCS))
	x86_64-w64-mingw32-ar rcs $@ $^

build/wvdc_asan/%.o: $(WVDC_DIR)/src/%.cpp $(WVDC_DIR)/include/nijinWVDC.h $(WVDC_DIR)/src/codec_internal.h $(WVDC_DIR)/src/quant_model_internal.h
	@mkdir -p build/wvdc_asan
	$(CXX) -O2 -g -std=c++17 $(if $(filter Linux,$(UNAME_S)),-mavx2 -mfma -ffp-contract=fast,) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -Wall -Wextra -Werror -DNIJINWVDC_STATIC -I$(WVDC_DIR)/include -c $< -o $@

build/wvdc_asan/libnijinWVDC.a: $(patsubst $(WVDC_DIR)/src/%.cpp,build/wvdc_asan/%.o,$(WVDC_SRCS))
	ar rcs $@ $^

heraw_gpu_metal.o: heraw_gpu_metal.mm heraw_gpu_backend.hpp
	$(CXX) -std=c++17 -fobjc-arc -O3 -mcpu=apple-m1 -Wall -Wextra -Werror -c heraw_gpu_metal.mm -o heraw_gpu_metal.o

nijinLBS: heraw_sr2x.cpp $(HOST_GPU_OBJ) $(HDRS) $(LIBRAW_LIB) $(SVT_HOST_LIB) $(WVDC_HOST_LIB)
	$(CXX) $(CXXFLAGS) heraw_sr2x.cpp $(HOST_GPU_OBJ) $(LIBRAW_LIB) $(SVT_HOST_LIB) $(WVDC_HOST_LIB) $(LDFLAGS) -o nijinLBS

nijinLBS_gui: heraw_sr2x_gui_mac.mm $(GUI_HDRS)
	$(CXX) -std=c++17 -fobjc-arc -O2 -Wall -Wextra -Werror heraw_sr2x_gui_mac.mm -framework Cocoa -o nijinLBS_gui

nijinLBS.exe: heraw_sr2x.cpp $(HDRS) $(WIN_LIBRAW) $(WIN_SVT_LIB) $(WIN_WVDC_LIB) $(WIN_ZLIB)
	$(WIN_CXX) $(WIN_CXXFLAGS) heraw_sr2x.cpp $(WIN_LIBRAW) $(WIN_SVT_LIB) $(WIN_WVDC_LIB) $(WIN_ZLIB) $(WIN_LDFLAGS) -o nijinLBS.exe

heraw_sr2x_gui_res.o: heraw_sr2x_gui.rc heraw_sr2x_gui.manifest heraw_sr2x_gui.ico
	$(WIN_WINDRES) heraw_sr2x_gui.rc -O coff -o heraw_sr2x_gui_res.o

nijinLBS_gui.exe: heraw_sr2x_gui_win.cpp $(GUI_HDRS) heraw_sr2x_gui_res.o
	$(WIN_CXX) -std=gnu++17 -O2 -municode -mwindows -Wall -Wextra -Werror \
		heraw_sr2x_gui_win.cpp heraw_sr2x_gui_res.o \
		-static -static-libgcc -static-libstdc++ \
		-lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -lgdi32 -lgdiplus -lmsimg32 -ladvapi32 -o nijinLBS_gui.exe

.PHONY: all asan dmg windows-zip clean

asan: nijinLBS_asan

ifeq ($(UNAME_S),Linux)
nijinLBS_asan: heraw_sr2x.cpp $(HDRS) $(LIBRAW_LIB) $(SVT_HOST_LIB) build/wvdc_asan/libnijinWVDC.a
	$(CXX) -std=gnu++17 -O2 -g -mavx2 -mfma -ffp-contract=fast -DLIBRAW_NODLL $(WVDC_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Werror \
		heraw_sr2x.cpp $(LIBRAW_LIB) $(SVT_HOST_LIB) build/wvdc_asan/libnijinWVDC.a $(LDFLAGS) -o nijinLBS_asan
else
nijinLBS_asan: heraw_sr2x.cpp heraw_gpu_metal.mm $(HDRS) $(LIBRAW_LIB) $(SVT_HOST_LIB) build/wvdc_asan/libnijinWVDC.a
	$(CXX) -std=c++17 -fobjc-arc -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Werror \
		-c heraw_gpu_metal.mm -o heraw_gpu_metal_asan.o
	$(CXX) -std=c++17 -O2 -g $(WVDC_FLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Werror \
		heraw_sr2x.cpp heraw_gpu_metal_asan.o $(LIBRAW_LIB) $(SVT_HOST_LIB) build/wvdc_asan/libnijinWVDC.a $(LDFLAGS) -o nijinLBS_asan
endif

dmg: nijinLBS nijinLBS_gui
	python3 scripts/build_macos_dmg.py $(VERSION) nijinLBS-v$(VERSION)-macOS-arm64.dmg

windows-zip: nijinLBS.exe nijinLBS_gui.exe
	python3 scripts/build_windows_zip.py $(VERSION) release_dist/nijinLBS-v$(VERSION)-Windows-x64.zip

clean:
	rm -rf build nijinLBS nijinLBS_gui nijinLBS.exe nijinLBS_gui.exe nijinLBS_asan \
		heraw_gpu_metal.o heraw_gpu_metal_asan.o heraw_sr2x_gui_res.o \
		NijiN.LiteBurstSuite.app
