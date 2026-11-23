///////////////////////////////////////////////////////////////////////////////////
// File Name  : server.c                                                         //
// Date       : 2025/05/01                                                       //
// Os         : Ubuntu 20.04.6 LTS 64bits                                        //
// Author     : Seo Hyeon JANG                                                   //
// Student ID : 2023202062                                                       //
// ----------------------------------------------------------------------------- //
// Title      : System Programming Assignment #2-1 (proxy - server)              //
// Description:                                                                  //
//   This server accepts multiple client connections using fork().               //
//   It receives URL requests from clients, checks cache using SHA-1 hash,       //
//   logs each request as HIT or MISS, and tracks runtime and stats per client.  //
///////////////////////////////////////////////////////////////////////////////////

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
#include <sys/wait.h>
#include <signal.h>

#define PORT 8080  // Server port
#define BUF_SIZE 1024  // Buffer size for communication

//////////////////////////////////////////////////////////
// sigint_handler                                       //
// ==================================================== //
// Input : int signo - signal number                    //
// Output: None                                         //
// Purpose: Handle SIGINT (Ctrl+C) and exit gracefully. //
//////////////////////////////////////////////////////////
void sigint_handler(int signo) {
    exit(0);  // Silent termination (flush ensured by OS)
}

////////////////////////////////////////////////////////////////
// sha1_hash                                                  //
// ========================================================== //
// Input : char* input_url - URL to be hashed                 //
//         char* hashed_url - Output buffer for hex hash      //
// Output: char* - resulting hex string                       //
// Purpose: Generate 40-character SHA-1 hash string from URL. //
////////////////////////////////////////////////////////////////
char *sha1_hash(char *input_url, char *hashed_url) {
    unsigned char hashed[SHA_DIGEST_LENGTH];
    SHA1((unsigned char *)input_url, strlen(input_url), hashed);
    for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
        sprintf(hashed_url + i * 2, "%02x", hashed[i]);  // Convert to hex
    }
    hashed_url[40] = '\0';
    return hashed_url;
}

/////////////////////////////////////////////////
// get_home_dir                                //
// =========================================== //
// Input : char* home - buffer to store result //
// Output: char* - pointer to home path        //
// Purpose: Get current user's home directory. //
/////////////////////////////////////////////////
char *get_home_dir(char *home) {
    struct passwd *pw = getpwuid(getuid());
    strcpy(home, pw->pw_dir);
    return home;
}

////////////////////////////////////////////////////////
// log_to_file                                        //
// ================================================== //
// Input : const char* message - log message          //
// Output: None                                       //
// Purpose: Append a message to ~/logfile/logfile.txt //
////////////////////////////////////////////////////////
void log_to_file(const char *message) {
    char home[100], log_path[300];
    get_home_dir(home);
    snprintf(log_path, sizeof(log_path), "%s/logfile/logfile.txt", home);

    FILE *fp = fopen(log_path, "a");
    if (fp) {
        fprintf(fp, "%s\n", message);  // Write log
        fflush(fp);  // Ensure flush
        fclose(fp);
    }
}

//////////////////////////////////////////////////////////
// handle_client                                        //
// ==================================================== //
// Input : int client_fd - client socket                //
//         struct sockaddr_in cli_addr - client address //
// Output: None (exits process)                         //
// Purpose: Handle individual client request lifecycle. //
//////////////////////////////////////////////////////////
void handle_client(int client_fd, struct sockaddr_in cli_addr) {
    char home[100];
    get_home_dir(home);

    // Connection message
    printf("[127.0.0.1 : %d] client was connected\n", ntohs(cli_addr.sin_port));

    time_t start_time = time(NULL);
    int hit = 0, miss = 0;
    char buffer[BUF_SIZE], hashed[41];

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        if (read(client_fd, buffer, BUF_SIZE - 1) <= 0) break;

        buffer[strcspn(buffer, "\n")] = '\0';  // Remove newline
        if (strlen(buffer) == 0) continue;
        if (strcmp(buffer, "bye") == 0) break;

        sha1_hash(buffer, hashed);

        // Construct cache path
        char cache_dir[300], full_path[400];
        snprintf(cache_dir, sizeof(cache_dir), "%s/cache/%c%c%c", home, hashed[0], hashed[1], hashed[2]);
        snprintf(full_path, sizeof(full_path), "%s/%s", cache_dir, hashed + 3);

        // Get timestamp
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char time_buf[100];
        snprintf(time_buf, sizeof(time_buf), "%04d/%02d/%02d, %02d:%02d:%02d",
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                 t->tm_hour, t->tm_min, t->tm_sec);

        // Ensure cache directory exists
        mkdir(strcat(strcpy(cache_dir, home), "/cache"), 0777);
        snprintf(cache_dir, sizeof(cache_dir), "%s/cache/%c%c%c", home, hashed[0], hashed[1], hashed[2]);
        mkdir(cache_dir, 0777);

        // Search cache file
        int found = 0;
        DIR *dp = opendir(cache_dir);
        if (dp) {
            struct dirent *entry;
            while ((entry = readdir(dp))) {
                if (strcmp(entry->d_name, hashed + 3) == 0) {
                    found = 1;
                    break;
                }
            }
            closedir(dp);
        }

        if (found) {
            // HIT
            char log1[1200], log2[1100];
            snprintf(log1, sizeof(log1), "[HIT] ServerPID: %d | %c%c%c/%s -[%s]",
                     getpid(), hashed[0], hashed[1], hashed[2], hashed + 3, time_buf);
            snprintf(log2, sizeof(log2), "[HIT] %s", buffer);
            log_to_file(log1);
            log_to_file(log2);
            write(client_fd, "HIT\n", 4);
            hit++;
        } else {
            // MISS
            FILE *fp = fopen(full_path, "w");
            if (fp) {
                fflush(fp);
                fsync(fileno(fp));
                fclose(fp);
            }
            char log1[1200];
            snprintf(log1, sizeof(log1), "[MISS] ServerPID: %d | %s -[%s]", getpid(), buffer, time_buf);
            log_to_file(log1);
            write(client_fd, "MISS\n", 5);
            miss++;
        }
    }

    // Log termination
    time_t end_time = time(NULL);
    char term_log[300];
    snprintf(term_log, sizeof(term_log),
             "[Terminated] ServerPID: %d | run time : %ld sec. #request hit : %d, miss : %d", getpid(), end_time - start_time, hit, miss);
    log_to_file(term_log);

    printf("[127.0.0.1 : %d] client was disconnected\n", ntohs(cli_addr.sin_port));

    close(client_fd);
    exit(0);
}

///////////////////////////////////////////////////////////////////
// main                                                          //
// ============================================================= //
// Input : None                                                  //
// Output: int (0 on normal exit)                                //
// Purpose: Initialize server, accept clients, and fork handler. //
///////////////////////////////////////////////////////////////////
int main()
{
    signal(SIGINT, sigint_handler);  // Handle Ctrl+C safely

    int server_fd, client_fd;
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t cli_len = sizeof(cli_addr);

    // Create ~/logfile and ~/cache directories
    char home[100];
    get_home_dir(home);
    mkdir(strcat(strcpy(home, home), "/logfile"), 0777);
    get_home_dir(home);
    mkdir(strcat(strcpy(home, home), "/cache"), 0777);

    // Set up socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    listen(server_fd, 5);  // Max 5 clients in queue

    // Accept and handle clients
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&cli_addr, &cli_len);
        pid_t pid = fork();
        if (pid == 0) {
            close(server_fd);  // Child closes listening socket
            handle_client(client_fd, cli_addr);
        } else {
            close(client_fd);  // Parent closes connected socket
        }
    }

    return 0;
}

