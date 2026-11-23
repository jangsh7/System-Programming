///////////////////////////////////////////////////////////////////
// File Name : proxy_cache.c                                     //
// Date : 2025/05/28                                             //
// OS : Ubuntu 20.04.6 LTS 64bits                                //
// Author : Seo Hyeon JANG                                       //
// Student ID : 2023202062                                       //
// ------------------------------------------------------------- //
// Title : System Programming Assignment #3-1 (proxy server)     //
// Description :                                                 //
//   - Implements a proxy server that handles HTTP GET requests. //
//   - Caches responses using SHA-1 hashed URLs.                 //
//   - Logs HIT/MISS requests with timestamp and PID.            //
//   - Controls concurrent logfile access using semaphore.       //
///////////////////////////////////////////////////////////////////

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

#define PORT 39999
#define BUF_SIZE 8192

int client_sock;
char filepath[2048];
time_t server_start_time = 0;
int semid;

// System V semaphore union definition
union semun { int val; struct semid_ds *buf; unsigned short *array; };

////////////////////////////////////////////////////////////
// sem_wait                                               //
// ====================================================== //
// Input:  int semid -> Semaphore ID                      //
// Output: void                                           //
// Purpose: Decrement semaphore to enter critical section //
////////////////////////////////////////////////////////////
void sem_wait(int semid) {
    // Define a semaphore operation: decrement (P operation) on semaphore 0
    // SEM_UNDO ensures the operation is undone if the process terminates unexpectedly
    struct sembuf p = {0, -1, SEM_UNDO};
    printf("*PID# %d is waiting for the semaphore.\n", getpid());
    semop(semid, &p, 1);  // Perform the semaphore operation (may block if semaphore value is 0)
    printf("*PID# %d is in the critical zone.\n", getpid());
}

////////////////////////////////////////////////////////////
// sem_signal                                             //
// ====================================================== //
// Input:  int semid -> Semaphore ID                      //
// Output: void                                           //
// Purpose: Increment semaphore to leave critical section //
////////////////////////////////////////////////////////////
void sem_signal(int semid) {
    // Define a semaphore operation: increment (V operation) on semaphore 0
    // SEM_UNDO ensures the operation is reversed if the process exits unexpectedly
    struct sembuf v = {0, 1, SEM_UNDO};
    semop(semid, &v, 1);  // Perform the semaphore operation (increment the value to release the critical section)
    printf("*PID# %d exited the critical zone.\n", getpid());
}

////////////////////////////////////////////////////////////////////
// get_home_path                                                  //
// ============================================================== //
// Input:  const char* subpath -> Relative subdirectory           //
// Output: char* -> Absolute full path                            //
// Purpose: Return the full path within the user's home directory //
////////////////////////////////////////////////////////////////////
char *get_home_path(const char *subpath) {
    static char path[2048];  // Static buffer to hold the full path
    const char *home = getenv("HOME");  // Try to get the HOME environment variable
    if (!home) home = getpwuid(getuid())->pw_dir;  // If HOME is not set, retrieve the home directory using the user's UID
    snprintf(path, sizeof(path), "%s/%s", home, subpath);
    return path;
}

////////////////////////////////////////////////////////////////////////
// log_to_file                                                        //
// ================================================================== //
// Input:  const char* msg -> Log message string                      //
// Output: void                                                       //
// Purpose: Write a message to the log file with semaphore protection //
////////////////////////////////////////////////////////////////////////
void log_to_file(const char *msg) {
    char log_path[2048];
    snprintf(log_path, sizeof(log_path), "%s/logfile/logfile.txt", get_home_path(""));

    sem_wait(semid);  // Enter critical section
    sleep(3);  // Visualize concurrency
    FILE *fp = fopen(log_path, "a");
    if (fp) {
        fprintf(fp, "%s\n", msg);  // Write log message
        fclose(fp);
    }
    sem_signal(semid);  // Exit critical section
}

/////////////////////////////////////////////////////
// get_time_str                                    //
// =============================================== //
// Input:  void                                    //
// Output: char* -> Current timestamp as string    //
// Purpose: Get formatted current time for logging //
/////////////////////////////////////////////////////
char *get_time_str() {
    static char buf[64];  // Static buffer to store the formatted time string
    time_t t = time(NULL);  // Get the current time in seconds since the epoch
    struct tm *lt = localtime(&t);  // Convert the time to local time (broken-down format)
    snprintf(buf, sizeof(buf), "%04d/%02d/%02d, %02d:%02d:%02d", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min, lt->tm_sec);
    return buf;
}

///////////////////////////////////////////////////////////////
// sha1_hash                                                 //
// ========================================================= //
// Input:  char* input -> Original input URL                 //
//         char* hex_output -> Buffer to store hashed result //
// Output: void                                              //
// Purpose: Convert input string to SHA-1 hash (hexadecimal) //
///////////////////////////////////////////////////////////////
void sha1_hash(char *input, char *hex_output) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((unsigned char*)input, strlen(input), hash);
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
        sprintf(hex_output + (i * 2), "%02x", hash[i]);  // Convert byte to hex
    hex_output[40] = '\0';
}

///////////////////////////////////////////////////////////////////////////
// is_loggable_url                                                       //
// ===================================================================== //
// Input:  const char* url -> Target URL string                          //
// Output: int -> 1 if loggable, 0 otherwise                             //
// Purpose: Check whether URL should be logged (filter static resources) //
///////////////////////////////////////////////////////////////////////////
int is_loggable_url(const char *url) {
    if (strchr(url, '?')) return 0;  // Ignore dynamic URLs
    
    // List of file extensions and domains to skip (typically static or background resources)
    const char *skip_exts[] = {
        "favicon.ico", ".css", ".js", ".png", ".jpg", ".jpeg", ".gif", ".svg", ".ico",
        ".woff", ".woff2", ".ttf", ".eot", ".otf", ".webp", "detectportal.firefox.com",
        "cdn-cgi", "fonts.googleapis.com", "fonts.gstatic.com", NULL
    };
    
    // Check if the URL contains any of the skipped patterns
    for (int i = 0; skip_exts[i]; i++)
        if (strstr(url, skip_exts[i])) return 0;
    return 1;
}

///////////////////////////////////////////////////////////////////
// count_miss_requests_from_log                                  //
// ============================================================= //
// Input:  void                                                  //
// Output: int -> Number of [MISS] entries in log file           //
// Purpose: Count cache MISS occurrences for termination summary //
///////////////////////////////////////////////////////////////////
int count_miss_requests_from_log() {
    char path[2048];
    snprintf(path, sizeof(path), "%s/logfile/logfile.txt", get_home_path(""));
    FILE *fp = fopen(path, "r");  // Open the logfile in read mode
    if (!fp) return 0;  // If the file can't be opened, return 0
    
    char line[1024];
    int count = 0;
    
    // Read each line from the logfile
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "[MISS]")) count++;  // If the line contains the keyword "[MISS]", increment the counter
    }
    fclose(fp);
    return count;
}

////////////////////////////////////////////////////////////////////
// sigint_handler                                                 //
// ============================================================== //
// Input:  int sig -> Signal number                               //
// Output: void                                                   //
// Purpose: Gracefully terminate server and log summary on SIGINT //
////////////////////////////////////////////////////////////////////
void sigint_handler(int sig) {
    if (getpid() != getpgrp()) return;  // Ensure this logic runs only in the main (group leader) process
    time_t end_time = time(NULL);  // Get current time for calculating server uptime
    int miss_count = count_miss_requests_from_log();  // Count number of MISS entries in the log file
    char msg[1024];
    snprintf(msg, sizeof(msg), "**SERVER** [Terminated] run time: %ld sec. #sub process: %d", end_time - server_start_time, miss_count);
    log_to_file(msg);  // Write the termination message to the logfile
    printf("\n[!] Server terminated by SIGINT\n");
    kill(0, SIGTERM);  // Send SIGTERM to all processes in the same process group
    semctl(semid, 0, IPC_RMID);  // Remove the semaphore from the system
    exit(0);
}

////////////////////////////////////////
// alarm_handler                      //
// ================================== //
// Input:  int sig -> Signal number   //
// Output: void                       //
// Purpose: Dummy handler for SIGALRM //
////////////////////////////////////////
void alarm_handler(int sig) {}

//////////////////////////////////////////
// sigchld_handler                      //
// ==================================== //
// Input:  int sig -> Signal number     //
// Output: void                         //
// Purpose: Reap zombie child processes //
//////////////////////////////////////////
void sigchld_handler(int sig) { while (waitpid(-1, NULL, WNOHANG) > 0); }

//////////////////////////////////////////////////////////////////////////
// handle_browser                                                       //
// ==================================================================== //
// Input:  int sock -> Client socket                                    //
//         struct sockaddr_in cli_addr -> Client address info           //
// Output: void                                                         //
// Purpose: Handle HTTP GET request from browser and log/cache response //
//////////////////////////////////////////////////////////////////////////
void handle_browser(int sock, struct sockaddr_in cli_addr) {
    sleep(3);  // Simulate concurrency: delay to align start time across child processes
    client_sock = sock;  // Store client socket globally if needed elsewhere

    // Set up a dummy signal handler for SIGALRM
    struct sigaction sa;
    sa.sa_handler = alarm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);

    time_t start_time = time(NULL);  // Mark the start time of this child
    char buf[BUF_SIZE];  // Buffer for receiving HTTP requests
    int len, logged = 0;  // `logged` flag prevents duplicate log entries

    // Continuously receive data from client
    while ((len = recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[len] = '\0';  // Null-terminate the buffer

        char method[16], url[1024], protocol[16], pure_url[1024];

        // Parse HTTP request line (method URL protocol)
        if (sscanf(buf, "%15s %1023s %15s", method, url, protocol) != 3) continue;
        if (strncmp(method, "GET", 3) != 0) continue;  // Only handle GET requests

        sscanf(url, "http://%1023s", pure_url);

        char hash[41];
        sha1_hash(pure_url, hash);

        // Construct the cache directory and subdirectory path
        char *cachedir = get_home_path("cache");
        char subdir[1024];
        snprintf(subdir, sizeof(subdir), "%s/%c%c%c", cachedir, hash[0], hash[1], hash[2]);
        mkdir(cachedir, 0755);  // Ensure base cache directory exists
        mkdir(subdir, 0755);  // Ensure subdirectory exists

        snprintf(filepath, sizeof(filepath), "%s/%s", subdir, hash + 3);

        // Check if the requested URL is already cached
        int is_hit = access(filepath, F_OK) == 0;
        char *time_str = get_time_str();  // Current timestamp
        char full_log[2048] = "";  // Full log message buffer

        if (is_hit) {
            // If cached, read the file and send content to client
            FILE *fp = fopen(filepath, "r");
            if (fp) {
                char data[BUF_SIZE];
                int bytes;
                while ((bytes = fread(data, 1, sizeof(data), fp)) > 0)
                    send(sock, data, bytes, 0);
                fclose(fp);
            }

            // Log only if it’s a meaningful user request and hasn’t been logged yet
            if (!logged && is_loggable_url(pure_url)) {
                snprintf(full_log + strlen(full_log), sizeof(full_log) - strlen(full_log), "[HIT] ServerPID: %d | %.3s/%.37s -[%s]\n[HIT]http://%s", getpid(), hash, hash + 3, time_str, pure_url);
                logged = 1;
            }
        } else {
            // On MISS: split host and resource path
            char host[256], resource[1024] = "/";
            sscanf(pure_url, "%255[^/]%1023s", host, resource);

            // Create a socket to connect to the origin web server
            int web_fd = socket(AF_INET, SOCK_STREAM, 0);
            struct hostent *he = gethostbyname(host);
            if (!he) { close(sock); exit(1); }

            // Configure address of the web server
            struct sockaddr_in web_addr = {
                .sin_family = AF_INET,
                .sin_port = htons(80),
            };
            memcpy(&web_addr.sin_addr, he->h_addr, he->h_length);

            // Connect to the origin server
            if (connect(web_fd, (struct sockaddr *)&web_addr, sizeof(web_addr)) < 0) {
                if (filepath[0]) unlink(filepath);  // Delete file if it was created
                close(web_fd); close(sock); exit(1);
            }

            // Send the HTTP GET request to the web server
            char request[1024];
            snprintf(request, sizeof(request),
                     "GET %.512s HTTP/1.0\r\nHost: %.256s\r\nConnection: close\r\n\r\n",
                     resource, host);
            send(web_fd, request, strlen(request), 0);
            alarm(10);  // Set timeout to prevent hanging

            // Open the cache file to store the response
            FILE *cache_fp = fopen(filepath, "w");
            char data[BUF_SIZE];
            int bytes;

            // Relay data from origin server to client and write to cache
            while ((bytes = recv(web_fd, data, sizeof(data), 0)) > 0) {
                send(sock, data, bytes, 0);
                if (cache_fp) fwrite(data, 1, bytes, cache_fp);
            }

            if (cache_fp) fclose(cache_fp);
            alarm(0);  // Cancel timeout
            close(web_fd);  // Close web server socket

            // Log if this is a valid user-initiated request
            if (!logged && is_loggable_url(pure_url)) {
                snprintf(full_log + strlen(full_log), sizeof(full_log) - strlen(full_log), "[MISS] ServerPID: %d | http://%s -[%s]", getpid(), pure_url, time_str);
                logged = 1;
            }
        }

        // If a loggable request occurred, write termination summary to log
        if (logged) {
            time_t end_time = time(NULL);
            snprintf(full_log + strlen(full_log), sizeof(full_log) - strlen(full_log), "\n[Terminated] ServerPID: %d | run time : %ld sec. #request hit : %d, miss : %d", getpid(), end_time - start_time, is_hit, !is_hit);
            log_to_file(full_log);
        }
    }

    close(sock);
    exit(0);
}


/////////////////////////////////////////////////////////////////////////
// main                                                                //
// =================================================================== //
// Input:  void                                                        //
// Output: int -> Exit status                                          //
// Purpose: Initialize proxy server, accept connections, fork children //
/////////////////////////////////////////////////////////////////////////
int main() {
    signal(SIGCHLD, sigchld_handler);  // Set handler to reap zombie processes when children terminate

    if (getpid() == getpgrp()) signal(SIGINT, sigint_handler);  // Set SIGINT handler only in the parent process group leader

    server_start_time = time(NULL);  // Record the server start time for later use in termination logging

    // Create cache and logfile directories under the user's home path
    mkdir(get_home_path("cache"), 0755);
    mkdir(get_home_path("logfile"), 0755);

    // Create a System V semaphore for critical section control
    semid = semget((key_t)PORT, 1, IPC_CREAT | 0666);  // Use PORT as IPC key
    union semun sem_attr;
    sem_attr.val = 1;  // Initialize semaphore to 1
    semctl(semid, 0, SETVAL, sem_attr);  // Set the value for semaphore 0

    int serv_sock = socket(AF_INET, SOCK_STREAM, 0);  // Create a TCP socket for incoming client connections

    // Set up the server address structure
    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),  // Bind to all interfaces
        .sin_port = htons(PORT),  // Use defined PORT
    };

    bind(serv_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));  // Bind the socket to the specified address and port

    listen(serv_sock, 10);  // Start listening for incoming connections (up to 10 in backlog)

    printf("Proxy server is running on port %d...\n", PORT);

    // Main server loop: accept and handle incoming connections
    while (1) {
        struct sockaddr_in cli_addr;  // Client address structure
        socklen_t cli_len = sizeof(cli_addr);
        int client = accept(serv_sock, (struct sockaddr *)&cli_addr, &cli_len);  // Accept client

        if (fork() == 0) {  // In child process
            close(serv_sock);
            handle_browser(client, cli_addr);  // Process the client's request
        }

        close(client);
    }

    return 0;
}

