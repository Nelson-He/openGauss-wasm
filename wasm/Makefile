# contrib/wasm/Makefile

MODULE_big = wasm_executor
OBJS= wasm_executor.o

EXTENSION = wasm_executor
DATA = wasm_executor--1.0.sql

SHLIB_LINK_INTERNAL = $(libpq)
SHLIB_LINK += -lwasmedge

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/wasm
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
override CPPFLAGS := $(filter-out -fPIE, $(CPPFLAGS)) -fPIC
endif
