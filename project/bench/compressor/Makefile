# Project Name
TARGET = compressor

# Sources
CPP_SOURCES = compressor.cpp

# Compressor 在 DaisySP-LGPL 子專案裡（授權不同被拆出去），要開這個旗標
# 才會 -I 它的 Source/ 並連結 libdaisysp-lgpl.a（libDaisy/core/Makefile:148-152, 258-260）
USE_DAISYSP_LGPL = 1

# Library Locations
LIBDAISY_DIR ?= ../../../DaisyExamples/libDaisy
DAISYSP_DIR ?= ../../../DaisyExamples/DaisySP

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
