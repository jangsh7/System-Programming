//////////////////////////////////////////////////////////////////////////////////
// File Name : proxy_server.c                                                   //
// Date : 2025/05/08                                                            //
// OS : Ubuntu 20.04.6 LTS 64bits                                               //
// Author : Seo Hyeon JANG                                                      //
// Student ID : 2023202062                                                      //
// ---------------------------------------------------------------------------- //
// Title : System Programming Assignment #2-2 (proxy server, logging version)   //
// Description :                                                                //
//   - Acts as a proxy server that processes HTTP GET requests from a browser.  //
//   - Determines cache HIT or MISS based on SHA-1 hashed URL.                  //
//   - Sends an HTML response and logs all relevant information to logfile.txt. //
//////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pwd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/sha.h>

#define PORT 39999
#define BUF_SIZE 8192
char *student_id = "kw2023202062";

/////////////////////////////////////////////////////////////
// get_home_path                                           //
// ======================================================= //
// Input:  const char *subpath -> directory path to append //
// Output: char* - full path under user's home directory   //
// Purpose: Constructs a full path like ~/subpath          //
/////////////////////////////////////////////////////////////
char *get_home_path(const char *subpath) {
    static char fullpath[2048];
    const char *home = getenv("HOME");
    if (!home) home = getpwuid(getuid())->pw_dir;
    snprintf(fullpath, sizeof(fullpath), "%s/%s", home, subpath);
    return fullpath;
}

////////////////////////////////////////////////////////////
// log_to_file                                            //
// ====================================================== //
// Input:  const char *message -> string to append to log //
// Output: None                                           //
// Purpose: Writes a message to ~/logfile/logfile.txt     //
////////////////////////////////////////////////////////////
void log_to_file(const char *message) {
    char path[2048];
    snprintf(path, sizeof(path), "%s/logfile/logfile.txt", get_home_path(""));
    FILE *fp = fopen(path, "a");
    if (fp) {
        fprintf(fp, "%s\n", message);    // Append message to logfile
        fclose(fp);
    }
}

//////////////////////////////////////////////////////////////////
// get_time_str                                                 //
// ============================================================ //
// Input:  None                                                 //
// Output: char* - formatted time string                        //
// Purpose: Returns current time in YYYY/MM/DD, HH:MM:SS format //
//////////////////////////////////////////////////////////////////
char *get_time_str() {
    static char buf[64];
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    snprintf(buf, sizeof(buf), "%04d/%02d/%02d, %02d:%02d:%02d", lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday, lt->tm_hour, lt->tm_min, lt->tm_sec);
    return buf;
}

//////////////////////////////////////////////////////////////////////
// sha1_hash                                                        //
// ================================================================ //
// Input:  char *input_url -> the URL to hash                       //
//         char *hash_out  -> output SHA1 hex string                //
// Output: None                                                     //
// Purpose: Computes SHA-1 hash of input_url and stores in hash_out //
//////////////////////////////////////////////////////////////////////
void sha1_hash(char *input_url, char *hash_out) {
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((unsigned char *)input_url, strlen(input_url), hash);
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
        sprintf(hash_out + (i * 2), "%02x", hash[i]);    // Convert to hex string
    }
    hash_out[40] = '\0';
}

/////////////////////////////////////////////////////////////////
// send_html_response                                          //
// =========================================================== //
// Input:  int client_sock -> client socket descriptor         //
//         const char *status -> "HIT" or "MISS"               //
//         const char *ip_port -> IP:PORT string               //
//         const char *url -> requested URL                    //
// Output: None                                                //
// Purpose: Sends HTML response to the client with status info //
/////////////////////////////////////////////////////////////////
void send_html_response(int client_sock, const char *status, const char *ip_port, const char *url) {
    char response[1024];
    snprintf(response, sizeof(response),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/html\r\n\r\n"
        "<html><body>"
        "<h1>%s</h1>"
        "%s<br>"
        "http://%s<br>"
        "%s<br>"
        "</body></html>\n", status, ip_port, url, student_id);
    send(client_sock, response, strlen(response), 0);    // Send HTTP response
}

////////////////////////////////////////////////////////////////////
// handle_client                                                  //
// ============================================================== //
// Input:  int client_sock -> client socket                       //
//         struct sockaddr_in cli_addr -> client address          //
// Output: None                                                   //
// Purpose: Process request, determine HIT/MISS, log, and respond //
////////////////////////////////////////////////////////////////////
void handle_client(int client_sock, struct sockaddr_in cli_addr) {
    time_t start_time = time(NULL);    // Record the start time of this client session
    char buf[BUF_SIZE], url[256], hash[41], path[2048];
    int hit = 0, miss = 0;    // Counters for hit/miss

    int len = recv(client_sock, buf, BUF_SIZE - 1, 0);    // Receive HTTP request from client
    if (len <= 0) exit(1);    // If receive failed, exit
    buf[len] = '\0';    // Null-terminate the received buffer

    if (sscanf(buf, "GET http://%255s", url) != 1) {
        strcpy(url, "");    // If not a GET request, set URL to empty
    }

    int ignore = strstr(url, "favicon.ico") != NULL;    // Ignore favicon.ico automatic requests

    if (!ignore) {
        printf("[%s : %d] client was connected\n", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
        printf("================================================\n");
        printf("Request from [%s : %d]\n",
               inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
        char *line = strtok(buf, "\r\n");
        while (line) {
            if (strncmp(line, "Priority:", 9) != 0)
                printf("%s\n", line);    // Print header except Priority field
            line = strtok(NULL, "\r\n");
        }
        printf("================================================\n");
    }

    if (ignore) {
        close(client_sock);    // Close connection for ignored requests
        exit(0);
    }

    sha1_hash(url, hash);    // Get SHA1 hash from URL

    char *cachedir = get_home_path("cache");    // Get base path for cache directory
    char subdir[2048];
    snprintf(subdir, sizeof(subdir), "%s/%c%c%c", cachedir, hash[0], hash[1], hash[2]);    // Use first 3 hash characters as folder name
    mkdir(cachedir, 0755);    // Create cache directory if not exists
    mkdir(subdir, 0755);    // Create subdirectory

    strncpy(path, subdir, sizeof(path) - 1);    // Copy base path
    path[sizeof(path) - 1] = '\0';
    strncat(path, "/", sizeof(path) - strlen(path) - 1);    // Add trailing slash
    strncat(path, hash + 3, sizeof(path) - strlen(path) - 1);    // Append remaining hash string to path

    int is_hit = 0;    // Flag for cache hit detection
    DIR *dp = opendir(subdir);    // Open cache subdirectory
    if (dp) {
        struct dirent *entry;
        while ((entry = readdir(dp))) {
            if (strcmp(entry->d_name, hash + 3) == 0) {
                is_hit = 1;    // Cache hit if file exists
                break;
            }
        }
        closedir(dp);
    }

    char ip_port[64];
    snprintf(ip_port, sizeof(ip_port), "%s:%d", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));    // Format client IP and port
    char *time_str = get_time_str();    // Get current timestamp string

    if (is_hit) {
        send_html_response(client_sock, "HIT", ip_port, url);    // Send HIT response
        char log1[1024], log2[1024];
        snprintf(log1, sizeof(log1), "[HIT] ServerPID: %d | %c%c%c/%s -[%s]", getpid(), hash[0], hash[1], hash[2], hash + 3, time_str);    // Create HIT log entry
        snprintf(log2, sizeof(log2), "[HIT]http://%s", url);
        log_to_file(log1);    // Write HIT log entry 1
        log_to_file(log2);    // Write HIT log entry 2
        hit++;
    } else {
        FILE *fp = fopen(path, "w");    // Create new cache file
        if (fp) {
            fputs("cached content", fp);    // Write placeholder content
            fclose(fp);
        }
        send_html_response(client_sock, "MISS", ip_port, url);    // Send MISS response
        char log[1024];
        snprintf(log, sizeof(log), "[MISS] ServerPID: %d | http://%s -[%s]", getpid(), url, time_str);    // Create MISS log entry
        log_to_file(log);
        miss++;
    }

    printf("[%s : %d] client was disconnected\n", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));    // Print disconnect info

    time_t end_time = time(NULL);    // Record session end time
    char log[1024];
    snprintf(log, sizeof(log), "[Terminated] ServerPID: %d | run time : %ld sec. #request hit : %d, miss : %d", getpid(), end_time - start_time, hit, miss);    // Format session summary
    log_to_file(log);    // Write session summary to logfile

    close(client_sock);    // Close client connection
    exit(0);    // End child process
}

///////////////////////////////////////////////////////
// main                                              //
// ================================================= //
// Input:  None                                      //
// Output: int (always 0)                            //
// Purpose: Start server socket, fork per connection //
///////////////////////////////////////////////////////
int main() {
    int server_sock, client_sock;
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t cli_len = sizeof(cli_addr);

    mkdir(get_home_path("logfile"), 0755);    // Ensure logfile dir exists
    mkdir(get_home_path("cache"), 0755);      // Ensure cache dir exists

    server_sock = socket(AF_INET, SOCK_STREAM, 0);    // Create TCP socket
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT);

    bind(server_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));    // Bind to port
    listen(server_sock, 5);    // Start listening for connections

    while (1) {
        client_sock = accept(server_sock, (struct sockaddr *)&cli_addr, &cli_len);    // Accept new client
        pid_t pid = fork();
        if (pid == 0) {
            close(server_sock);    // Child closes server socket
            handle_client(client_sock, cli_addr);    // Handle request
        } else {
            close(client_sock);    // Parent closes client socket
        }
    }

    return 0;
}

