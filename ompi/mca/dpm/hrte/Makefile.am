#
# Copyright (c) 2004-2006 The Regents of the University of California.
#                         All rights reserved.
# Copyright (c) 2009      High Performance Computing Center Stuttgart, 
#                         University of Stuttgart.  All rights reserved.
# Copyright (c) 2010      Cisco Systems, Inc.  All rights reserved.
# $COPYRIGHT$
# 
# Additional copyrights may follow
# 
# $HEADER$
#


dist_pkgdata_DATA = help-ompi-dpm-hrte.txt

# Make the output library in this directory, and name it either
# mca_<type>_<name>.la (for DSO builds) or libmca_<type>_<name>.la
# (for static builds).

if MCA_BUILD_ompi_dpm_hrte_DSO
component_noinst =
component_install = mca_dpm_hrte.la
else
component_noinst = libmca_dpm_hrte.la
component_install =
endif

local_sources = \
	dpm_hrte.c \
	dpm_hrte.h \
	dpm_hrte_component.c

mcacomponentdir = $(pkglibdir)
mcacomponent_LTLIBRARIES = $(component_install)
mca_dpm_hrte_la_SOURCES = $(local_sources)
mca_dpm_hrte_la_LDFLAGS = -module -avoid-version $(dpm_hrte_LDFLAGS)

noinst_LTLIBRARIES = $(component_noinst)
libmca_dpm_hrte_la_SOURCES = $(local_sources)
libmca_dpm_hrte_la_LIBADD = $(dpm_hrte_LIBS)
libmca_dpm_hrte_la_LDFLAGS = -module -avoid-version $(dpm_hrte_LDFLAGS)

