/*
 * $HEADER$
 *
 */

#include <stdio.h>
#include <string.h>
#include <libgen.h>
#include <sys/stat.h>

#include "ompi_config.h"
#include "util/proc_info.h"
#include "util/os_path.h"
#include "util/session_dir.h"
#include "runtime/universe_init.h"

char *ompi_universe_init(char *tmpdir, char *user, char *universe)
{
    char *uri, *tmp, *oob_info_path;
    FILE *fp;


    if (OMPI_ERROR == ompi_session_dir(true, tmpdir, user, NULL, NULL, universe, "seed", NULL)) {
	/* not found, and could not be created */
	return(NULL);
    }

    oob_info_path = ompi_os_path(false, ompi_process_info.job_session_dir, "oob", NULL);
    fp = fopen(oob_info_path, "w"); /* must be a new file - fail if one already exists */
    if (NULL == fp) {
	return(NULL);
    }

    uri = strdup("dummy_uri_for_seed_oob");
    fclose(fp);
    return(ompi_process_info.job_session_dir);
}
