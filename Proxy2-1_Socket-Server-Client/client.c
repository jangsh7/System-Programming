///////////////////////////////////////////////////////////////////////////////////
// File Name  : client.c                                                         //
// Date       : 2025/05/01                                                       //
// Os         : Ubuntu 20.04.6 LTS 64bits                                        //
// Author     : Seo Hyeon JANG                                                   //
// Student ID : 2023202062                                                       //
// ----------------------------------------------------------------------------- //
// Title      : System Programming Assignment #2-1 (proxy - client)              //
// Description:                                                                  //
//   This client connects to the proxy server, takes URL input from the user,    //
//   sends it to the server, and prints the server's response ("HIT" or "MISS"). //
///////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define PORT 8080  // Server port
#define BUF_SIZE 1024  // Buffer size for data transmission

//////////////////////////////////////////////////////////////////////
// main                                                             //
// ================================================================ //
// Input : None                                                     //
// Output: int (0 on successful exit)                               //
// Purpose: Connect to the proxy server, send user-entered URLs,    //
//          and print the result ("HIT" or "MISS") from the server. //
//////////////////////////////////////////////////////////////////////
int main()
{
    int sockfd;
    struct sockaddr_in serv_addr;
    char buffer[BUF_SIZE], response[BUF_SIZE];

    sockfd = socket(AF_INET, SOCK_STREAM, 0);  // Create TCP socket
    if (sockfd < 0) {
        perror("socket error");
        exit(1);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));  // Zero out server address
    serv_addr.sin_family = AF_INET;  // IPv4
    serv_addr.sin_port = htons(PORT);  // Server port
    inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);  // Set server IP

    // Connect to the server
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect error");
        exit(1);
    }

    // Repeat: input URL and receive response
    while (1) {
        printf("input url > ");
        fflush(stdout);  // Flush prompt

        if (!fgets(buffer, BUF_SIZE, stdin)) break;  // Read input
        buffer[strcspn(buffer, "\n")] = '\0';  // Remove newline

        write(sockfd, buffer, strlen(buffer));  // Send URL
        write(sockfd, "\n", 1);  // Send newline as delimiter

        if (strcmp(buffer, "bye") == 0) break;  // Exit condition

        memset(response, 0, BUF_SIZE);  // Clear response buffer
        if (read(sockfd, response, BUF_SIZE - 1) > 0) {
            printf("%s", response);  // Print server response (HIT or MISS)
        }
    }

    close(sockfd);  // Close connection
    return 0;
}

