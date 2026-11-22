UNAME_S := $(shell uname -s)
REPO_ROOT := $(abspath ../..)
PMAP_FLAGS := -ffile-prefix-map=$(REPO_ROOT)=. -fmacro-prefix-map=$(REPO_ROOT)=. -fdebug-prefix-map=$(REPO_ROOT)=. -ffile-prefix-map=$(HOME)=~
LIBRAW_DIR ?= ../libraw-heraw
SVT_DIR ?= ../svt-jpeg-xs
WVDC_DIR ?= ../nraw-bayer-draft
CODEC_DIR ?= ../codec-deps
WVDC_SRCS := $(WVDC_DIR)/src/nev_container.cpp $(WVDC_DIR)/src/arw_container.cpp \
	$(WVDC_DIR)/src/ticoraw_dec.cpp $(WVDC_DIR)/src/arw6_dec.cpp \
	$(WVDC_DIR)/src/dng_writer.cpp $(WVDC_DIR)/src/session_api.cpp $(WVDC_DIR)/src/c_api.cpp
WVDC_FLAGS := -DHERAW_HAS_WVDC -DNIJINWVDC_STATIC -I$(WVDC_DIR)/include
CODEC_STATIC_DEFS := -DJXL_STATIC_DEFINE -DJXL_THREADS_STATIC_DEFINE -DJXL_CMS_STATIC_DEFINE -DOPJ_STATIC
CODEC_LIBS_LIST = $(1)/lib/libjxl.a $(1)/lib/libjxl_cms.a $(1)/lib/libjxl_threads.a $(1)/lib/libhwy.a \
	$(1)/lib/libbrotlienc.a $(1)/lib/libbrotlidec.a $(1)/lib/libbrotlicommon.a \
	$(1)/lib/libopenjp2.a $(1)/lib/libwebpmux.a $(1)/lib/libwebp.a $(1)/lib/libsharpyuv.a $(1)/lib/libjpeg.a

ifeq ($(UNAME_S),Linux)
CXX ?= g++
CODEC_HOST_INC := -I$(CODEC_DIR)/linux64/include -I$(CODEC_DIR)/linux64/include/openjpeg-2.5 $(CODEC_STATIC_DEFS)
CODEC_HOST_LIBS := $(call CODEC_LIBS_LIST,$(CODEC_DIR)/linux64)
CXXFLAGS ?= -std=gnu++20 -O3 -mavx2 -mfma -ffp-contract=fast $(PMAP_FLAGS) -DLIBRAW_NODLL $(WVDC_FLAGS) $(CODEC_HOST_INC) -Wall -Wextra -Werror
LIBRAW_LIB := $(shell [ -f $(LIBRAW_DIR)/build-linux64/libraw_r.a ] && echo $(LIBRAW_DIR)/build-linux64/libraw_r.a || echo $(LIBRAW_DIR)/lib/.libs/libraw_r.a)
SVT_HOST_LIB := $(SVT_DIR)/build-linux64/libSvtJpegxs.a
WVDC_HOST_LIB := build/wvdc/libnijinWVDC.a
LINUX_ZLIB_NATIVE := $(shell [ -f $(SVT_DIR)/zlib-linux64/libzs.a ] && echo $(SVT_DIR)/zlib-linux64/libzs.a || echo -lz)
LDFLAGS := -static-libstdc++ -static-libgcc -Wl,-s $(LINUX_ZLIB_NATIVE) -ldl -lm -lpthread
HOST_GPU_OBJ :=
HOST_TARGETS := nijinLBS
else
CXX ?= clang++
CODEC_HOST_INC := -I$(CODEC_DIR)/mac/include -I$(CODEC_DIR)/mac/include/openjpeg-2.5 $(CODEC_STATIC_DEFS)
CODEC_HOST_LIBS := $(call CODEC_LIBS_LIST,$(CODEC_DIR)/mac)
CXXFLAGS ?= -std=c++20 -O3 -mcpu=apple-m1 $(PMAP_FLAGS) $(WVDC_FLAGS) $(CODEC_HOST_INC) -Wall -Wextra -Werror
LIBRAW_LIB := $(LIBRAW_DIR)/lib/.libs/libraw_r.a
SVT_HOST_LIB := $(SVT_DIR)/build-mac/libSvtJpegxs.a
WVDC_HOST_LIB := build/wvdc/libnijinWVDC.a
LCMS2_MAC_LIB := $(shell [ -f /opt/homebrew/opt/little-cms2/lib/liblcms2.a ] && echo /opt/homebrew/opt/little-cms2/lib/liblcms2.a || echo -llcms2)
LDFLAGS := $(LCMS2_MAC_LIB) -lz -lm -lpthread -framework Metal -framework Foundation
HOST_GPU_OBJ := heraw_gpu_metal.o
HOST_TARGETS := nijinLBS nijinLBS_gui nijinLBS.exe nijinLBS_gui.exe nijinLBS_linux
endif

WIN_CXX ?= x86_64-w64-mingw32-g++
WIN_WINDRES ?= x86_64-w64-mingw32-windres
WIN_CODEC_INC := -I$(CODEC_DIR)/win64/include -I$(CODEC_DIR)/win64/include/openjpeg-2.5 $(CODEC_STATIC_DEFS)
WIN_CODEC_LIBS := $(call CODEC_LIBS_LIST,$(CODEC_DIR)/win64)
WIN_CXXFLAGS ?= -std=gnu++20 -O3 -mavx2 -mfma -ffp-contract=fast -Wa,-muse-unaligned-vector-move $(PMAP_FLAGS) -D_WIN32_WINNT=0x0602 -D__USE_MINGW_ANSI_STDIO=1 -DLIBRAW_NODLL $(WVDC_FLAGS) $(WIN_CODEC_INC) \
	-I$(SVT_DIR)/zlib-src -I$(SVT_DIR)/zlib-win64 -Wall -Wextra -Werror
WIN_LIBRAW := $(LIBRAW_DIR)/build-win64/libraw_r.a
WIN_SVT_LIB := $(SVT_DIR)/build-win64/libSvtJpegxs.a
WIN_WVDC_LIB := build/wvdc_win/libnijinWVDC.a
WIN_ZLIB := $(SVT_DIR)/zlib-win64/libzs.a
WIN_LDFLAGS := -static -static-libgcc -static-libstdc++ -Wl,-s -Wl,--stack,33554432 -lws2_32 -lshell32 -lm -lpthread

LINUX_CXX ?= x86_64-unknown-linux-gnu-g++
LINUX_AR ?= x86_64-unknown-linux-gnu-ar
LINUX_CODEC_INC := -I$(CODEC_DIR)/linux64/include -I$(CODEC_DIR)/linux64/include/openjpeg-2.5 $(CODEC_STATIC_DEFS)
LINUX_CODEC_LIBS := $(call CODEC_LIBS_LIST,$(CODEC_DIR)/linux64)
LINUX_CXXFLAGS ?= -std=gnu++20 -O3 -mavx2 -mfma -ffp-contract=fast $(PMAP_FLAGS) -DLIBRAW_NODLL $(WVDC_FLAGS) $(LINUX_CODEC_INC) \
	-I$(SVT_DIR)/zlib-src -I$(SVT_DIR)/zlib-linux64 -Wall -Wextra -Werror
LINUX_LIBRAW := $(LIBRAW_DIR)/build-linux64/libraw_r.a
LINUX_SVT_LIB := $(SVT_DIR)/build-linux64/libSvtJpegxs.a
LINUX_WVDC_LIB := build/wvdc_linux/libnijinWVDC.a
LINUX_ZLIB := $(SVT_DIR)/zlib-linux64/libzs.a
LINUX_LDFLAGS := -static-libstdc++ -static-libgcc -Wl,-s -Wl,-z,noexecstack -ldl -lm -lpthread

HDRS := heraw_fs_utf8.hpp heraw_version.hpp heraw_cpu_cache.hpp heraw_exif_io.hpp heraw_codecs_extra.hpp heraw_nlens.hpp heraw_eval_metrology.hpp heraw_focus_meta.hpp heraw_burst_planner.hpp heraw_libraw_dyn.hpp \
	heraw_wvdc_router.hpp heraw_wavelet_noise.hpp \
	heraw_gpu_backend.hpp heraw_gpu_vulkan.hpp heraw_gpu_spirv.hpp heraw_cpu_stage45.hpp $(wildcard heraw_suite_*.hpp)
GUI_HDRS := heraw_fs_utf8.hpp heraw_brand_assets.hpp heraw_version.hpp heraw_cpu_cache.hpp heraw_gui_i18n.hpp
VERSION := $(shell awk '$$2 == "NIJIN_VERSION" { gsub(/"/, "", $$3); print $$3 }' heraw_version.hpp)

all: $(HOST_TARGETS)

build/wvdc/%.o: $(WVDC_DIR)/src/%.cpp $(WVDC_DIR)/include/nijinWVDC.h $(WVDC_DIR)/src/codec_internal.h $(WVDC_DIR)/src/quant_model_internal.h $(WVDC_DIR)/src/focus_meta_internal.h $(WVDC_DIR)/src/simd_platform.h $(WVDC_DIR)/src/worker_pool.h $(WVDC_DIR)/src/tiff_internal.h
	@mkdir -p build/wvdc
	$(CXX) -O3 $(PMAP_FLAGS) $(if $(filter Linux,$(UNAME_S)),-std=gnu++20 -mavx2 -mfma -ffp-contract=fast,-std=c++20 -mcpu=apple-m1) -Wall -Wextra -Werror -DNIJINWVDC_STATIC -I$(WVDC_DIR)/include -c $< -o $@

$(WVDC_HOST_LIB): $(patsubst $(WVDC_DIR)/src/%.cpp,build/wvdc/%.o,$(WVDC_SRCS))
	ar rcs $@ $^

build/wvdc_win/%.o: $(WVDC_DIR)/src/%.cpp $(WVDC_DIR)/include/nijinWVDC.h $(WVDC_DIR)/src/codec_internal.h $(WVDC_DIR)/src/quant_model_internal.h $(WVDC_DIR)/src/focus_meta_internal.h $(WVDC_DIR)/src/simd_platform.h $(WVDC_DIR)/src/worker_pool.h $(WVDC_DIR)/src/tiff_internal.h
	@mkdir -p build/wvdc_win
	$(WIN_CXX) -O3 $(PMAP_FLAGS) -std=gnu++20 -mavx2 -mfma -ffp-contract=fast -Wa,-muse-unaligned-vector-move -D_WIN32_WINNT=0x0602 -D__USE_MINGW_ANSI_STDIO=1 -Wall -Wextra -Werror -DNIJINWVDC_STATIC -I$(WVDC_DIR)/include -c $< -o $@

$(WIN_WVDC_LIB): $(patsubst $(WVDC_DIR)/src/%.cpp,build/wvdc_win/%.o,$(WVDC_SRCS))
	x86_64-w64-mingw32-ar rcs $@ $^

build/wvdc_asan/%.o: $(WVDC_DIR)/src/%.cpp $(WVDC_DIR)/include/nijinWVDC.h $(WVDC_DIR)/src/codec_internal.h $(WVDC_DIR)/src/quant_model_internal.h $(WVDC_DIR)/src/focus_meta_internal.h $(WVDC_DIR)/src/simd_platform.h $(WVDC_DIR)/src/worker_pool.h $(WVDC_DIR)/src/tiff_internal.h
	@mkdir -p build/wvdc_asan
	$(CXX) -O2 -g $(PMAP_FLAGS) $(if $(filter Linux,$(UNAME_S)),-std=gnu++20 -mavx2 -mfma -ffp-contract=fast,-std=c++20) -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -Wall -Wextra -Werror -DNIJINWVDC_STATIC -I$(WVDC_DIR)/include -c $< -o $@

build/wvdc_asan/libnijinWVDC.a: $(patsubst $(WVDC_DIR)/src/%.cpp,build/wvdc_asan/%.o,$(WVDC_SRCS))
	ar rcs $@ $^

heraw_gpu_metal.o: heraw_gpu_metal.mm heraw_gpu_backend.hpp
	$(CXX) -std=c++20 -fobjc-arc -O3 -mcpu=apple-m1 $(PMAP_FLAGS) -Wall -Wextra -Werror -c heraw_gpu_metal.mm -o heraw_gpu_metal.o

nijinLBS: heraw_sr2x.cpp $(HOST_GPU_OBJ) $(HDRS) $(LIBRAW_LIB) $(SVT_HOST_LIB) $(WVDC_HOST_LIB) $(CODEC_HOST_LIBS)
	$(CXX) $(CXXFLAGS) heraw_sr2x.cpp $(HOST_GPU_OBJ) $(LIBRAW_LIB) $(SVT_HOST_LIB) $(WVDC_HOST_LIB) $(CODEC_HOST_LIBS) $(LDFLAGS) -o nijinLBS
	$(if $(filter Darwin,$(UNAME_S)),strip -x nijinLBS,)

nijinLBS_gui: heraw_sr2x_gui_mac.mm $(GUI_HDRS)
	$(CXX) -std=c++20 -fobjc-arc -O2 $(PMAP_FLAGS) -Wall -Wextra -Werror heraw_sr2x_gui_mac.mm -framework Cocoa -o nijinLBS_gui
	strip -x nijinLBS_gui

nijinLBS.exe: heraw_sr2x.cpp $(HDRS) $(WIN_LIBRAW) $(WIN_SVT_LIB) $(WIN_WVDC_LIB) $(WIN_ZLIB) $(WIN_CODEC_LIBS)
	$(WIN_CXX) $(WIN_CXXFLAGS) heraw_sr2x.cpp $(WIN_LIBRAW) $(WIN_SVT_LIB) $(WIN_WVDC_LIB) $(WIN_CODEC_LIBS) $(WIN_ZLIB) $(WIN_LDFLAGS) -o nijinLBS.exe

heraw_sr2x_gui_res.o: heraw_sr2x_gui.rc heraw_sr2x_gui.manifest heraw_sr2x_gui.ico
	$(WIN_WINDRES) heraw_sr2x_gui.rc -O coff -o heraw_sr2x_gui_res.o

nijinLBS_gui.exe: heraw_sr2x_gui_win.cpp $(GUI_HDRS) heraw_sr2x_gui_res.o
	$(WIN_CXX) -std=gnu++20 -O2 $(PMAP_FLAGS) -municode -mwindows -D_WIN32_WINNT=0x0602 -D__USE_MINGW_ANSI_STDIO=1 -Wall -Wextra -Werror \
		heraw_sr2x_gui_win.cpp heraw_sr2x_gui_res.o \
		-static -static-libgcc -static-libstdc++ -Wl,-s \
		-lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -lgdi32 -lgdiplus -lmsimg32 -ladvapi32 -o nijinLBS_gui.exe

build/wvdc_linux/%.o: $(WVDC_DIR)/src/%.cpp $(WVDC_DIR)/include/nijinWVDC.h $(WVDC_DIR)/src/codec_internal.h $(WVDC_DIR)/src/quant_model_internal.h $(WVDC_DIR)/src/focus_meta_internal.h $(WVDC_DIR)/src/simd_platform.h $(WVDC_DIR)/src/worker_pool.h $(WVDC_DIR)/src/tiff_internal.h
	@mkdir -p build/wvdc_linux
	$(LINUX_CXX) -O3 $(PMAP_FLAGS) -std=gnu++20 -mavx2 -mfma -ffp-contract=fast -Wall -Wextra -Werror -DNIJINWVDC_STATIC -I$(WVDC_DIR)/include -c $< -o $@

$(LINUX_WVDC_LIB): $(patsubst $(WVDC_DIR)/src/%.cpp,build/wvdc_linux/%.o,$(WVDC_SRCS))
	$(LINUX_AR) rcs $@ $^

nijinLBS_linux: heraw_sr2x.cpp $(HDRS) $(LINUX_LIBRAW) $(LINUX_SVT_LIB) $(LINUX_WVDC_LIB) $(LINUX_ZLIB) $(LINUX_CODEC_LIBS)
	$(LINUX_CXX) $(LINUX_CXXFLAGS) heraw_sr2x.cpp $(LINUX_LIBRAW) $(LINUX_SVT_LIB) $(LINUX_WVDC_LIB) $(LINUX_CODEC_LIBS) $(LINUX_ZLIB) $(LINUX_LDFLAGS) -o nijinLBS_linux

.PHONY: all asan test-vulkan-lifetime dmg windows-zip linux-tar clean

test-vulkan-lifetime: tests/test_vulkan_lifetime.cpp heraw_gpu_vulkan.hpp heraw_gpu_backend.hpp heraw_gpu_spirv.hpp
	@mkdir -p build
	$(CXX) $(if $(filter Linux,$(UNAME_S)),-std=gnu++20,-std=c++20) -O2 -g \
		-fsanitize=address,undefined -fsanitize-address-use-after-scope -fno-sanitize-recover=all -fno-omit-frame-pointer \
		-Wall -Wextra -Werror tests/test_vulkan_lifetime.cpp -o build/test_vulkan_lifetime_asan
	./build/test_vulkan_lifetime_asan

asan: nijinLBS_asan

ifeq ($(UNAME_S),Linux)
nijinLBS_asan: heraw_sr2x.cpp $(HDRS) $(LIBRAW_LIB) $(SVT_HOST_LIB) build/wvdc_asan/libnijinWVDC.a $(CODEC_HOST_LIBS)
	$(CXX) -std=gnu++20 -O2 -g -mavx2 -mfma -ffp-contract=fast -DLIBRAW_NODLL $(WVDC_FLAGS) $(CODEC_HOST_INC) -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Werror \
		heraw_sr2x.cpp $(LIBRAW_LIB) $(SVT_HOST_LIB) build/wvdc_asan/libnijinWVDC.a $(CODEC_HOST_LIBS) $(LDFLAGS) -o nijinLBS_asan
else
nijinLBS_asan: heraw_sr2x.cpp heraw_gpu_metal.mm $(HDRS) $(LIBRAW_LIB) $(SVT_HOST_LIB) build/wvdc_asan/libnijinWVDC.a $(CODEC_HOST_LIBS)
	$(CXX) -std=c++20 -fobjc-arc -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Werror \
		-c heraw_gpu_metal.mm -o heraw_gpu_metal_asan.o
	$(CXX) -std=c++20 -O2 -g $(WVDC_FLAGS) $(CODEC_HOST_INC) -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Werror \
		heraw_sr2x.cpp heraw_gpu_metal_asan.o $(LIBRAW_LIB) $(SVT_HOST_LIB) build/wvdc_asan/libnijinWVDC.a $(CODEC_HOST_LIBS) $(LDFLAGS) -o nijinLBS_asan
endif

dmg: nijinLBS nijinLBS_gui
	python3 scripts/build_macos_dmg.py $(VERSION) release_dist/nijinLBS-v$(VERSION)-macOS-arm64.dmg

windows-zip: nijinLBS.exe nijinLBS_gui.exe
	python3 scripts/build_windows_zip.py $(VERSION) release_dist/nijinLBS-v$(VERSION)-Windows-x64.zip

linux-tar: nijinLBS_linux
	python3 scripts/build_linux_tar.py $(VERSION) release_dist/nijinLBS-v$(VERSION)-Linux-x64.tar.gz

clean:
	rm -rf build nijinLBS nijinLBS_gui nijinLBS.exe nijinLBS_gui.exe nijinLBS_linux nijinLBS_asan \
		heraw_gpu_metal.o heraw_gpu_metal_asan.o heraw_sr2x_gui_res.o \
		NijiN.LiteBurstSuite.app NijiN.LiteBurstScaler.app *.dSYM shaders/*.spv
