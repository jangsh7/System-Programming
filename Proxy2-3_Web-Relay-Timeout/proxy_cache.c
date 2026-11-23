////////////////////////////////////////////////////////////////////////
// File Name : proxy_cache.c                                          //
// Date : 2025/05/15                                                  //
// OS : Ubuntu 20.04.6 LTS 64bits                                     //
// Author : Seo Hyeon JANG                                            //
// Student ID : 2023202062                                            //
// ------------------------------------------------------------------ //
// Title : System Programming Assignment #2-3 (proxy server)          //
// Description :                                                      //
//   - Implements a proxy server that handles HTTP GET requests.      //
//   - Caches web content using SHA-1 hash of the requested URL.      //
//   - Distinguishes between HIT and MISS using local file existence. //
//   - Logs all activities into ~/logfile/logfile.txt.                //
//   - Handles timeout and disconnection gracefully.                  //
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

char *student_id = "kw2023202062";
int client_sock;
char filepath[2048];
int already_handled = 0;

///////////////////////////////////////////////////////////////////
// get_home_path                                                 //
// ============================================================= //
// Input:                                                        //
//   - const char *subpath : Subdirectory path under HOME        //
// Output:                                                       //
//   - char * : Full absolute path to the given subdirectory     //
// Purpose: Construct a full path from the user's home directory //
///////////////////////////////////////////////////////////////////
char *get_home_path(const char *subpath) {
    static char path[2048];
    const char *home = getenv("HOME");
    if (!home) home = getpwuid(getuid())->pw_dir;
    snprintf(path, sizeof(path), "%s/%s", home, subpath);
    return path;
}

////////////////////////////////////////////////////////////////
// log_to_file                                                //
// ========================================================== //
// Input:                                                     //
//   - const char *msg : A log message to write               //
// Output:                                                    //
//   - None                                                   //
// Purpose: Appends log message to logfile.txt in ~/logfile/  //
////////////////////////////////////////////////////////////////
void log_to_file(const char *msg) {
    char log_path[2048];
    snprintf(log_path, sizeof(log_path), "%s/logfile/logfile.txt", get_home_path(""));
    FILE *fp = fopen(log_path, "a");
    if (fp) {
        fprintf(fp, "%s\n", msg);
        fclose(fp);
    }
}

//////////////////////////////////////////////////////////////
// get_time_str                                             //
// ======================================================== //
// Input: None                                              //
// Output:                                                  //
//   - char * : Formatted current time string               //
// Purpose: Get current time in YYYY/MM/DD, HH:MM:SS format //
//////////////////////////////////////////////////////////////
char *get_time_str() {
    static char buf[64];  // Buffer to store the formatted time string

    time_t t = time(NULL);  // Get current time in seconds since the epoch
    struct tm *lt = localtime(&t);  // Convert to local time (broken-down time)

    // Format time as "YYYY/MM/DD, HH:MM:SS" and store it in buf
    snprintf(buf, sizeof(buf), "%04d/%02d/%02d, %02d:%02d:%02d", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min, lt->tm_sec);

    return buf;  // Return pointer to formatted time string
}


//////////////////////////////////////////////////////////////
// sha1_hash                                                //
// ======================================================== //
// Input:                                                   //
//   - char *input : URL string to hash                     //
//   - char *hex_output : Output buffer for hashed result   //
// Output:                                                  //
//   - None                                                 //
// Purpose: Generate SHA-1 hash string (hex) from input URL //
//////////////////////////////////////////////////////////////
void sha1_hash(char *input, char *hex_output) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((unsigned char*)input, strlen(input), hash);
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++)
        sprintf(hex_output + (i * 2), "%02x", hash[i]);
    hex_output[40] = '\0';
}

////////////////////////////////////////////////////////////////////
// alarm_handler                                                  //
// ============================================================== //
// Input:                                                         //
//   - int sig : Signal number (expected SIGALRM)                 //
// Output: None                                                   //
// Purpose: Dummy handler for SIGALRM to interrupt blocking calls //
////////////////////////////////////////////////////////////////////
void alarm_handler(int sig) {}

/////////////////////////////////////////////////////////////////
// sigchld_handler                                             //
// =========================================================== //
// Input:                                                      //
//   - int sig : Signal number (expected SIGCHLD)              //
// Output: None                                                //
// Purpose: Reap terminated child processes to prevent zombies //
/////////////////////////////////////////////////////////////////
void sigchld_handler(int sig) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

//////////////////////////////////////////////////////////////////////////////////
// is_user_request                                                              //
// ============================================================================ //
// Input:                                                                       //
//   - const char *url : The requested URL string                               //
// Output:                                                                      //
//   - int : 1 if the request is from user (not favicon or portal), 0 otherwise //
// Purpose: Filter out automatic or background browser requests                 //
//////////////////////////////////////////////////////////////////////////////////
int is_user_request(const char *url) {
    return !(strstr(url, "favicon.ico") || strstr(url, "detectportal.firefox.com"));
}

///////////////////////////////////////////////////////////////
// handle_browser                                            //
// ========================================================= //
// Input:                                                    //
//   - int sock : Client socket descriptor                   //
//   - struct sockaddr_in cli_addr : Client IP and port info //
// Output:                                                   //
//   - None (exits child process after request handling)     //
// Purpose: Handle the entire HTTP request/response cycle    //
//          from the browser, with caching and logging       //
///////////////////////////////////////////////////////////////
void handle_browser(int sock, struct sockaddr_in cli_addr) {
    client_sock = sock;

    // Set up SIGALRM handler for timeout
    struct sigaction sa;
    sa.sa_handler = alarm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);

    time_t start_time = time(NULL);
    char buf[BUF_SIZE];
    int len;

    while ((len = recv(sock, buf, sizeof(buf) - 1, 0)) > 0) {
        buf[len] = '\0';

        // Parse request line: GET URL HTTP/1.1
        char method[16], url[1024], protocol[16], pure_url[1024];
        if (sscanf(buf, "%15s %1023s %15s", method, url, protocol) != 3) continue;
        if (strncmp(method, "GET", 3) != 0) continue;
        sscanf(url, "http://%1023s", pure_url);
        if (!is_user_request(pure_url)) continue;
        if (already_handled) continue;  // Only process first valid request
        already_handled = 1;

        // Print request info to terminal
        printf("[%s : %d] client was connected\n",
               inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
        printf("================================================\n");
        printf("Request from [%s : %d]\n", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
        char *line = strtok(buf, "\r\n");
        while (line) {
            printf("%s\n", line);
            line = strtok(NULL, "\r\n");
        }
        printf("================================================\n");

        // Hash URL and prepare cache path
        char hash[41];
        sha1_hash(pure_url, hash);

        char *cachedir = get_home_path("cache");
        char subdir[1024];
        snprintf(subdir, sizeof(subdir), "%s/%c%c%c", cachedir, hash[0], hash[1], hash[2]);
        mkdir(cachedir, 0755);
        mkdir(subdir, 0755);
        snprintf(filepath, sizeof(filepath), "%s/%s", subdir, hash + 3);

        int is_hit = access(filepath, F_OK) == 0;
        char *time_str = get_time_str();

        if (is_hit) {
            // Serve content from cache
            FILE *fp = fopen(filepath, "r");
            if (fp) {
                char data[BUF_SIZE];
                int bytes;
                while ((bytes = fread(data, 1, sizeof(data), fp)) > 0)
                    send(sock, data, bytes, 0);
                fclose(fp);
            }
            // Log HIT
            char log1[1024], log2[1024];
            snprintf(log1, sizeof(log1), "[HIT] ServerPID: %d | %.3s/%.37s -[%s]",
                     getpid(), hash, hash + 3, time_str);
            snprintf(log2, sizeof(log2), "[HIT]http://%.900s", pure_url);
            log_to_file(log1);
            log_to_file(log2);
        } else {
            // Parse host/resource from URL
            char host[256], resource[1024] = "/";
            sscanf(pure_url, "%255[^/]%1023s", host, resource);

            // Connect to original web server
            int web_fd = socket(AF_INET, SOCK_STREAM, 0);
            struct hostent *he = gethostbyname(host);
            if (!he) {
                perror("gethostbyname");
                printf("========================================\n");
                printf("========No Response========\n");
                printf("========================================\n");
                close(sock);
                exit(1);
            }

            struct sockaddr_in web_addr = {
                .sin_family = AF_INET,
                .sin_port = htons(80),
            };
            memcpy(&web_addr.sin_addr, he->h_addr, he->h_length);

            if (connect(web_fd, (struct sockaddr *)&web_addr, sizeof(web_addr)) < 0) {
                perror("connect to web server");
                printf("========================================\n");
                printf("========No Response========\n");
                printf("========================================\n");
                if (filepath[0]) unlink(filepath);  // Remove partial cache
                close(web_fd);
                close(sock);
                exit(1);
            }

            // Send GET request to web server
            char request[1024];
            snprintf(request, sizeof(request),
                     "GET %.512s HTTP/1.0\r\nHost: %.256s\r\nConnection: close\r\n\r\n",
                     resource, host);

            send(web_fd, request, strlen(request), 0);
            alarm(10);  // 10-second timeout

            // Receive and forward response + write to cache
            FILE *cache_fp = fopen(filepath, "w");
            int bytes;
            char data[BUF_SIZE];

            while (1) {
                bytes = recv(web_fd, data, sizeof(data), 0);
                if (bytes > 0) {
                    send(sock, data, bytes, 0);
                    if (cache_fp) fwrite(data, 1, bytes, cache_fp);
                } else if (bytes == 0) {
                    break;
                } else {
                    // If error, delete incomplete cache
                    printf("========================================\n");
                    printf("========No Response========\n");
                    printf("========================================\n");
                    if (cache_fp) {
                        fclose(cache_fp);
                        unlink(filepath);
                    }
                    close(web_fd);
                    close(sock);
                    exit(1);
                }
            }

            if (cache_fp) fclose(cache_fp);
            alarm(0);
            close(web_fd);

            // Log MISS
            char log[1024];
            snprintf(log, sizeof(log), "[MISS] ServerPID: %d | http://%.900s -[%s]",
                     getpid(), pure_url, time_str);
            log_to_file(log);
        }

        // Print disconnection message
        printf("[%s : %d] client was disconnected\n",
               inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));

        // Log termination with request result
        time_t end_time = time(NULL);
        char term_log[1024];
        snprintf(term_log, sizeof(term_log),
                 "[Terminated] ServerPID: %d | run time : %ld sec. #request hit : %d, miss : %d",
                 getpid(), end_time - start_time, is_hit, !is_hit);
        log_to_file(term_log);
        break;
    }

    close(sock);
    exit(0);
}

//////////////////////////////////////////////////////////////////
// main                                                         //
// ============================================================ //
// Input: None                                                  //
// Output: int (0 on success)                                   //
// Purpose: Entry point; initializes server, listens on socket, //
//          forks child to handle each client request           //
//////////////////////////////////////////////////////////////////
int main() {
    signal(SIGCHLD, sigchld_handler);  // Prevent zombie processes

    // Ensure required directories exist
    mkdir(get_home_path("cache"), 0755);
    mkdir(get_home_path("logfile"), 0755);

    // Set up server socket
    int serv_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(PORT),
    };

    bind(serv_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    listen(serv_sock, 10);

    printf("Proxy server is running on port %d...\n", PORT);

    while (1) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int client = accept(serv_sock, (struct sockaddr *)&cli_addr, &cli_len);
        if (fork() == 0) {
            close(serv_sock);
            handle_browser(client, cli_addr);
        }
        close(client);
    }

    return 0;
}

