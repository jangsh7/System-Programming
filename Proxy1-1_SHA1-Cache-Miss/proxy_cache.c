///////////////////////////////////////////////////////////////////////////////////
// File Name  : proxy_cache.c                                   	  	    //
// Date       : 2025/04/03                                        		    //
// Os         : Ubuntu 20.04.6 LTS 64bits                                        //
// Author     : Seo Hyeon JANG						    //
// Student ID : 2023202062                                        	   	    //
// ----------------------------------------------------------------------------- //
// Title : System Programming Assignment #1-1 (proxy server)         	    //
// Description : Read URLs from input and create cache directory and file        //
//               using SHA-1 hash. Files are saved in ~/cache/[dir]/[file]       //
//               until "bye" command is entered.                                 //
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

/////////////////////////////////////////////////////////////////
// sha1_hash                                                   //
// =========================================================== //
// Input:  char* input_url -> 입력된 원본 URL                   //
//         char* hashed_url -> 해시 결과를 저장할 배열           //
// Output: char* - hashed_url (해시 결과 문자열)                //
// Purpose: SHA-1 해시 함수를 이용하여 URL을 해시 문자열로 변환   //
////////////////////////////////////////////////////////////////
char *sha1_hash(char *input_url, char *hashed_url) {
	unsigned char hashed_160bits[20]; // Buffer for 160-bit SHA-1 result
	char hashed_hex[41]; // Buffer for 40-character hex string
	int i;
	
	SHA1((unsigned char*)input_url, strlen(input_url), hashed_160bits); // Compute SHA-1 hash
	
	for(i = 0; i < sizeof(hashed_160bits); i++) // Convert each byte
		sprintf(hashed_hex + i * 2, "%02x", hashed_160bits[i]); // to 2-digit hex string
	
	// Add null terminator - necessary...
	hashed_hex[40] = '\0';	// Add null terminator
	strcpy(hashed_url, hashed_hex); // Copy hex string to output buffer

	return hashed_url;
}

////////////////////////////////////////////////////////////
// getHomeDir                                             //
// ====================================================== //
// Input:  char* home -> 홈 디렉토리 경로를 저장할 배열      //
// Output: char* - home (사용자의 홈 디렉토리 경로 문자열)   //
// Purpose: 현재 로그인된 사용자의 홈 디렉토리 경로를 구함    //
///////////////////////////////////////////////////////////
char *getHomeDir(char *home) {
	struct passwd *usr_info = getpwuid(getuid()); // Get current user info
	strcpy(home, usr_info->pw_dir); // Copy home path
	
	return home;
}

///////////////////////////////////////////////////////////////////////
// main                                                              //
// ================================================================= //
// Input:  없음                                                      //
// Output: int - 0 (정상 종료 시)                                     //
// Purpose: 사용자로부터 URL을 입력 받아 cache 디렉토리와 파일 생성      //
//          "bye" 입력 시 종료됨                                      //
///////////////////////////////////////////////////////////////////////
int main() {
	char input_url[256]; // Buffer for URL input
	char hashed_url[41]; // Buffer for hashed URL (hex string)
	char home[100]; // Buffer for home directory path

	while (1) {
		printf("input url> ");
		fflush(stdout); // Flush output buffer

		if (fgets(input_url, sizeof(input_url), stdin) == NULL) // Read input
			break;

		input_url[strcspn(input_url, "\n")] = 0; // Remove newline character

		if (strcmp(input_url, "bye") == 0) // Exit if "bye"
			break;

		sha1_hash(input_url, hashed_url); // Hash the input URL

		getHomeDir(home); // Get home directory path

		// Construct cache directory and file paths
		char cache_dir[150], cache_path[200];
		snprintf(cache_dir, sizeof(cache_dir), "%s/cache/%c%c%c", home, hashed_url[0], hashed_url[1], hashed_url[2]); // Directory: first 3 chars of hash
 		snprintf(cache_path, sizeof(cache_path), "%s/%s", cache_dir, hashed_url + 3);  // File: remaining chars

		// Create directories: ~/cache and ~/cache/xxx
		mode_t old_mask = umask(0); // Set umask to 0 (full permissions)
		mkdir(strcat(strcpy(cache_dir, home), "/cache"), 0777);  // Create ~/cache if not exists
		snprintf(cache_dir, sizeof(cache_dir), "%s/cache/%c%c%c", home, hashed_url[0], hashed_url[1], hashed_url[2]); // Reset path to ~/cache/xxx
		mkdir(cache_dir, 0777); // Create ~/cache/xxx
		umask(old_mask); // Restore original umask

		// If cache file does not exist, create it
		FILE *fp = fopen(cache_path, "r"); // Try opening file
		if (fp) {
			fclose(fp); // File exists → do nothing (HIT case for next assignment..)
		} else {
			fp = fopen(cache_path, "w"); // File doesn't exist → create new file
			if (fp) {
				fclose(fp);
			} else {
				perror("fail to create file"); // Print error if file creation fails
			}
		} // end of else
	} // end of while

	return 0;
}

