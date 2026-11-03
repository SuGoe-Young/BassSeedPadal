# Project Name
TARGET = multi_effect

# Sources
CPP_SOURCES = src/multi_effect.cpp

# Compressor 在 DaisySP-LGPL（授權不同被拆出去），要開這個旗標
USE_DAISYSP_LGPL = 1

# Library Locations（這個 Makefile 在 project/multiEffet/，往上兩層是專案根目錄）
LIBDAISY_DIR ?= ../../DaisyExamples/libDaisy
DAISYSP_DIR ?= ../../DaisyExamples/DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
