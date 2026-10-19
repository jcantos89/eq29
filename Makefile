#!/usr/bin/make -f
# Makefile for DynEQ — DPF-based dynamic EQ LV2 plugin
# Requires the DPF framework cloned at ./dpf (see README.md)

NAME = dyneq
FILES_DSP = src/DynEQPlugin.cpp
FILES_UI  = src/DynEGUI.cpp

DPF_PATH = dpf
UI_TYPE = cairo

include $(DPF_PATH)/Makefile.plugins.mk

TARGETS += lv2_sep

all: $(TARGETS)
