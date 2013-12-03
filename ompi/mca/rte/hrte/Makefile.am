#
# Copyright (c) 2012      Los Alamos National Security, LLC.
#                         All rights reserved.
# $COPYRIGHT$
# 
# Additional copyrights may follow
# 
# $HEADER$
#

headers = rte_hrte.h

sources = \
        rte_hrte_component.c \
        rte_hrte_module.c

# Conditionally install the header files
if WANT_INSTALL_HEADERS
ompidir = $(includedir)/openmpi/$(subdir)
nobase_ompi_HEADERS = $(headers)
endif

# We only ever build this component statically
noinst_LTLIBRARIES = libmca_rte_hrte.la
libmca_rte_hrte_la_SOURCES =$(sources) $(headers)
libmca_rte_hrte_la_LDFLAGS = -module -avoid-version
libmca_rte_hrte_la_LIBADD = $(top_builddir)/orte/libopen-rte.la