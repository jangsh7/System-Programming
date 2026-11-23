////////////////////////////////////////////////////////////////////////
// File Name : proxy_cache.c                                          //
// Date : 2025/06/01                                                  //
// OS : Ubuntu 20.04.6 LTS 64bits                                     //
// Author : Seo Hyeon JANG                                            //
// Student ID : 2023202062                                            //
// ------------------------------------------------------------------ //
// Title : System Programming Assignment #3-2 (proxy server + thread) //
// Description :                                                      //
//   - Implements a proxy server with thread-based HTTP GET handling. //
//   - Each request is handled in a new thread created by a child.    //
//   - Logs activity with semaphore-based protection.                 //
//   - Critical section logs include both PID and TID.                //
////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <errno.h>
#include <pwd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <pthread.h>

#define PORT 39999
#define BUF_SIZE 8192

int semid;
time_t server_start_time = 0;

union semun { int val; struct semid_ds *buf; unsigned short *array; };

////////////////////////////////////////////////////////////////////
// sem_wait                                                       //
// ============================================================== //
// Input:  int semid  -> Semaphore ID                             //
//         pid_t pid  -> Process ID for logging                   //
// Output: void                                                   //
// Purpose: Acquire semaphore and print entry to critical section //
////////////////////////////////////////////////////////////////////
void sem_wait(int semid, pid_t pid) {
    struct sembuf p = {0, -1, SEM_UNDO};
    printf("*PID# %d is waiting for the semaphore.\n", pid);
    semop(semid, &p, 1);  // P operation (wait)
    printf("*PID# %d is in the critical zone.\n", pid);
    printf("*PID# %d create the *TID# %lu.\n", pid, pthread_self());
}

/////////////////////////////////////////////////////////////////////
// sem_signal                                                      //
// =============================================================== //
// Input:  int semid  -> Semaphore ID                              //
//         pid_t pid  -> Process ID for logging                    //
// Output: void                                                    //
// Purpose: Release semaphore and print exit from critical section //
/////////////////////////////////////////////////////////////////////
void sem_signal(int semid, pid_t pid) {
    printf("*TID# %lu is exited.\n", pthread_self());
    printf("*PID# %d exited the critical zone.\n", pid);
    struct sembuf v = {0, 1, SEM_UNDO};
    semop(semid, &v, 1);  // V operation (signal)
}

////////////////////////////////////////////////////////////////////
// get_home_path                                                  //
// ============================================================== //
// Input:  const char* subpath -> Relative path to append to HOME //
// Output: char* -> Absolute path string                          //
// Purpose: Construct full path under user's home directory       //
////////////////////////////////////////////////////////////////////
char *get_home_path(const char *subpath) {
    static char path[2048];
    const char *home = getenv("HOME");
    if (!home) home = getpwuid(getuid())->pw_dir;
    snprintf(path, sizeof(path), "%s/%s", home, subpath);  // combine home and subpath
    return path;
}

/////////////////////////////////////////////////////////////////////
// log_to_file                                                     //
// =============================================================== //
// Input:  const char* msg -> Message to log                       //
//         pid_t pid       -> Process ID for logging               //
// Output: void                                                    //
// Purpose: Write log message to logfile with semaphore protection //
/////////////////////////////////////////////////////////////////////
void log_to_file(const char *msg, pid_t pid) {
    char log_path[2048];
    snprintf(log_path, sizeof(log_path), "%s/logfile/logfile.txt", get_home_path(""));

    sem_wait(semid, pid);  // enter critical section
    sleep(3);  // for visualization purposes
    FILE *fp = fopen(log_path, "a");
    if (fp) {
        fprintf(fp, "%s\n", msg);  // write log
        fclose(fp);
    }
    sem_signal(semid, pid);  // exit critical section
}

///////////////////////////////////////////////////////////////
// get_time_str                                              //
// ========================================================= //
// Output: char* -> Formatted current time string            //
// Purpose: Retrieve current system time in printable format //
///////////////////////////////////////////////////////////////
char *get_time_str() {
    static char buf[64];
    time_t t = time(NULL);  // Get the current time in seconds since the epoch
    struct tm *lt = localtime(&t);  // Convert the time to local time (broken-down format)
    snprintf(buf, sizeof(buf), "%04d/%02d/%02d, %02d:%02d:%02d", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min, lt->tm_sec);
    return buf;
}

//////////////////////////////////////////////////////////////////
// sha1_hash                                                    //
// ============================================================ //
// Input:  char* input      -> Original input string            //
//         char* hex_output -> Buffer to store hex result       //
// Output: void                                                 //
// Purpose: Generate SHA-1 hash in hex format from input string //
//////////////////////////////////////////////////////////////////
void sha1_hash(char *input, char *hex_output) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((unsigned char*)input, strlen(input), hash);  // SHA-1 hashing
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
        sprintf(hex_output + (i * 2), "%02x", hash[i]);  // convert to hex
    hex_output[40] = '\0';
}

///////////////////////////////////////////////////////////////////////////
// is_loggable_url                                                       //
// ===================================================================== //
// Input:  const char* url -> Request URL                                //
// Output: int (0 or 1)                                                  //
// Purpose: Determine if URL should be logged (exclude static resources) //
///////////////////////////////////////////////////////////////////////////
int is_loggable_url(const char *url) {
    if (strchr(url, '?')) return 0;  // exclude dynamic query
    const char *skip_exts[] = {
        "favicon.ico", ".css", ".js", ".png", ".jpg", ".jpeg", ".gif", ".svg", ".ico",
        ".woff", ".woff2", ".ttf", ".eot", ".otf", ".webp", "detectportal.firefox.com",
        "cdn-cgi", "fonts.googleapis.com", "fonts.gstatic.com", NULL
    };
    for (int i = 0; skip_exts[i]; i++)
        if (strstr(url, skip_exts[i])) return 0;  // match static extensions
    return 1;
}

///////////////////////////////////////////////////////////////
// count_miss_requests_from_log                              //
// ========================================================= //
// Output: int -> Number of lines containing "[MISS]"        //
// Purpose: Count the number of cache MISS logs from logfile //
///////////////////////////////////////////////////////////////
int count_miss_requests_from_log() {
    char path[2048];
    snprintf(path, sizeof(path), "%s/logfile/logfile.txt", get_home_path(""));
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;  // If the file can't be opened, return 0

    char line[1024];
    int count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "[MISS]")) count++;  // If the line contains the keyword "[MISS]", increment the counter
    }
    fclose(fp);
    return count;
}

////////////////////////////////////////////////////////////////////////
// sigint_handler                                                     //
// ================================================================== //
// Input:  int sig -> Received signal number                          //
// Output: void                                                       //
// Purpose: Handle SIGINT to cleanly terminate server and log summary //
////////////////////////////////////////////////////////////////////////
void sigint_handler(int sig) {
    if (getpid() != getpgrp()) return;  // only parent process handles

    time_t end_time = time(NULL);
    int miss_count = count_miss_requests_from_log();

    char msg[1024];
    snprintf(msg, sizeof(msg),
             "**SERVER** [Terminated] run time: %ld sec. #sub process: %d",
             end_time - server_start_time, miss_count);

    log_to_file(msg, getpid());  // write final summary log
    printf("\n[!] Server terminated by SIGINT\n");

    kill(0, SIGTERM);  // terminate all processes in group
    semctl(semid, 0, IPC_RMID);  // remove semaphore
    exit(0);
}

///////////////////////////////////////////////////////////////
// handle_request                                            //
// ========================================================= //
// Input:  void* arg -> Pointer to socket descriptor (int*)  //
// Output: void* (unused)                                    //
// Purpose: Handle a single HTTP GET request in a new thread //
///////////////////////////////////////////////////////////////
void *handle_request(void *arg) {
    int sock = *(int *)arg;
    pid_t pid = getpid();
    free(arg);  // free allocated socket memory

    char buf[BUF_SIZE];
    int len, logged = 0;

    // Receive HTTP request from client
    while ((len = recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[len] = '\0';

        char method[16], url[1024], protocol[16], pure_url[1024];
        if (sscanf(buf, "%15s %1023s %15s", method, url, protocol) != 3) continue;
        if (strncmp(method, "GET", 3) != 0) continue;
        sscanf(url, "http://%1023s", pure_url);  // extract pure domain/path

        // Generate SHA-1 hash and cache path
        char hash[41];
        sha1_hash(pure_url, hash);
        char *cachedir = get_home_path("cache");

        char subdir[1024];
        snprintf(subdir, sizeof(subdir), "%s/%c%c%c", cachedir, hash[0], hash[1], hash[2]);
        mkdir(cachedir, 0755);
        mkdir(subdir, 0755);

        char filepath[2048];
        snprintf(filepath, sizeof(filepath), "%s/%s", subdir, hash + 3);

        // Check for cache HIT
        int is_hit = access(filepath, F_OK) == 0;
        char *time_str = get_time_str();
        char full_log[2048] = "";

        if (is_hit) {
            // Serve from cache
            FILE *fp = fopen(filepath, "r");
            if (fp) {
                char data[BUF_SIZE];
                int bytes;
                while ((bytes = fread(data, 1, sizeof(data), fp)) > 0)
                    send(sock, data, bytes, 0);
                fclose(fp);
            }
            // Prepare log if first loggable request
            if (!logged && is_loggable_url(pure_url)) {
                snprintf(full_log + strlen(full_log), sizeof(full_log) - strlen(full_log), "[HIT] ServerPID: %d | %.3s/%.37s -[%s]\n[HIT]http://%s", pid, hash, hash + 3, time_str, pure_url);
                logged = 1;
            }
        } else {
            // Cache MISS: forward to original web server
            char host[256], resource[1024] = "/";
            sscanf(pure_url, "%255[^/]%1023s", host, resource);

            int web_fd = socket(AF_INET, SOCK_STREAM, 0);
            struct hostent *he = gethostbyname(host);
            if (!he) { close(sock); return NULL; }

            struct sockaddr_in web_addr = {
                .sin_family = AF_INET,
                .sin_port = htons(80),
            };
            memcpy(&web_addr.sin_addr, he->h_addr, he->h_length);
            if (connect(web_fd, (struct sockaddr *)&web_addr, sizeof(web_addr)) < 0) {
                close(web_fd); close(sock); return NULL;
            }

            // Construct HTTP GET request
            char request[1024];
            snprintf(request, sizeof(request), "GET %.512s HTTP/1.0\r\nHost: %.256s\r\nConnection: close\r\n\r\n", resource, host);
            send(web_fd, request, strlen(request), 0);

            // Write response to both client and cache
            FILE *cache_fp = fopen(filepath, "w");
            char data[BUF_SIZE];
            int bytes;
            while ((bytes = recv(web_fd, data, sizeof(data), 0)) > 0) {
                send(sock, data, bytes, 0);
                if (cache_fp) fwrite(data, 1, bytes, cache_fp);
            }
            if (cache_fp) fclose(cache_fp);
            close(web_fd);

            // Prepare log
            if (!logged && is_loggable_url(pure_url)) {
                snprintf(full_log + strlen(full_log), sizeof(full_log) - strlen(full_log), "[MISS] ServerPID: %d | http://%s -[%s]", pid, pure_url, time_str);
                logged = 1;
            }
        }

        // Write log if applicable
        if (logged) {
            snprintf(full_log + strlen(full_log), sizeof(full_log) - strlen(full_log), "\n[Terminated] ServerPID: %d | run time : 0 sec. #request hit : %d, miss : %d", pid, is_hit, !is_hit);
            log_to_file(full_log, pid);
        }
    }

    close(sock);
    return NULL;
}

///////////////////////////////////////////////////////////////////////
// main                                                              //
// ================================================================= //
// Output: int -> Exit status                                        //
// Purpose: Start the proxy server, accept clients, fork per client, //
//          and handle requests using threads                        //
///////////////////////////////////////////////////////////////////////
int main() {
    signal(SIGINT, sigint_handler);  // install signal handler
    server_start_time = time(NULL);

    mkdir(get_home_path("cache"), 0755);  // create cache directory
    mkdir(get_home_path("logfile"), 0755);  // create logfile directory

    // Create semaphore for critical section control
    semid = semget((key_t)PORT, 1, IPC_CREAT | 0666);
    union semun sem_attr;
    sem_attr.val = 1;
    semctl(semid, 0, SETVAL, sem_attr);

    // Set up listening socket
    int serv_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(PORT),
    };
    bind(serv_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    listen(serv_sock, 10);

    printf("Proxy server is running on port %d...\n", PORT);

    // Accept loop
    while (1) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int client = accept(serv_sock, (struct sockaddr *)&cli_addr, &cli_len);

        if (fork() == 0) {  // child process
            close(serv_sock);
            int *sock_ptr = malloc(sizeof(int));
            *sock_ptr = client;
            pthread_t tid;
            pthread_create(&tid, NULL, handle_request, sock_ptr);  // create thread
            pthread_detach(tid);  // auto-reclaim resources
            pause();  // wait indefinitely
        }

        close(client);  // parent closes unused socket
    }

    return 0;
}

