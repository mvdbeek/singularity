/* 
 * Copyright (c) 2017, SingularityWare, LLC. All rights reserved.
 * 
 * This software is licensed under a 3-clause BSD license.  Please
 * consult LICENSE file distributed with the sources of this project regarding
 * your rights to use or distribute this software.
 * 
 */


#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#include "config.h"
#include "util/file.h"
#include "util/util.h"
#include "util/registry.h"
#include "util/fork.h"
#include "lib/image/image.h"
#include "lib/runtime/runtime.h"
#include "util/config_parser.h"
#include "util/privilege.h"
#include "util/suid.h"
#include "util/sessiondir.h"
#include "util/cleanupd.h"
#include "util/daemon.h"

#include "./action-lib/include.h"

#ifndef SYSCONFDIR
#error SYSCONFDIR not defined
#endif



int main(int argc, char **argv) {
    struct image_object image;
    char *sinit_bin = joinpath(LIBEXECDIR, "/singularity/bin/sinit"); //path to sinit binary
    char *daemon_fd_str;
    int daemon_fd, i;


    singularity_config_init(joinpath(SYSCONFDIR, "/singularity/singularity.conf"));

    singularity_priv_init();
    singularity_suid_init(argv);

    singularity_registry_init();
    singularity_priv_userns();
    singularity_priv_drop();

    singularity_registry_set("UNSHARE_PID", "1");
    singularity_registry_set("UNSHARE_IPC", "1");

    singularity_cleanupd();

    if ( singularity_registry_get("WRITABLE") != NULL ) {
        singularity_message(VERBOSE3, "Instantiating writable container image object\n");
        image = singularity_image_init(singularity_registry_get("IMAGE"), O_RDWR);
    } else {
        singularity_message(VERBOSE3, "Instantiating read only container image object\n");
        image = singularity_image_init(singularity_registry_get("IMAGE"), O_RDONLY);
    }
        
    singularity_runtime_ns(SR_NS_ALL);
    
    singularity_sessiondir();

    singularity_image_mount(&image, CONTAINER_MOUNTDIR);

    action_ready();

    singularity_runtime_overlayfs();
    singularity_runtime_mounts();
    singularity_runtime_files();

    /* After this point, we are running as PID 1 inside PID NS */
    singularity_message(DEBUG, "Preparing sinit daemon\n");
    singularity_registry_set("ROOTFS", CONTAINER_FINALDIR);
    singularity_daemon_init();

    singularity_message(DEBUG, "We are ready to recieve jobs, sending signal_go_ahead to parent\n");

    daemon_fd_str = singularity_registry_get("DAEMON_FD");
    daemon_fd = atoi(daemon_fd_str);

    singularity_message(DEBUG, "Signaling parent it is ok to go ahead\n");
    singularity_signal_go_ahead(0);

    /* Close all open fd's that may be present besides daemon info file fd */
    singularity_message(DEBUG, "Closing open fd's\n");
    for( i = sysconf(_SC_OPEN_MAX); i >= 2; i-- ) {
        if( i != daemon_fd ) {
            close(i);
        }
    }

    if ( execl(sinit_bin, sinit_bin, NULL) < 0 ) { //Flawfinder: ignore
        singularity_message(ERROR, "Failed to exec sinit: %s\n", strerror(errno));
        ABORT(255);
    }
    
    return(0);
}