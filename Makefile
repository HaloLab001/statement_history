# contrib/statement_history/Makefile

MODULE_big = statement_history
OBJS = \
	$(WIN32RES) \
	statement_history.o

EXTENSION = statement_history
DATA = statement_history--1.0.sql
PGFILEDESC = "statement_history - track planning and execution statistics and other performance related info of all SQL statements executed"

LDFLAGS_SL += $(filter -lm, $(LIBS))

REGRESS_OPTS = --temp-config $(top_srcdir)/contrib/statement_history/statement_history.conf
REGRESS = select dml cursors utility level_tracking planning \
	user_activity wal cleanup oldextversions

NO_INSTALLCHECK = 1

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/statement_history
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
