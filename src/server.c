#include <arpa/inet.h>
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_WEBSITES 10             // Maximum number of Websites to ping per handle
#define NUM_WORKER_THREADS 5        // Number of worker threads
#define NUM_PINGS_PER_SITE 10       // Number of times to ping site
#define SOCKET_LISTEN_PORT 3333     // Port for listening socket
#define MESG_SIZE 9000              // Size of messages


/***************************************************************************************************
 * Global variables
 **************************************************************************************************/
// Global mutex and condition signals
pthread_mutex_t queueMutex;                                 // Mutex for altering HandleNodes
pthread_mutexattr_t init;                                   // Needed for recursive mutex
pthread_cond_t gotRequest = PTHREAD_COND_INITIALIZER;       // Signal threads of pending request
pthread_t threadPool[NUM_WORKER_THREADS];                   // Thread pool of workers

// Global variables for identifying clients
static int clientID = 0;
static int numOfConnectedClients = 0;
struct SocketData {
    int clientID;
    int *socketDesc;
};

// Linked-list (queue) of handles
static int handleQueueSize = 0;
struct HandleNode {
    unsigned int handle;
    unsigned int pendingWebsiteNodes;
    struct WebsiteNode *websiteHead;
    struct WebsiteNode *firstWebsiteNodeInHandle;
    struct WebsiteNode *lastWebsiteNodeInHandle;
    struct HandleNode *nextHandleNodeInQueue;
};
struct HandleNode *queueHead = NULL;                // Pointer to head of handle queue
struct HandleNode *firstHandleNodeInQueue = NULL;   // Pointer to first handle in the queue
struct HandleNode *lastHandleNodeInQueue = NULL;    // Pointer to last handle in the queue

// Linked-list of WebsiteNodes for keeping track of each HandleNode's data
struct WebsiteNode {
    unsigned int handle;
    char url[50];
    short avgPing;
    short minPing;
    short maxPing;
    char status[10];
    struct WebsiteNode *nextWebsiteNodeInHandle;
    struct HandleNode *handleNodeParent;
};

static int handleID = 0;                // Handle ID for clients
static int pendingHandleNodes = 0;      // HandleNodes pending for processing
static int pendingWebsiteNodes = 0;     // WebsiteNodes pending for processing

/***************************************************************************************************
 * Prototypes
 **************************************************************************************************/
void addHandleNodeToQueue(struct HandleNode *hNode);
struct HandleNode* getHandleNodeFromQueue(void);
struct WebsiteNode* getWebsiteNodeFromHandleNode(struct HandleNode *hNode);
int pingWebsite(struct WebsiteNode *website);
void* processRequest(void *arg);
void printReturnCode(int rc);
void* connectionHandler(void *sockData);
int parseWebsiteList(char list[]);
void handleCommand(char cmd[], char arg[], struct SocketData *sockData);
void getHandleStatus(int handle, char mesgOut[]);

/***************************************************************************************************
 * Main
 **************************************************************************************************/
// Main function
int main(int argc, char* argv[]) {
    // Initialize mutex
    pthread_mutexattr_init(&init);
    pthread_mutexattr_settype(&init, PTHREAD_MUTEX_RECURSIVE_NP);
    pthread_mutex_init(&queueMutex, &init);
    
    // Initialize thread pool
    int i;
    int tid[NUM_WORKER_THREADS];
    for (i=0; i<NUM_WORKER_THREADS; i++) {
        tid[i] = i;
        pthread_create(&threadPool[i], NULL, processRequest, (void*)tid);
    }
    
    // Create socket for connecting clients
    int socketDesc;
    int newSocket;
    int sockArg;
    int *newSock;
    struct sockaddr_in server;
    struct sockaddr_in client;
    
    // Initialize socket for listening
    socketDesc = socket(AF_INET, SOCK_STREAM, 0);
    if (socketDesc == -1) {
        puts("Could not create socket.");
        return 1;
    }
    bzero(&server, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(SOCKET_LISTEN_PORT);
    
    // Bind
    if (bind(socketDesc, (struct sockaddr*)&server, sizeof(server)) < 0) {
        puts("Bind failed.");
        return 1;
    }
    
    // Listen
    listen(socketDesc, 2);
    
    // Accept connections
    puts("Awaiting connections...");
    sockArg = sizeof(struct sockaddr_in);
    while (1) {
        newSocket = accept(socketDesc, (struct sockaddr*)&client, (socklen_t*)&sockArg);
        clientID++;
        numOfConnectedClients++;
        printf("Client %d connected.\n", clientID);
        
        // Create thread for client and pass clientID and socket to thread
        pthread_t clientThread;
        newSock = malloc(1);
        *newSock = newSocket;
        struct SocketData *sockData;
        sockData = malloc(sizeof(struct SocketData));
        sockData->clientID = clientID;
        sockData->socketDesc = newSock;
        if (pthread_create(&clientThread, NULL, connectionHandler, (void*)sockData) < 0) {
            perror("Could not create a thread.\n");
            return 1;
        }
        //pthread_join(clientThread, NULL);
        printf("Connected clients: %d\n", numOfConnectedClients);
    }
    
    return 0;
}

/***************************************************************************************************
 * Definitions
 **************************************************************************************************/
// Handler routine for each client
void* connectionHandler(void *sockData) {
    struct SocketData *socketData = (struct SocketData*)sockData;
    int socket = *(int*)socketData->socketDesc;
    char mesgIn[MESG_SIZE];
    char mesgOut[MESG_SIZE];
    char command[MESG_SIZE];
    char arg[MESG_SIZE];
    int i;
    
    // Initial message
    strcpy(mesgOut, "\nYou are connected.\nType 'help' to see available commands.\n");
    send(socket, mesgOut, strlen(mesgOut), MSG_NOSIGNAL);
    
    // Get input from client
    while ((read(socket, mesgIn, MESG_SIZE)) > 0) {
        // Parse command from mesgIn
        for (i=0; i<strlen(mesgIn); i++) {
            if (*(mesgIn + i) == '\0') {
                break;
            }
            if (isspace(*(mesgIn + i))) {
                *(command + i) = '\0';
                break;
            }
            *(command + i) = *(mesgIn + i);
        }
        // Skip whitespace
        i++;
        // Copy rest of mesgIn into arg
        memcpy(arg, &mesgIn[i], strlen(mesgIn));
        // Process information
        handleCommand(command, arg, socketData);
        
        // Clear memory buffers
        memset(mesgIn, '\0', MESG_SIZE*sizeof(char));
        memset(mesgOut, '\0', MESG_SIZE*sizeof(char));
        memset(command, '\0', MESG_SIZE*sizeof(char));
        memset(arg, '\0', MESG_SIZE*sizeof(char));
    }
    
    // Client disconnects
    printf("Client %d disconnected.\n", socketData->clientID);
    free(socketData);
    numOfConnectedClients--;
    
    return 0;
}

// Takes command, validates and processes it
void handleCommand(char cmd[], char arg[], struct SocketData *sockData) {
    struct SocketData *sData = (struct SocketData*)sockData;
    int handle = 0;
    int socket = *(int*)sData->socketDesc;
    char mesgOut[MESG_SIZE];
    char temp[MESG_SIZE];
    char temp1[MESG_SIZE];
    char temp2[MESG_SIZE];
    
    if (strcmp(cmd, "help") == 0) {
        strcpy(mesgOut, ("\nAvailable commands:\n \
        * help - Display this dialog.\n \
        * pingSites <comma separated URL list>\n \
        \t- Example: pingSites <www.google.com,www.espn.com>\n \
        \t- Up to 10 URLs are supported.\n \
        * showHandles - Displays the current pending requests from all clients.\n \
        * showHandleStatus <integer> - (Ex. showHandleStatus 3)\n \
        \t- Lists the websites requested by each client and \n \
        \t  their current status.\n\n"));
        send(socket, mesgOut, strlen(mesgOut), MSG_NOSIGNAL);
    }
    else if (strcmp(cmd, "pingSites") == 0) {
        handle = parseWebsiteList(arg);
        strcpy(temp1, "Your handle for this request is: ");
        strcpy(temp2, "To view status of this request, type\n\t showHandleStatus ");
        snprintf(mesgOut, MESG_SIZE, "\n%s%d\n%s%d\n\n", temp1, handle, temp2, handle);
        send(socket, mesgOut, strlen(mesgOut), MSG_NOSIGNAL);
    }
    else if (strcmp(cmd, "showHandles") == 0) {
        strcpy(temp1, "Total handles on server: ");
        snprintf(mesgOut, MESG_SIZE, "\n%s%d\n\n", temp1, handleQueueSize);
        send(socket,mesgOut, strlen(mesgOut), MSG_NOSIGNAL);
    }
    else if (strcmp(cmd, "showHandleStatus") == 0) {
        // If arg is blank, return every handle's status
        if (strlen(arg) == 0) {
            if (handleQueueSize == 0) {
                strcpy(mesgOut, "Nothing to show.\n");
                send(socket, mesgOut, strlen(mesgOut), MSG_NOSIGNAL);
                return;
            }
            int i = 1;
            while (i <= handleQueueSize) {
                getHandleStatus(i, temp);
                strcat(mesgOut, temp);
                memset(temp, '\0', MESG_SIZE*sizeof(char));
                i++;
            }
            send(socket, mesgOut, strlen(mesgOut), MSG_NOSIGNAL);
        }
        // Validate arg is a digit
        else {
            int i;
            for (i=0; i<strlen(arg); i++) {
                if (!isdigit(arg[i])) {
                    strcpy(mesgOut, "Argument is not an integer.\n");
                    send(socket, mesgOut, strlen(mesgOut)+1, MSG_NOSIGNAL);
                    return;
                }
            }
            handle = atoi(arg);
            getHandleStatus(handle, mesgOut);
            send(socket, mesgOut, strlen(mesgOut), MSG_NOSIGNAL);
        }
    }
    else {
        strcpy(mesgOut, "\nError: Unrecognized command.\nType 'help'\n\n");
        send(socket, mesgOut, strlen(mesgOut), MSG_NOSIGNAL);
    }
    
    // Clear memory buffers
    memset(mesgOut, '\0', MESG_SIZE*sizeof(char));
    memset(temp, '\0', MESG_SIZE*sizeof(char));
    memset(temp1, '\0', MESG_SIZE*sizeof(char));
    memset(temp2, '\0', MESG_SIZE*sizeof(char));
    
    return;
}

// Parses a list of websites entered by client and adds them to queue
int parseWebsiteList(char list[]) {
    // Parse websites from list into separate URL strings
    char *parsedURLs[MAX_WEBSITES];
    const char *delim = ", \n \0";
    int i = 0;
    char *ptr;
    ptr = strtok(list, delim);
    while ((ptr) && (i<strlen(list))) {
        parsedURLs[i] = ptr;
        i++;
        ptr = strtok(NULL, delim);
    }
    // Create and initialize new HandleNode
    struct HandleNode *hNode = malloc(sizeof(struct HandleNode));
    handleID++;
    hNode->handle = handleID;
    hNode->pendingWebsiteNodes = 0;
    
    // Create and initialize WebsiteNode
    i = 0;
    while (parsedURLs[i] && i<10) {
        // Create new WebsiteNodes for hNode
        struct WebsiteNode *wNode = malloc(sizeof(struct WebsiteNode));
        if (!wNode) {
            fprintf(stderr, "addhNodeToQueue: Out of memory!\n");
            exit(1);
        }
        // Initialize wNode and add to hNode
        wNode->handle = hNode->handle;
        strcpy(wNode->url, parsedURLs[i]);
        wNode->avgPing = -1;
        wNode->minPing = -1;
        wNode->maxPing = -1;
        strcpy(wNode->status, "IN_QUEUE");
        wNode->handleNodeParent = hNode;
        // If this is first WebsiteNode
        if (hNode->firstWebsiteNodeInHandle == NULL) {
            hNode->websiteHead = wNode;
            hNode->firstWebsiteNodeInHandle = wNode;
            hNode->lastWebsiteNodeInHandle = wNode;
            hNode->lastWebsiteNodeInHandle->nextWebsiteNodeInHandle = NULL;
        }
        // Otherwise append as usual
        else {
            hNode->lastWebsiteNodeInHandle->nextWebsiteNodeInHandle = wNode;
            hNode->lastWebsiteNodeInHandle = wNode;
            hNode->lastWebsiteNodeInHandle->nextWebsiteNodeInHandle = NULL;
        }
        hNode->pendingWebsiteNodes++;
        i++;
    }
    
    // Update global variable
    pendingWebsiteNodes += hNode->pendingWebsiteNodes;
    
    // Add hNode to queue
    addHandleNodeToQueue(hNode);
    
    return handleID;
}

// A function for adding a requested site to the queue
void addHandleNodeToQueue(struct HandleNode *hNode) {
    int returnCode;
    
    // Lock mutex to ensure exclusive access to queue before adding HandleNode
    returnCode = pthread_mutex_lock(&queueMutex);
    if (returnCode) {
        printReturnCode(returnCode);
    }
    
    // If queue is empty, append first HandleNode
    if (firstHandleNodeInQueue == NULL) {
        // Set queue head
        if (handleQueueSize == 0) {
            queueHead = hNode;
        }
        // Move first and last HandleNode in queue to next available request
        firstHandleNodeInQueue = lastHandleNodeInQueue = hNode;
    }
    // Otherwise, append as usual
    else {
        lastHandleNodeInQueue->nextHandleNodeInQueue = hNode;
        lastHandleNodeInQueue = hNode;
        lastHandleNodeInQueue->nextHandleNodeInQueue = NULL;
    }
    
    // Update global variables & signal
    handleQueueSize++;
    pendingHandleNodes++;
    pthread_cond_broadcast(&gotRequest);
    
    // Unlock mutex
    returnCode = pthread_mutex_unlock(&queueMutex);
    if (returnCode) {
        printReturnCode(returnCode);
    }
    
    return;
}

// A function for getting first HandleNode in queue
struct HandleNode* getHandleNodeFromQueue(void) {
    struct HandleNode *hNode = NULL;
    
    // If no more HandleNodes left to process
    if (firstHandleNodeInQueue == NULL) {
        return hNode;
    }
    
    // Get first HandleNode in queue
    hNode = firstHandleNodeInQueue;
    // If not all websites from HandleNode have been processed, return it
    if (hNode->pendingWebsiteNodes) {
        return hNode;
    }
    else {
        // Otherwise, get next HandleNode if it is available
        if (hNode != lastHandleNodeInQueue) {
            firstHandleNodeInQueue = hNode->nextHandleNodeInQueue;
            hNode = firstHandleNodeInQueue;
            pendingHandleNodes--;
            return hNode;
        }
        // Last HandleNode reached
        else {
            firstHandleNodeInQueue = NULL;
            pendingHandleNodes--;
            return hNode;
        }
    }
}

// A function for getting a Website from queue
struct WebsiteNode* getWebsiteNodeFromHandleNode(struct HandleNode *hNode) {
    struct WebsiteNode *wNode = NULL;
    
    // Get available WebsiteNode
    if (hNode->firstWebsiteNodeInHandle) {
        wNode = hNode->firstWebsiteNodeInHandle;
        // If there is a next WebsiteNode in HandleNode, reset firstWebsiteNodeInHandle
        if (wNode != hNode->lastWebsiteNodeInHandle) {
            hNode->firstWebsiteNodeInHandle = wNode->nextWebsiteNodeInHandle;
            hNode->pendingWebsiteNodes--;
            return wNode;
        }
        // Last WebsiteNode in HandleNode reached
        else {
            hNode->pendingWebsiteNodes--;
            return wNode;
        }
    }
    else {
        return wNode;
    }
}

// A loop for continuously handling requests from client
void* processRequest(void *t) {
    struct HandleNode *hNode = NULL;
    struct WebsiteNode *wNode = NULL;
    
    while (1) {
        // Block if no pending nodes to process
        pthread_mutex_lock(&queueMutex);
        while (!pendingWebsiteNodes) {
            pthread_cond_wait(&gotRequest, &queueMutex);
        }
        // Website is available for processing, retrieve the first WebsiteNode
        if ((hNode = getHandleNodeFromQueue())) {
            if ((wNode = getWebsiteNodeFromHandleNode(hNode))) {
                //printf("Thread %ld grabbed %s.\n", pthread_self(), wNode->url);
                pendingWebsiteNodes--;
            }
        }
        pthread_mutex_unlock(&queueMutex);
        // Prevent pinging NULL wNodes
        if (strlen(wNode->url) > 1) {
            pingWebsite(wNode);
        }
        else {
            pthread_join(pthread_self(), NULL);
        }
    }
    pthread_exit(NULL);
}

// Prints return code in case of an error
void printReturnCode(int rc) {
    printf("ERROR: Return code from pthread_ is %d\n", rc);
    exit(-1);
}

// Returns status of handle requested
void getHandleStatus(int handle, char mesgOut[]) {
    struct HandleNode *hItr = queueHead;
    struct WebsiteNode *wItr;
    char temp[MESG_SIZE];
    
    // Validate handle is within range
    if ((handle > handleQueueSize) || (handle < 1)) {
        strcpy(mesgOut, "This handle doesn't exist.\n");
        return;
    }

    // Go through HandleNode queue to find handle
    while (handle != hItr->handle) {
        hItr = hItr->nextHandleNodeInQueue;
    }
    wItr = hItr->websiteHead;
    // Write header for table
    snprintf(
        temp,
        MESG_SIZE/MAX_WEBSITES,
        "\n%s\t%s\t\t\t%s\t%s\t%s\t%s\n%s\n",
        "Handle", "URL", "Avg", "Min", "Max", "Status",
        "==================================================================="
    );
    strcpy(mesgOut, temp);
    memset(temp, '\0', MESG_SIZE*sizeof(char));
    while (wItr) {
        // Store data for table
        snprintf(
            temp,
            MESG_SIZE/MAX_WEBSITES,
            "  %d\t%-20.20s\t%d\t%d\t%d\t%-12s\n",
            handle, wItr->url, wItr->avgPing, wItr->minPing, wItr->maxPing, wItr->status
        );
        strcat(mesgOut, temp);
        memset(temp, '\0', MESG_SIZE*sizeof(char));
        wItr = wItr->nextWebsiteNodeInHandle;
    }
    strcat(mesgOut, "\n");
    
    return;
}

// A function to ping (/usr/bin/ping) Website and store its data. Need 'curl' and 'ping' installed
int pingWebsite(struct WebsiteNode *website) {
    FILE *fp;
    char *validateURLCmd1 = "curl -Is ";        // First part of command for URL validation
    char *validateURLCmd2 = " | head -n 1";     // Second part of command for URL validation
    char *ping = "/usr/bin/ping -q -c ";        // Command for pinging
    char output[1000];                          // Output from command
    char *parsed;                               // For parsing output
    char *err;                                  // For parsing output
    double pingData[4];                         // Store out ping data
    int i;
    
    // First, check if URL is valid
    char validateURLCmd[strlen(validateURLCmd1)+strlen(website->url)+strlen(validateURLCmd2)+1];
    snprintf(
        validateURLCmd,
        sizeof(validateURLCmd), 
        "%s%s%s",
        validateURLCmd1, website->url, validateURLCmd2
    );
    
    // Run curl
    fp = popen(validateURLCmd, "r");
    if (fp == NULL) {
        printf("Failed to run curl. Not installed?\n");
        return 0;
    }
    
    // Collect data and close
    while (fgets(output, sizeof(output), fp) != NULL) {
        // printf("%s", output);
    }
    pclose(fp);
    
    // "curl -Is <URL> | head -n 1" returns nothing if URL is invalid
    if (strlen(output) == 0) {
        strcpy(website->status, "INVALID_URL");
        return 1;
    }
    
    // If URL is valid, update Website status
    strcpy(website->status, "IN_PROGRESS");
    
    // Build command parameters
    char pingCmd[strlen(ping)+10+strlen(website->url)+1];
    snprintf(pingCmd, sizeof(pingCmd), "%s %d %s", ping, NUM_PINGS_PER_SITE, website->url);
    
    // Run ping
    fp = popen(pingCmd, "r");
    if (fp == NULL) {
        printf("Failed to run ping. Not installed?\n");
        return 0;
    }
    // Collect data and close
    while (fgets(output, sizeof(output), fp) != NULL) {
        //printf("%s", output);
    }
    pclose(fp);
    
    /* Parse data. Basically goes through the data and locates the double values we are looking for.
     * For example, the ping command returns this line at the end:
     * rtt min/avg/max/mdev = 27.848/31.269/34.939/2.907 ms
     * Parsing the float values and converting to int will give us the data we need.
     */
    err = output;
    parsed = output;
    i = 0;
    while (*parsed) {
        pingData[i] = strtod(parsed, &err);
        if (parsed == err) {
            parsed++;
        }
        else if ((err == NULL) || (*err == 0)) {
            break;
        }
        else {
            parsed = err + 1;
            i++;
        }
    }
    
    // Update Website with acquired data
    // For pingData[i], 0 = Minimum, 1 = Average, 2 = Maximum
    website->minPing = (int)pingData[0];
    website->avgPing = (int)pingData[1];
    website->maxPing = (int)pingData[2];
    if ((website->minPing == 0) && (website->avgPing == 0) && (website->maxPing == 0)) {
        strcpy(website->status, "BLOCKED");
    }
    else {
        strcpy(website->status, "COMPLETE");
    }

    return 1;
}
