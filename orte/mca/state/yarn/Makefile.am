#AUTOMAKE_OPTIONS = foreign
#AM_CPPFLAGS = -I$(includedir)/openmpi -I$(includedir)/openmpi/opal/mca/event/libevent2019/libevent -I$(includedir)/openmpi/opal/mca/hwloc/hwloc152/hwloc/include

sources = \
        state_yarn.h \
        state_yarn_component.c \
        state_yarn.c

mcacomponentdir = $(libdir)/openmpi
mcacomponent_LTLIBRARIES = mca_state_yarn.la
mca_state_yarn_la_SOURCES = $(sources) 
#mca_state_yarn_la_LIBADD = $(libdir)/libopen-rte.la
mca_state_yarn_la_LDFLAGS = -module -avoid-version