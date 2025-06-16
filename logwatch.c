#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <limits.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <ftw.h>
#include <sys/time.h>


#define EVENT_SIZE (sizeof(struct inotify_event))
#define BUF_LEN (1024 *(EVENT_SIZE + 16))
#define LOG_FILE "logwatch.log"
#define WATCH_MASK (IN_CREATE | IN_DELETE | IN_MODIFY | IN_ACCESS | IN_ISDIR)
#define MAX_WATCHES 1024

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    char path[PATH_MAX];
    struct timeval last_logged;
} RecentAccess;

#define MAX_RECENT 256
RecentAccess recent_accesses[MAX_RECENT];
int recent_count = 0;


char *watch_paths[MAX_WATCHES];
int fd, wd;

void printBanner(){  
    printf("\n┓     ┓ ┏    ┓  \n"); 
    printf("┃ ┏┓┏┓┃┃┃┏┓╋┏┣┓ \n");
    printf("┗┛┗┛┗┫┗┻┛┗┻┗┗┛┗ \n");
    printf("     ┛ Nadeen H\n\n");       
}

void handleExit(int sig) {
   
    printf("\nStopping log watcher...\n");
    for (int i = 0; i < MAX_WATCHES; ++i) {
        if (watch_paths[i]) {
            inotify_rm_watch(fd, i);
            free(watch_paths[i]);
        }
    }
    close(fd);
    exit(0);
}


void eventLog(const char *message, const char *filename){
    FILE *log = fopen(LOG_FILE, "a");
    if(!log){
        perror("Error opening log file");
        return;
    }

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%d-%m-%Y %H:%M:%S", t);

    fprintf(log, "[%s] %s: %s\n", timeStr, filename, message);
    printf("[%s] %s: %s\n", timeStr, filename, message);
    fclose(log);
}

int recursive_callback(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    if (typeflag == FTW_D) {
        int this_wd = inotify_add_watch(fd, fpath, WATCH_MASK);
        if (this_wd >= 0) {
            watch_paths[this_wd] = strdup(fpath);
            printf("Watching: %s\n", fpath);
        }
    }
    return 0;
}

int shouldThrottle(const char *path) {
    struct timeval now;
    gettimeofday(&now, NULL);

    for (int i = 0; i < recent_count; ++i) {
        if (strcmp(recent_accesses[i].path, path) == 0) {
            long diff = (now.tv_sec - recent_accesses[i].last_logged.tv_sec) * 1000 +
                        (now.tv_usec - recent_accesses[i].last_logged.tv_usec) / 1000;
            if (diff < 3000)
                return 1;
            recent_accesses[i].last_logged = now;
            return 0;
        }
    }

    if (recent_count < MAX_RECENT) {
        strncpy(recent_accesses[recent_count].path, path, PATH_MAX);
        recent_accesses[recent_count].last_logged = now;
        recent_count++;
    }

    return 0; // do not throttle (it's new)
}

int main(int argc, char **argv){
    printBanner();

    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        printf("Usage: %s [directory_to_logwatch]\n", argv[0]);
        exit(0);
    }           

    signal(SIGINT, handleExit);
    const char *targetDir = argc > 1 ? argv[1] : ".";

    fd = inotify_init();
    if(fd < 0){
        perror("inotify_init");
        exit(EXIT_FAILURE);
    }

    nftw(targetDir, recursive_callback, 16, FTW_PHYS);

    printf("LogWatch is running — monitoring '%s' recursively in real time...\n\n", targetDir);
    
    char buffer[BUF_LEN];
 
    while(1){
        int length = read(fd, buffer, BUF_LEN);
        if(length < 0){
            perror("read");
            break;
        }

        int i = 0;
        while (i < length) {
            struct inotify_event *event = (struct inotify_event *) &buffer[i];

            if (event->wd >= 0 && event->wd < MAX_WATCHES && watch_paths[event->wd]) {
                char full_path[PATH_MAX];
                snprintf(full_path, sizeof(full_path), "%s/%s", watch_paths[event->wd], event->len ? event->name : "");

                if ((event->mask & IN_CREATE) && (event->mask & IN_ISDIR)) {
                    int new_wd = inotify_add_watch(fd, full_path, WATCH_MASK);
                    if (new_wd >= 0 && new_wd < MAX_WATCHES) {
                        watch_paths[new_wd] = strdup(full_path);
                        printf("New directory added to watch: %s\n", full_path);
                    }
                    eventLog("Directory created", full_path);
                } else if (event->mask & IN_CREATE) {
                    eventLog("File created", full_path);
                }

                if ((event->mask & IN_DELETE) && (event->mask & IN_ISDIR)) {
                    eventLog("Directory deleted", full_path);
                } else if (event->mask & IN_DELETE) {
                    eventLog("File deleted", full_path);
                }

                if ((event->mask & IN_MODIFY) && (event->mask & IN_ISDIR)) {
                    eventLog("Directory modified", full_path);
                } else if (event->mask & IN_MODIFY) {
                    eventLog("File modified", full_path);
                }

                if (event->mask & IN_ACCESS) {
                    if (shouldThrottle(full_path)) {
                        // Skip logging (too soon)
                    } else {
                        if (event->mask & IN_ISDIR) {
                            eventLog("Directory accessed", full_path);
                        } else {
                            eventLog("File accessed", full_path);
                        }
                    }
                }
            }
            i += EVENT_SIZE + event->len;
            }
        }
    handleExit(0);
    return 0;
}
