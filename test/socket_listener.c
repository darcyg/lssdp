#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>     // select
#include <arpa/inet.h>  // sockaddr_in
#include <sys/time.h>   // gettimeofday
#include "lssdp.h"

/* socket_listener.c
 *
 * 1. create SSDP socket with port 1900
 * 2. select SSDP socket with timeout 0.5 seconds
 *    - when select return value > 0, invoke lssdp_read_socket
 * 3. per 5 seconds do:
 *    - send M-SEARCH and NOTIFY
 *    - check neighbor timeout
 *    - show neighbor list
 */

int log_callback(const char * file, const char * tag, const char * level, int line, const char * func, const char * message) {
    printf("[%s][%s] %s: %s", level, tag, func, message);
    return 0;
}

long get_current_time() {
    struct timeval time = {};
    if (gettimeofday(&time, NULL) == -1) {
        printf("gettimeofday failed, errno = %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    return (time.tv_sec * 1000) + (time.tv_usec / 1000);
}

int main() {
    lssdp_set_log_callback(log_callback);

    lssdp_ctx lssdp = {
        .sock = -1,
        .port = 1900,
        .neighbor_timeout = 15000,  // 15 seconds
        .header = {
            .st            = "ST_P2P",
            .usn           = "f835dd0001",
            .sm_id         = "700000123",
            .device_type   = "BUZZI",
            .location.port = 5678
        }
    };

    // get network interface
    lssdp_get_network_interface(&lssdp);

    if (lssdp_create_socket(&lssdp) != 0) {
        puts("SSDP create socket failed");
        return -1;
    }

    printf("SSDP socket = %d\n", lssdp.sock);

    long last_time = get_current_time();
    if (last_time < 0) return EXIT_SUCCESS;

    // Main Loop
    for (;;) {
        fd_set fs;
        FD_ZERO(&fs);
        FD_SET(lssdp.sock, &fs);
        struct timeval tv = {
            .tv_usec = 500 * 1000   // 500 ms
        };

        int ret = select(lssdp.sock + 1, &fs, NULL, NULL, &tv);
        if (ret < 0) {
            printf("select error, ret = %d\n", ret);
            break;
        }

        if (ret > 0) {
            lssdp_read_socket(&lssdp);
        }

        // get current time
        long current_time = get_current_time();
        if (current_time < 0) break;

        // doing task per 5 seconds
        if (current_time - last_time >= 5000) {

            // 1. send M-SEARCH
            lssdp_send_msearch(&lssdp);

            // 2. send NOTIFY
            lssdp_send_notify(&lssdp);

            // 3. check neighbor timeout
            lssdp_check_neighbor_timeout(&lssdp);

            // 4. show neighbor list
            int i = 0;
            lssdp_nbr * nbr;
            printf("\nSSDP List:\n");
            for (nbr = lssdp.neighbor_list; nbr != NULL; nbr = nbr->next) {
                printf("%d. id = %-9s, ip = %-15s, name = %-10s, device_type = %-8s (%ld)\n",
                    ++i,
                    nbr->sm_id,
                    nbr->location,
                    nbr->name,
                    nbr->device_type,
                    nbr->update_time
                );
            }
            printf("%s\n", i == 0 ? "Empty" : "");

            // update last_time
            last_time = current_time;
        }
    }

    return EXIT_SUCCESS;
}
