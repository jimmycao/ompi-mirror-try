#AUTOMAKE_OPTIONS = foreign
#AM_CPPFLAGS = -I$(includedir)/openmpi -I$(includedir)/openmpi/opal/mca/event/libevent2019/libevent -I$(includedir)/openmpi/opal/mca/hwloc/hwloc152/hwloc/include

sources = \
        plm_yarn.h \
        plm_yarn_component.c \
        plm_yarn_module.c

mcacomponentdir = $(libdir)/openmpi
mcacomponent_LTLIBRARIES = mca_plm_yarn.la
mca_plm_yarn_la_SOURCES = $(sources) 
#mca_plm_yarn_la_LIBADD = $(libdir)/libopen-rte.la $(top_builddir)/common/libhamster_common.la
mca_plm_yarn_la_LIBADD = ../../common/libhamster_common.la
mca_plm_yarn_la_LDFLAGS = -module -avoid-version
#mca_plm_yarn_la_DEPENDENCIES = $(top_builddir)/common/libhamster_common.la