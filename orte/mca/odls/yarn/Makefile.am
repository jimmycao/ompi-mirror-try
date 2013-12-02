#AUTOMAKE_OPTIONS = foreign
#AM_CPPFLAGS = -I$(includedir)/openmpi -I$(includedir)/openmpi/opal/mca/event/libevent2019/libevent -I$(includedir)/openmpi/opal/mca/hwloc/hwloc152/hwloc/include

sources = \
        odls_yarn.h \
        odls_yarn_component.c \
        odls_yarn_module.c

mcacomponentdir = $(libdir)/openmpi
mcacomponent_LTLIBRARIES = mca_odls_yarn.la
mca_odls_yarn_la_SOURCES = $(sources) 
#mca_odls_yarn_la_LIBADD = $(libdir)/libopen-rte.la 
mca_odls_yarn_la_LDFLAGS = -module -avoid-version 
