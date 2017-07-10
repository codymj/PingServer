#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SERVER_IP "127.0.0.1"   // IP of the server
#define SERVER_PORT 3333        // Port of the server
#define MESG_SIZE 9000          // Size of messages
#define OK 0
#define NO_INPUT 1
#define TOO_LONG 2
#define EXIT 3

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
    int bytesRead = 0;
    
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
        // Clear memory buffers
        memset(mesgIn, '\0', MESG_SIZE*sizeof(char));
        memset(mesgOut, '\0', MESG_SIZE*sizeof(char));
        
        // Get input and validate
        rc = getLine(prompt, mesgOut, MESG_SIZE);
        if (rc == NO_INPUT) {
            printf("No input\n");
            continue;
        }
        else if (rc == TOO_LONG) {
            printf(prompt, mesgOut);
            continue;
        }
        else if (rc == EXIT) {
            break;
        }
        else {
            // Write to server
            send(clientSocket, mesgOut, strlen(mesgOut), MSG_NOSIGNAL);
        }
        
        // Read from server
        if ((bytesRead = read(clientSocket, mesgIn, MESG_SIZE)) == 0) {
            continue;
        }
        printf("%s", mesgIn);
    }
    close(clientSocket);
    puts("\nDisconnected.\n");
    
    return 0;
}

/* Function to get a line from stdin and prevent buffer overflow
 **************************************************************************************************/
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
    if (strcmp(buffer, "exit\n") == 0) {
        return EXIT;
    }
    if (strcmp(buffer, "\n") == 0 || buffer == NULL) {
        return NO_INPUT;
    }
    // Ensure newline
    if (buffer[strlen(buffer) - 1] != '\n') {
        extra = 0;
        while (((ch = getchar()) != '\n') && (ch != EOF)) {
            extra = 1;
        }
        return (extra == 1) ? TOO_LONG : OK;
    }
    // Ensure null-termination
    buffer[strlen(buffer) - 1] = '\0';
    
    return OK;
}
