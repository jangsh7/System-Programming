////////////////////////////////////////////////////////////////////////
// File Name : proxy_cache.c                                          //
// Date : 2025/05/22                                                  //
// OS : Ubuntu 20.04.6 LTS 64bits                                     //
// Author : Seo Hyeon JANG                                            //
// Student ID : 2023202062                                            //
// ------------------------------------------------------------------ //
// Title : System Programming Assignment #2-4 (proxy server)          //
// Description :                                                      //
//   - Implements a proxy server that receives GET requests.          //
//   - Determines cache HIT or MISS and caches responses.             //
//   - Filters out non-user-intended resources from logging.          //
//   - Logs user-requested access and termination status.             //
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

#define PORT 39999
#define BUF_SIZE 8192

int client_sock;
char filepath[2048];
time_t server_start_time = 0;

///////////////////////////////////////////////////////////////////////////////////
// get_home_path                                                                 //
// ============================================================================= //
// Input:  const char* subpath - Subdirectory name                               //
// Output: char* - Full path under home directory                                //
// Purpose: Builds a full path by appending subpath to the user's home directory //
///////////////////////////////////////////////////////////////////////////////////
char *get_home_path(const char *subpath) {
    static char path[2048];  // Buffer for the full path
    const char *home = getenv("HOME");  // Get $HOME env var
    if (!home) home = getpwuid(getuid())->pw_dir;  // Fallback to passwd DB
    snprintf(path, sizeof(path), "%s/%s", home, subpath);  // Build full path
    return path;
}

///////////////////////////////////////////////////
// log_to_file                                   //
// ============================================= //
// Input:  const char* msg - Log message         //
// Output: None                                  //
// Purpose: Appends a log message to the logfile //
///////////////////////////////////////////////////
void log_to_file(const char *msg) {
    char log_path[2048];  // Buffer to hold path to logfile
    snprintf(log_path, sizeof(log_path), "%s/logfile/logfile.txt", get_home_path(""));
    FILE *fp = fopen(log_path, "a");  // Open log file in append mode
    if (fp) {
        fprintf(fp, "%s\n", msg);  // Write message
        fclose(fp);  // Close file
    }
}

/////////////////////////////////////////////////////////////////
// get_time_str                                                //
// =========================================================== //
// Input: None                                                 //
// Output: char* - Current time as formatted string            //
// Purpose: Retrieves current time in readable format for logs //
/////////////////////////////////////////////////////////////////
char *get_time_str() {
    static char buf[64];  // Buffer for time string
    time_t t = time(NULL);  // Current time
    struct tm *lt = localtime(&t);  // Convert to local time
    snprintf(buf, sizeof(buf), "%04d/%02d/%02d, %02d:%02d:%02d", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min, lt->tm_sec);
    return buf;
}

/////////////////////////////////////////////////////
// sha1_hash                                       //
// =============================================== //
// Input:  char* input - Original URL              //
//         char* hex_output - Buffer to store hash //
// Output: None                                    //
// Purpose: Computes SHA1 hash of input string     //
/////////////////////////////////////////////////////
void sha1_hash(char *input, char *hex_output) {
    unsigned char hash[SHA_DIGEST_LENGTH];  // Binary hash buffer
    SHA1((unsigned char*)input, strlen(input), hash);  // Compute SHA1
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
        sprintf(hex_output + (i * 2), "%02x", hash[i]);  // Convert to hex string
    hex_output[40] = '\0';  // Null terminate
}

////////////////////////////////////////////////////////////////////////
// is_loggable_url                                                    //
// ================================================================== //
// Input:  const char* url - Requested URL                            //
// Output: int - 1 if loggable, 0 otherwise                           //
// Purpose: Filters out URLs related to static or automatic resources //
////////////////////////////////////////////////////////////////////////
int is_loggable_url(const char *url) {
    if (strchr(url, '?')) return 0;  // Skip URLs with parameters
    const char *skip_exts[] = {
        "favicon.ico", ".css", ".js", ".png", ".jpg", ".jpeg", ".gif",
        ".svg", ".ico", ".woff", ".woff2", ".ttf", ".eot", ".otf", ".webp",
        "detectportal.firefox.com", "cdn-cgi", "fonts.googleapis.com", "fonts.gstatic.com", NULL
    };
    for (int i = 0; skip_exts[i]; i++) {
        if (strstr(url, skip_exts[i])) return 0;  // Skip if URL contains one of the extensions/domains
    }
    return 1;  // Loggable URL
}

////////////////////////////////////////////////////////////////////
// alarm_handler                                                  //
// ============================================================== //
// Input:  int sig - signal number                                //
// Output: None                                                   //
// Purpose: Dummy handler for SIGALRM to interrupt blocking calls //
////////////////////////////////////////////////////////////////////
void alarm_handler(int sig) {}

//////////////////////////////////////////////////////////////////
// sigchld_handler                                              //
// ============================================================ //
// Input:  int sig - signal number                              //
// Output: None                                                 //
// Purpose: Reaps terminated child processes to prevent zombies //
//////////////////////////////////////////////////////////////////
void sigchld_handler(int sig) {
    while (waitpid(-1, NULL, WNOHANG) > 0);  // Non-blocking reap
}

//////////////////////////////////////////////////////////////
// count_miss_requests_from_log                             //
// ======================================================== //
// Input: None                                              //
// Output: int - Number of MISS log entries                 //
// Purpose: Count how many times MISS occurred from logfile //
//////////////////////////////////////////////////////////////
int count_miss_requests_from_log() {
    char path[2048];
    snprintf(path, sizeof(path), "%s/logfile/logfile.txt", get_home_path(""));  // Build log file path
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;  // Return 0 if file not found
    char line[1024];
    int count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "[MISS]")) count++;  // Count MISS entries
    }
    fclose(fp);
    return count;
}

//////////////////////////////////////////////////////////////////////////
// sigint_handler                                                       //
// ==================================================================== //
// Input:  int sig - SIGINT signal                                      //
// Output: None                                                         //
// Purpose: Handles Ctrl+C by logging server run time and shutting down //
//////////////////////////////////////////////////////////////////////////
void sigint_handler(int sig) {
    if (getpid() != getpgrp()) return;  // Only main process logs termination
    time_t end_time = time(NULL);
    int miss_count = count_miss_requests_from_log();
    char log_path[2048];
    snprintf(log_path, sizeof(log_path), "%s/logfile/logfile.txt", get_home_path(""));

    FILE *fp = fopen(log_path, "a");
    if (fp) {
        fprintf(fp, "**SERVER** [Terminated] run time: %ld sec. #sub process: %d\n", end_time - server_start_time, miss_count);
        fclose(fp);
    }
    printf("\n[!] Server terminated by SIGINT\n");
    exit(0);
}

/////////////////////////////////////////////////////////////////
// handle_browser                                              //
// =========================================================== //
// Input:  int sock - connected socket                         //
//         struct sockaddr_in cli_addr - client address        //
// Output: None                                                //
// Purpose: Handles a single HTTP GET request from the browser //
/////////////////////////////////////////////////////////////////
void handle_browser(int sock, struct sockaddr_in cli_addr) {
    client_sock = sock;  // Save socket globally

    // Set up alarm signal
    struct sigaction sa;
    sa.sa_handler = alarm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);

    time_t start_time = time(NULL);
    char buf[BUF_SIZE];
    int len;
    int logged_once = 0;  // Prevent multiple logs for auto-requests

    while ((len = recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[len] = '\0';  // Null-terminate buffer

        char method[16], url[1024], protocol[16], pure_url[1024];
        if (sscanf(buf, "%15s %1023s %15s", method, url, protocol) != 3) continue;
        if (strncmp(method, "GET", 3) != 0) continue;  // Only process GET
        sscanf(url, "http://%1023s", pure_url);  // Strip prefix

        char hash[41];
        sha1_hash(pure_url, hash);  // Generate SHA1 hash

        char *cachedir = get_home_path("cache");
        char subdir[1024];
        snprintf(subdir, sizeof(subdir), "%s/%c%c%c", cachedir, hash[0], hash[1], hash[2]);
        mkdir(cachedir, 0755);
        mkdir(subdir, 0755);
        snprintf(filepath, sizeof(filepath), "%s/%s", subdir, hash + 3);

        int is_hit = access(filepath, F_OK) == 0;  // Check if file exists
        char *time_str = get_time_str();

        if (is_hit) {
            // HIT: Read from cache
            FILE *fp = fopen(filepath, "r");
            if (fp) {
                char data[BUF_SIZE];
                int bytes;
                while ((bytes = fread(data, 1, sizeof(data), fp)) > 0)
                    send(sock, data, bytes, 0);
                fclose(fp);
            }
            if (!logged_once && is_loggable_url(pure_url)) {
                char log1[1024], log2[1024];
                snprintf(log1, sizeof(log1), "[HIT] ServerPID: %d | %.3s/%.37s -[%s]", getpid(), hash, hash + 3, time_str);
                snprintf(log2, sizeof(log2), "[HIT]http://%.900s", pure_url);
                log_to_file(log1); log_to_file(log2);
                printf("%s\n%s\n", log1, log2);
                logged_once = 1;
            }
        } else {
            // MISS: Fetch from server
            char host[256], resource[1024] = "/";
            sscanf(pure_url, "%255[^/]%1023s", host, resource);

            int web_fd = socket(AF_INET, SOCK_STREAM, 0);
            struct hostent *he = gethostbyname(host);
            if (!he) {
                close(sock); exit(1);
            }

            struct sockaddr_in web_addr = {
                .sin_family = AF_INET,
                .sin_port = htons(80),
            };
            memcpy(&web_addr.sin_addr, he->h_addr, he->h_length);

            if (connect(web_fd, (struct sockaddr *)&web_addr, sizeof(web_addr)) < 0) {
                if (filepath[0]) unlink(filepath);
                close(web_fd); close(sock); exit(1);
            }

            char request[1024];
            snprintf(request, sizeof(request), "GET %.512s HTTP/1.0\r\nHost: %.256s\r\nConnection: close\r\n\r\n", resource, host);
            send(web_fd, request, strlen(request), 0);
            alarm(10);

            FILE *cache_fp = fopen(filepath, "w");
            int bytes;
            char data[BUF_SIZE];
            while ((bytes = recv(web_fd, data, sizeof(data), 0)) > 0) {
                send(sock, data, bytes, 0);
                if (cache_fp) fwrite(data, 1, bytes, cache_fp);
            }
            if (cache_fp) fclose(cache_fp);
            alarm(0); close(web_fd);

            if (!logged_once && is_loggable_url(pure_url)) {
                char log[1024];
                snprintf(log, sizeof(log), "[MISS] ServerPID: %d | http://%.900s -[%s]", getpid(), pure_url, time_str);
                log_to_file(log);
                printf("%s\n", log);
                logged_once = 1;
            }
        }

        if (logged_once) {
            time_t end_time = time(NULL);
            char term_log[1024];
            snprintf(term_log, sizeof(term_log), "[Terminated] ServerPID: %d | run time : %ld sec. #request hit : %d, miss : %d", getpid(), end_time - start_time, is_hit, !is_hit);
            log_to_file(term_log);
            printf("%s\n", term_log);
        }
    }

    close(sock);  // Close socket
    exit(0);  // Terminate child process
}

//////////////////////////////////////////////////////////
// main                                                 //
// ==================================================== //
// Input: None                                          //
// Output: int - Exit status                            //
// Purpose: Sets up and runs the main proxy server loop //
//////////////////////////////////////////////////////////
int main() {
    signal(SIGCHLD, sigchld_handler);  // Handle terminated children
    if (getpid() == getpgrp()) signal(SIGINT, sigint_handler);  // Only main process handles SIGINT
    server_start_time = time(NULL);  // Start time for uptime

    mkdir(get_home_path("cache"), 0755);     // Ensure cache directory
    mkdir(get_home_path("logfile"), 0755);   // Ensure log directory

    int serv_sock = socket(AF_INET, SOCK_STREAM, 0);  // Create server socket
    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(PORT),
    };
    bind(serv_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));  // Bind to address
    listen(serv_sock, 10);  // Start listening for connections
    printf("Proxy server is running on port %d...\n", PORT);

    while (1) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int client = accept(serv_sock, (struct sockaddr *)&cli_addr, &cli_len);  // Accept new connection
        if (fork() == 0) {  // Fork child to handle client
            close(serv_sock);
            handle_browser(client, cli_addr);
        }
        close(client);  // Parent closes client socket
    }
    return 0;
}

