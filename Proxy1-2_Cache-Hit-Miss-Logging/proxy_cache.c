///////////////////////////////////////////////////////////////////////////////////
// File Name  : proxy_cache.c                                                    //
// Date       : 2025/04/06                                                       //
// Os         : Ubuntu 16.04 LTS 64bits                                          //
// Author     : Seo Hyeon JANG                                                   //
// Student ID : 2023202062                                                       //
// ----------------------------------------------------------------------------- //
// Title : System Programming Assignment #1-2 (proxy server)                     //
// Description :                                                                 //
//   Read URLs from input and create cache directory and file                    //
//   using SHA-1 hash. Log each access with time into logfile.txt.               //
//   Display total runtime, hit, and miss counts on termination.                 //
///////////////////////////////////////////////////////////////////////////////////

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

/////////////////////////////////////////////////////////////////
// sha1_hash                                                   //
// =========================================================== //
// Input:  char* input_url -> Original input URL               //
//         char* hashed_url -> Buffer to store hashed result   //
// Output: char* - hashed_url (Hashed URL in hex string)       //
// Purpose: Convert input URL into a SHA-1 hashed string       //
////////////////////////////////////////////////////////////////
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

///////////////////////////////////////////////////////////////////////
// main                                                              //
// ================================================================= //
// Input:  None                                                      //
// Output: int - 0 (On normal exit)                                  //
// Purpose: Read URL input, hash it, create or check cache,          //
//          and log HIT/MISS with timestamps.                        //
///////////////////////////////////////////////////////////////////////
int main() {
	char input_url[256]; // Buffer for URL input
	char hashed_url[41]; // Buffer for hashed URL (hex string)
	char home[100]; // Buffer for home directory path
	char cache_dir[200], cache_path[300]; // Buffers for cache directory and file path
	char log_dir[200], log_path[300]; // Buffers for log directory and file path
	FILE *log_fp; // File pointer for log file
	int hit = 0, miss = 0; // Counters for HIT and MISS
	time_t start_time, end_time; // Timestamps
	start_time = time(NULL); // Record program start time

	getHomeDir(home); // Get home directory path

	// Create ~/logfile directory if it does not exist
	mode_t old_mask = umask(0); // Set umask to 0 (full permissions)
	snprintf(log_dir, sizeof(log_dir), "%s/logfile", home);
	mkdir(log_dir, 0777); // Create logfile directory
	umask(old_mask); // Restore original umask

	// Open ~/logfile/logfile.txt in append mode
	snprintf(log_path, sizeof(log_path), "%s/logfile.txt", log_dir);
	log_fp = fopen(log_path, "a");
	if (log_fp == NULL) {
		perror("Failed to open logfile.txt");
		return 1; // Exit if failed to open
	}

	while (1) {
		printf("input url> ");
		fflush(stdout); // Flush output buffer

		if (fgets(input_url, sizeof(input_url), stdin) == NULL) // Read input
			break;

		input_url[strcspn(input_url, "\n")] = 0; // Remove newline character

		if (strcmp(input_url, "bye") == 0) // Exit if "bye"
			break;

		sha1_hash(input_url, hashed_url); // Hash the input URL

		// Construct cache directory and file paths
		snprintf(cache_dir, sizeof(cache_dir), "%s/cache/%c%c%c", home, hashed_url[0], hashed_url[1], hashed_url[2]); // Directory: first 3 chars of hash
		snprintf(cache_path, sizeof(cache_path), "%s/%s", cache_dir, hashed_url + 3); // File: remaining chars

		// Create ~/cache and ~/cache/xxx directories if they do not exist
		umask(0);
		mkdir(strcat(strcpy(cache_dir, home), "/cache"), 0777); // Create ~/cache
		snprintf(cache_dir, sizeof(cache_dir), "%s/cache/%c%c%c", home, hashed_url[0], hashed_url[1], hashed_url[2]);
		mkdir(cache_dir, 0777); // Create ~/cache/xxx
		umask(old_mask);

		// Open the cache directory to check for existing files
		DIR *dir = opendir(cache_dir);
		int found = 0; // Flag for HIT detection

		if (dir != NULL) {
			struct dirent *entry;
			while ((entry = readdir(dir)) != NULL) {
				if (strcmp(entry->d_name, hashed_url + 3) == 0) { // Compare filenames
					found = 1; // File exists → HIT
					break;
				}
			}
			closedir(dir);
		}

		time_t now = time(NULL); // Current time
		struct tm *t = localtime(&now); // Convert to local time

		if (found) { // HIT case
			fprintf(log_fp, "[Hit]%c%c%c/%s-[%04d/%02d/%02d, %02d:%02d:%02d]\n", hashed_url[0], hashed_url[1], hashed_url[2], hashed_url + 3, t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
			fprintf(log_fp, "[Hit]%s\n", input_url);
			hit++; // Increment HIT counter
		} else { // MISS case
			FILE *fp = fopen(cache_path, "w"); // Create new cache file
			if (fp) {
				fclose(fp);
			} else {
				perror("fail to create file"); // Error if file creation fails
			}
			fprintf(log_fp, "[Miss]%s-[%04d/%02d/%02d, %02d:%02d:%02d]\n", input_url, t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
			miss++; // Increment MISS counter
		}
		fflush(log_fp); // Flush the log buffer
	} // end of while

	end_time = time(NULL); // Record end time

	// Write program termination status to log
	fprintf(log_fp, "[Terminated] run time: %ld sec. #request hit : %d, miss : %d\n", end_time - start_time, hit, miss);
	fclose(log_fp); // Close logfile

	return 0;
}

