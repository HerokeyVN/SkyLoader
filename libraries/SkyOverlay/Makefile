TARGET := ../../dist/SkyOverlay.dll
SRC := src/dllmain.cpp \
       src/overlay_host.cpp \
       third_party/imgui/imgui.cpp \
       third_party/imgui/imgui_draw.cpp \
       third_party/imgui/imgui_tables.cpp \
       third_party/imgui/imgui_widgets.cpp \
       third_party/imgui/backends/imgui_impl_win32.cpp \
       third_party/imgui/backends/imgui_impl_dx11.cpp

CXX := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -DSKYOVERLAY_BUILD \
            -Iinclude -Ithird_party/imgui -Ithird_party/imgui/backends
LDFLAGS := -shared -static -static-libgcc -static-libstdc++ \
           -Wl,--gc-sections -ld3d11 -ld3dcompiler -ldxgi -ldwmapi -lgdi32 -luser32

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC) | ../../dist
	$(CXX) $(CXXFLAGS) $(SRC) -o $@ $(LDFLAGS)

../../dist:
	mkdir ..\\..\\dist

clean:
	-del /q ..\\..\\dist\\SkyOverlay.dll
