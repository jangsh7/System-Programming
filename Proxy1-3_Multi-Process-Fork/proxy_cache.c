////////////////////////////////////////////////////////////////////////////////////
// File Name  : proxy_cache.c                                                     //
// Date       : 2025/04/15                                                        //
// OS         : Ubuntu 16.04 LTS 64bits                                           //
// Author     : Seo Hyeon JANG                                                    //
// Student ID : 2023202062                                                        //
// ------------------------------------------------------------------------------ //
// Title      : System Programming Assignment #1-3 (proxy server)                 //
// Description :                                                                  //
//   This program implements a concurrent server using fork.                      //
//   The main process receives user commands and creates sub-processes.           //
//   Each sub-process handles URL input, manages cache files using SHA-1 hashing, //
//   and logs results into a shared logfile.                                      //
//   The server logs the number of sub-processes created and total runtime.       //
////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <string.h>
#include <openssl/sha.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <stdlib.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>
#include <sys/wait.h>

/////////////////////////////////////////////////////////////////
// sha1_hash                                                   //
// =========================================================== //
// Input:  char* input_url -> Original input URL               //
//         char* hashed_url -> Buffer to store hashed result   //
// Output: char* - hashed_url (Hashed URL in hex string)       //
// Purpose: Convert input URL into a SHA-1 hashed string       //
/////////////////////////////////////////////////////////////////
char *sha1_hash(char *input_url, char *hashed_url) {
	unsigned char hashed_160bits[20]; // Buffer for 160-bit SHA-1 result
	char hashed_hex[41]; // Buffer for 40-character hex string
	int i;

	SHA1((unsigned char*)input_url, strlen(input_url), hashed_160bits); // Compute SHA-1 hash
	for (i = 0; i < sizeof(hashed_160bits); i++) // Convert each byte
		sprintf(hashed_hex + i * 2, "%02x", hashed_160bits[i]); // to 2-digit hex string
        
	// Add null terminator - necessary...
	hashed_hex[40] = '\0'; // Add null terminator
	strcpy(hashed_url, hashed_hex); // Copy hex string to output buffer
    
	return hashed_url;
}

////////////////////////////////////////////////////////////
// getHomeDir                                             //
// ====================================================== //
// Input:  char* home -> Buffer to store home directory   //
// Output: char* - home (Home directory path)             //
// Purpose: Get the current user's home directory path    //
////////////////////////////////////////////////////////////
char *getHomeDir(char *home) {
	struct passwd *usr_info = getpwuid(getuid()); // Get current user info
	strcpy(home, usr_info->pw_dir); // Copy home path
    
	return home;
}

////////////////////////////////////////////////////////////////////////////////////
// open_logfile                                                                   //
// ============================================================================== //
// Input : None                                                                   //
// Output: FILE* -> logfile pointer                                               //
// Purpose: Open the logfile in append mode. Create logfile directory if missing. //
////////////////////////////////////////////////////////////////////////////////////
FILE* open_logfile() {
	char home[100], log_dir[200], log_path[300];

	getHomeDir(home); // Get the current user's home directory path

	mode_t old_mask = umask(0); // Temporarily set umask to 0 to ensure full permissions
	snprintf(log_dir, sizeof(log_dir), "%s/logfile", home); // Construct path for logfile directory
	mkdir(log_dir, 0777); // Create logfile directory with 0777 permission (if not exists)
	umask(old_mask); // Restore the original umask

	snprintf(log_path, sizeof(log_path), "%s/logfile.txt", log_dir); // Construct path for logfile.txt
	FILE *log_fp = fopen(log_path, "a"); // Open logfile.txt in append mode

	if (log_fp == NULL) {
		perror("Failed to open logfile.txt"); // Print error if file open fails
		exit(1); // Terminate the program
	}

	return log_fp; // Return the file pointer
}

/////////////////////////////////////////////////////////////
// cache_exists                                            //
// ======================================================= //
// Input : const char* hashed_url - hashed URL             //
//         const char* home - user's home directory path   //
// Output: int - 1 if cache file exists, 0 otherwise       //
// Purpose: Check if the cache file for given URL exists   //
/////////////////////////////////////////////////////////////
int cache_exists(const char *hashed_url, const char *home) {
	char cache_dir[200];
    
	// Construct the cache directory path using the first 3 characters of the hashed URL
	snprintf(cache_dir, sizeof(cache_dir), "%s/cache/%c%c%c", home, hashed_url[0], hashed_url[1], hashed_url[2]);
    
	DIR *dir = opendir(cache_dir); // Try to open the cache directory
	if (dir != NULL) {
		struct dirent *entry;
		while ((entry = readdir(dir)) != NULL) {
			// Check if the file name matches the remaining part of the hashed URL
			if (strcmp(entry->d_name, hashed_url + 3) == 0) {
				closedir(dir); // Close directory before returning
				return 1; // Cache file exists (HIT)
			}
		}
		closedir(dir); // Close directory after search
	}

	return 0; // Cache file does not exist (MISS)
}

////////////////////////////////////////////////////////////////////////////
// create_cache_file                                                      //
// ====================================================================== //
// Input : const char* hashed_url - hashed URL                            //
//         const char* home - user's home directory path                  //
//         const char* input_url - original URL                           //
// Output: None                                                           //
// Purpose: Create a cache file for the URL in the appropriate directory  //
////////////////////////////////////////////////////////////////////////////
void create_cache_file(const char *hashed_url, const char *home, const char *input_url) {
	char cache_dir[200], cache_path[300], temp[300];
    
	snprintf(cache_dir, sizeof(cache_dir), "%s/cache/%c%c%c", home, hashed_url[0], hashed_url[1], hashed_url[2]); // Construct the subdirectory path using the first 3 characters of the hashed URL

	mode_t old_mask = umask(0); // Temporarily set umask to 0 to ensure full permission

	// Create the main cache directory (~/cache) if it doesn't exist
	snprintf(temp, sizeof(temp), "%s/cache", home);
	mkdir(temp, 0777);
    
	mkdir(cache_dir, 0777); // Create the subdirectory

	umask(old_mask); // Restore original umask

	snprintf(cache_path, sizeof(cache_path), "%s/%s", cache_dir, hashed_url + 3); // Construct the full path for the cache file

	// Create an empty cache file at the constructed path
	FILE *fp = fopen(cache_path, "w");
	if (fp == NULL) {
		perror("fail to create cache file"); // Error handling if file creation fails
	} else {
		fclose(fp); // Close file after creation
	}
}

///////////////////////////////////////////////////////////////////////////////////
// handle_url                                                                    //
// ==============================================================================//
// Input : None                                                                  //
// Output: None                                                                  //
// Purpose: Sub-process to handle URL input, manage cache, and log the results   //
///////////////////////////////////////////////////////////////////////////////////
void handle_url() {
	time_t start_time = time(NULL); // Record the start time of this sub-process
	int hit = 0, miss = 0; // Initialize hit/miss counters
	char input_url[256];
	char hashed_url[41];
	char home[100];
	getHomeDir(home); // Get the user's home directory

	FILE *log_fp = open_logfile(); // Open the shared logfile in append mode

	while (1) {
		printf("[%d] input URL> ", getpid()); // Print prompt with process ID
		fflush(stdout);

		if (fgets(input_url, sizeof(input_url), stdin) == NULL)
			break; // Exit on input error or EOF

		input_url[strcspn(input_url, "\n")] = '\0'; // Remove trailing newline

		if (strcmp(input_url, "bye") == 0)
			break; // Exit loop if user types "bye"

		sha1_hash(input_url, hashed_url); // Convert URL to SHA-1 hashed string

		time_t now = time(NULL);
		struct tm *t = localtime(&now); // Get current local time for logging

		if (cache_exists(hashed_url, home)) {
			// HIT: cache file already exists
			fprintf(log_fp, "[Hit]%c%c%c/%s-[%04d/%02d/%02d, %02d:%02d:%02d]\n", 
				hashed_url[0], hashed_url[1], hashed_url[2], hashed_url + 3,
				t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
				t->tm_hour, t->tm_min, t->tm_sec);
			fprintf(log_fp, "[Hit]%s\n", input_url);
			hit++; // Increment hit counter
		} else {
			// MISS: cache file not found, so create it
			create_cache_file(hashed_url, home, input_url);
			fprintf(log_fp, "[Miss]%s-[%04d/%02d/%02d, %02d:%02d:%02d]\n", 
				input_url,
				t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
				t->tm_hour, t->tm_min, t->tm_sec);
			miss++; // Increment miss counter
		}
		fflush(log_fp); // Ensure the log is written immediately
	}

	int runtime = (int)difftime(time(NULL), start_time); // Calculate total runtime
	// Write termination log with hit/miss summary
	fprintf(log_fp, "[Terminated] run time: %d sec. #request hit : %d, miss : %d\n", runtime, hit, miss);
	fclose(log_fp); // Close logfile before exit
	exit(0); // End sub-process
}

/////////////////////////////////////////////////////////////////////////////
// main                                                                    //
// ======================================================================= //
// Input : None                                                            //
// Output: int - 0 on normal exit                                          //
// Purpose: Main process to manage user commands and create sub-processes  //
/////////////////////////////////////////////////////////////////////////////
int main() {
	time_t server_start = time(NULL); // Record the server start time
	int sub_process_count = 0; // Counter for number of sub-processes created
	char command[256]; // Buffer to store user command

	while (1) {
		printf("[%d] input CMD> ", getpid()); // Prompt for command with PID
		fflush(stdout);
        
		if (fgets(command, sizeof(command), stdin) == NULL)
			break; // Exit on EOF or input error

		command[strcspn(command, "\n")] = '\0'; // Remove newline character

		if (strcmp(command, "connect") == 0) {
			pid_t pid = fork(); // Create a sub-process
			if (pid < 0) {
				perror("fork error"); // Handle fork failure
				exit(1);
			} else if (pid == 0) {
				// Child process: handle URL input
				handle_url();
			} else {
				// Parent process: wait for child to finish
				wait(NULL);
				sub_process_count++; // Increment sub-process counter
			}
		} else if (strcmp(command, "quit") == 0) {
			break; // Exit the command loop
		} else {
			printf("Unknown command. Use 'connect' or 'quit'.\n"); // Invalid input
		}
	}

	int runtime = (int)difftime(time(NULL), server_start); // Calculate total runtime of the server

	// Log server termination info
	FILE *log_fp = open_logfile();
	fprintf(log_fp, "**SERVER** [Terminated] run time: %d sec. #sub process: %d\n", runtime, sub_process_count);
	fclose(log_fp);

	return 0; // Exit the program
}

