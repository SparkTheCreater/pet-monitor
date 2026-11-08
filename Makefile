PM_PLATFORM ?= rk3588
PM_MPP_INCLUDE ?= /usr/include/rockchip
PM_RGA_INCLUDE ?= /usr/include/rga
PM_RKNN_INCLUDE ?=

BUILD := build

CMAKE_FLAGS := \
	-DPM_PLATFORM=$(PM_PLATFORM) \
	-DPM_MPP_INCLUDE=$(PM_MPP_INCLUDE) \
	-DPM_RGA_INCLUDE=$(PM_RGA_INCLUDE) \
	-DPM_RKNN_INCLUDE=$(PM_RKNN_INCLUDE)

ifeq ($(PM_PLATFORM),rk3588)
CMAKE_FLAGS += -DCMAKE_CXX_FLAGS=-DRK_3588
endif

.PHONY: all clean

all:
	cmake -S . -B $(BUILD) $(CMAKE_FLAGS)
	cmake --build $(BUILD)

clean:
	rm -rf $(BUILD)
