CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O3 -mcpu=apple-m1 -Wall -Wextra -Werror
LIBRAW_DIR ?= ../libraw-heraw
LIBRAW_LIB := $(LIBRAW_DIR)/lib/.libs/libraw_r.a
SVT_DIR ?= ../svt-jpeg-xs
SVT_MAC_LIB := $(SVT_DIR)/build-mac/libSvtJpegxs.a
LCMS2_MAC_LIB := $(shell [ -f /opt/homebrew/opt/little-cms2/lib/liblcms2.a ] && echo /opt/homebrew/opt/little-cms2/lib/liblcms2.a || echo -llcms2)
LDFLAGS := $(LCMS2_MAC_LIB) -lz -lm -lpthread -framework Metal -framework Foundation

WIN_CXX ?= x86_64-w64-mingw32-g++
WIN_WINDRES ?= x86_64-w64-mingw32-windres
WIN_CXXFLAGS ?= -std=gnu++17 -O3 -mavx2 -mfma -ffp-contract=fast -DLIBRAW_NODLL \
	-I$(SVT_DIR)/zlib-src -I$(SVT_DIR)/zlib-win64 -Wall -Wextra -Werror
WIN_LIBRAW := $(LIBRAW_DIR)/build-win64/libraw_r.a
WIN_SVT_LIB := $(SVT_DIR)/build-win64/libSvtJpegxs.a
WIN_ZLIB := $(SVT_DIR)/zlib-win64/libzs.a
WIN_LDFLAGS := -static -static-libgcc -static-libstdc++ -Wl,--stack,33554432 -lws2_32 -lshell32 -lm -lpthread

HDRS := heraw_version.hpp heraw_exif_io.hpp heraw_burst_planner.hpp heraw_libraw_dyn.hpp \
	heraw_gpu_backend.hpp heraw_gpu_vulkan.hpp heraw_gpu_spirv.hpp heraw_cpu_stage45.hpp $(wildcard heraw_suite_*.hpp)
GUI_HDRS := heraw_brand_assets.hpp heraw_version.hpp
VERSION := $(shell awk '$$2 == "NIJIN_VERSION" { gsub(/"/, "", $$3); print $$3 }' heraw_version.hpp)

all: nijinLBS nijinLBS_gui nijinLBS.exe nijinLBS_gui.exe

heraw_gpu_metal.o: heraw_gpu_metal.mm heraw_gpu_backend.hpp
	$(CXX) -std=c++17 -fobjc-arc -O3 -mcpu=apple-m1 -Wall -Wextra -Werror -c heraw_gpu_metal.mm -o heraw_gpu_metal.o

nijinLBS: heraw_sr2x.cpp heraw_gpu_metal.o $(HDRS) $(LIBRAW_LIB) $(SVT_MAC_LIB)
	$(CXX) $(CXXFLAGS) heraw_sr2x.cpp heraw_gpu_metal.o $(LIBRAW_LIB) $(SVT_MAC_LIB) $(LDFLAGS) -o nijinLBS

nijinLBS_gui: heraw_sr2x_gui_mac.mm $(GUI_HDRS)
	$(CXX) -std=c++17 -fobjc-arc -O2 -Wall -Wextra -Werror heraw_sr2x_gui_mac.mm -framework Cocoa -o nijinLBS_gui

nijinLBS.exe: heraw_sr2x.cpp $(HDRS) $(WIN_LIBRAW) $(WIN_SVT_LIB) $(WIN_ZLIB)
	$(WIN_CXX) $(WIN_CXXFLAGS) heraw_sr2x.cpp $(WIN_LIBRAW) $(WIN_SVT_LIB) $(WIN_ZLIB) $(WIN_LDFLAGS) -o nijinLBS.exe

heraw_sr2x_gui_res.o: heraw_sr2x_gui.rc heraw_sr2x_gui.manifest heraw_sr2x_gui.ico
	$(WIN_WINDRES) heraw_sr2x_gui.rc -O coff -o heraw_sr2x_gui_res.o

nijinLBS_gui.exe: heraw_sr2x_gui_win.cpp $(GUI_HDRS) heraw_sr2x_gui_res.o
	$(WIN_CXX) -std=gnu++17 -O2 -municode -mwindows -Wall -Wextra -Werror \
		heraw_sr2x_gui_win.cpp heraw_sr2x_gui_res.o \
		-static -static-libgcc -static-libstdc++ \
		-lcomctl32 -lcomdlg32 -lshell32 -lole32 -luuid -lgdi32 -lgdiplus -lmsimg32 -ladvapi32 -o nijinLBS_gui.exe

asan: heraw_sr2x.cpp heraw_gpu_metal.mm $(HDRS) $(LIBRAW_LIB) $(SVT_MAC_LIB)
	$(CXX) -std=c++17 -fobjc-arc -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Werror \
		-c heraw_gpu_metal.mm -o heraw_gpu_metal_asan.o
	$(CXX) -std=c++17 -O2 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Werror \
		heraw_sr2x.cpp heraw_gpu_metal_asan.o $(LIBRAW_LIB) $(SVT_MAC_LIB) $(LDFLAGS) -o nijinLBS_asan

dmg: nijinLBS nijinLBS_gui
	python3 scripts/build_macos_dmg.py $(VERSION) nijinLBS-v$(VERSION)-macOS-arm64.dmg

windows-zip: nijinLBS.exe nijinLBS_gui.exe
	python3 scripts/build_windows_zip.py $(VERSION) release_dist/nijinLBS-v$(VERSION)-Windows-x64.zip

clean:
	rm -rf nijinLBS nijinLBS_gui nijinLBS.exe nijinLBS_gui.exe nijinLBS_asan \
		heraw_gpu_metal.o heraw_gpu_metal_asan.o heraw_sr2x_gui_res.o \
		NijiN.LiteBurstSuite.app
