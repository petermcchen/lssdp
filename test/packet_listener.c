#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>     // select
#include <sys/time.h>   // gettimeofday
#include "lssdp.h"

/* packet_listener.c
 *
 * show all SSDP packet payload completely
 *
 * 1. create SSDP socket with port 1900
 * 2. select SSDP socket with timeout 0.5 seconds
 *    - when select return value > 0, invoke lssdp_socket_read
 * 3. update network interface per 5 seconds
 * 4. when network interface is changed
 *    - show interface list
 *    - re-bind the socket
 */
FILE *fptr;
/** Struct: lssdp_packet (copy...) **/
typedef struct lssdp_packet {
    char            method      [LSSDP_FIELD_LEN];      // M-SEARCH, NOTIFY, RESPONSE
    char            st          [LSSDP_FIELD_LEN];      // Search Target
    char            usn         [LSSDP_FIELD_LEN];      // Unique Service Name
    char            location    [LSSDP_LOCATION_LEN];   // Location

    /* Additional SSDP Header Fields */
    char            sm_id       [LSSDP_FIELD_LEN];
    char            device_type [LSSDP_FIELD_LEN];
    long long       update_time;
} lssdp_packet;
int Search_in_File(char *);
extern int lssdp_packet_parser(const char *, size_t, lssdp_packet *);
extern int get_colon_index(const char *, size_t, size_t);

void log_callback(const char * file, const char * tag, int level, int line, const char * func, const char * message) {
    char * level_name = "DEBUG";
    if (level == LSSDP_LOG_INFO)   level_name = "INFO";
    if (level == LSSDP_LOG_WARN)   level_name = "WARN";
    if (level == LSSDP_LOG_ERROR)  level_name = "ERROR";

    printf("[%-5s][%s] %s", level_name, tag, message);
}

long long get_current_time() {
    struct timeval time = {};
    if (gettimeofday(&time, NULL) == -1) {
        printf("gettimeofday failed, errno = %s (%d)\n", strerror(errno), errno);
        return -1;
    }
    return (long long) time.tv_sec * 1000 + (long long) time.tv_usec / 1000;
}

int show_interface_list_and_rebind_socket(lssdp_ctx * lssdp) {
    // 1. show interface list
    printf("\nNetwork Interface List (%zu):\n", lssdp->interface_num);
    size_t i;
    for (i = 0; i < lssdp->interface_num; i++) {
        printf("%zu. %-6s: %s\n",
            i + 1,
            lssdp->interface[i].name,
            lssdp->interface[i].ip
        );
    }
    printf("%s\n", i == 0 ? "Empty" : "");

    // 2. re-bind SSDP socket
    if (lssdp_socket_create(lssdp) != 0) {
        puts("SSDP create socket failed");
        return -1;
    }

    return 0;
}

int show_ssdp_packet(struct lssdp_ctx * lssdp, const char * buffer, size_t buffer_len) {
    lssdp_packet packet = {};
    lssdp_packet_parser(buffer, buffer_len, &packet);
    if (strlen(packet.sm_id) > 9) {
        printf(">>>SNO: %s\n", packet.sm_id);
        int colon = get_colon_index(packet.location, 1, strlen(packet.location));
        packet.location[colon] = '\0';
        printf(">>>IP: %s\n", packet.location);

        if (Search_in_File(packet.sm_id) == 0) // not found
        {
            fptr = fopen("/tmp/waltzlist.txt","a");
            fprintf(fptr, "%s %s\n", packet.sm_id, packet.location);
            fclose(fptr);
        }
    }
    return 0;
}

int Search_in_File(char *str) {
	int line_num = 1;
	int find_result = 0;
	char temp[32];

    fptr = fopen("/tmp/waltzlist.txt","r");
    fseek(fptr, 0L, SEEK_END);
    fseek(fptr, 0L, SEEK_SET);
	while(fgets(temp, 32, fptr) != NULL) {
		if((strstr(temp, str)) != NULL) {
			//printf("A match found on line: %d\n", line_num);
			//printf("\n%s\n", temp);
			find_result++;
		}
		line_num++;
	}
	if(find_result == 0) {
		//printf("Sorry, couldn't find a match (%s).\n", str);
	}
    fclose(fptr);
   	return(find_result);
}

int main() {  
    fptr = fopen("/tmp/waltzlist.txt","w");
    fclose(fptr);
    lssdp_set_log_callback(log_callback);

    lssdp_ctx lssdp = {
        .port = 1900,
        // .debug = true,           // debug

        // callback
        .network_interface_changed_callback = show_interface_list_and_rebind_socket,
        .packet_received_callback           = show_ssdp_packet
    };

    /* get network interface at first time, network_interface_changed_callback will be invoke
     * SSDP socket will be created in callback function
     */
    lssdp_network_interface_update(&lssdp);

    long long last_time = get_current_time();
    if (last_time < 0) {
        printf("got invalid timestamp %lld\n", last_time);
        return EXIT_SUCCESS;
    }

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
            lssdp_socket_read(&lssdp);
        }

        // get current time
        long long current_time = get_current_time();
        if (current_time < 0) {
            printf("got invalid timestamp %lld\n", current_time);
            break;
        }

        // doing task per 5 seconds
        if (current_time - last_time >= 5000) {
            lssdp_network_interface_update(&lssdp); // update network interface
            last_time = current_time;               // update last_time
        }
    }
    return EXIT_SUCCESS;
}
