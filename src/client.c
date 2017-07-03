#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SERVER_IP "127.0.0.1"   // IP of the server
#define SERVER_PORT 3333        // Port of the server
#define MESG_SIZE 2000          // Size of messages
#define OK 0
#define NO_INPUT 1
#define TOO_LONG 2

static int getLine(char *prompt, char *buffer, int size);

/***************************************************************************************************
 * Main function
 **************************************************************************************************/
int main(int argc, char* argv[]) {
    int clientSocket;
    struct sockaddr_in server;
    char mesgOut[MESG_SIZE];
    char mesgIn[MESG_SIZE];
    int rc = 0;
    char prompt[] = "Enter command> ";
    
    // Create socket to connect to local machine server via TCP on port 3333
    clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == -1) {
        printf("Could not create socket.\n");
    }
    bzero(&server, sizeof(server));
    server.sin_addr.s_addr = inet_addr(SERVER_IP);
    server.sin_family = AF_INET;
    server.sin_port = htons(SERVER_PORT);
    // Connect
    if (connect(clientSocket, (struct sockaddr*)&server, sizeof(server)) < 0) {
        printf("Connection error.\n");
        return 1;
    }
    // Receive initial message
    read(clientSocket, mesgIn, MESG_SIZE);
    if (strlen(mesgIn) <= 0) {
        printf("Failed to receieve data.\n");
    }
    printf("%s\n", mesgIn);
    
    while (1) {
        // Get input and validate
        memset(mesgIn, '\0', MESG_SIZE*sizeof(char));
        memset(mesgOut, '\0', MESG_SIZE*sizeof(char));
        rc = getLine(prompt, mesgOut, MESG_SIZE);
        if (rc == NO_INPUT) {
            printf("No input\n");
            continue;
        }
        if (rc == TOO_LONG) {
            printf(prompt, mesgOut);
            continue;
        }
        
        // Write to server
        
        send(clientSocket, mesgOut, strlen(mesgOut), MSG_NOSIGNAL);
        // Read from server
        if (read(clientSocket, mesgIn, MESG_SIZE) <= 1) {
            continue;
        }
        printf("%s", mesgIn);
    }
    close(clientSocket);
    
    return 0;
}

// Function to get a line from stdin and prefer buffer overflow
static int getLine(char prompt[], char buffer[], int size) {
    int ch, extra;
    
    // Display prompt
    if (prompt != NULL) {
        printf("%s", prompt);
        fflush(stdout);
    }
    // Get input
    fgets(buffer, size, stdin);
    // Handle input
    if (strcmp(buffer, "\n") == 0 || buffer == NULL) {
        return NO_INPUT;
    }
    if (buffer[strlen(buffer) - 1] != '\n') {
        extra = 0;
        while (((ch = getchar()) != '\n') && (ch != EOF)) {
            extra = 1;
        }
        return (extra == 1) ? TOO_LONG : OK;
    }
    buffer[strlen(buffer) - 1] = '\0';
    
    return OK;
}
