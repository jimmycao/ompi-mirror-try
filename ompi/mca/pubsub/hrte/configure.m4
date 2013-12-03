# -*- shell-script -*-
#
# Copyright (c) 2011      Los Alamos National Security, LLC.
#                         All rights reserved.
#
# $COPYRIGHT$
# 
# Additional copyrights may follow
# 
# $HEADER$
#

# MCA_pubsub_hrte_CONFIG([action-if-found], [action-if-not-found])
# -----------------------------------------------------------
AC_DEFUN([MCA_ompi_pubsub_hrte_CONFIG],[
    AC_CONFIG_FILES([ompi/mca/pubsub/hrte/Makefile])

    AC_ARG_WITH([hrte],
        AC_HELP_STRING([--with-hrte],
                       [Use HRTE run-time environment (default: no)]))
    AS_IF([test "$with_hrte" != "no"],
          [$1],
          [$2])
])
