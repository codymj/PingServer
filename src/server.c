#include <arpa/inet.h>
#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define _GNU_SOURCE

#define MAX_WEBSITES 10             // Maximum number of Websites to ping per handle
#define NUM_WORKER_THREADS 5        // Number of worker threads
#define NUM_PINGS_PER_SITE 10       // Number of times to ping site
#define SOCKET_LISTEN_PORT 3333     // Port for listening socket
#define MESG_SIZE 2000              // Size of messages


/***************************************************************************************************
 * Global variables
 **************************************************************************************************/
// Global recursive mutex
pthread_mutex_t requestMutex;
pthread_mutexattr_t attr;

// Global condition variable
pthread_cond_t gotRequest = PTHREAD_COND_INITIALIZER;
pthread_cond_t noRequests = PTHREAD_COND_INITIALIZER;

// Number of pending requests
static int numRequests = 0;

// Global variable for creating unique handles for requests
static int handleCounter = 0;

// Global variables for identifying clients
static int clientID = 0;
static int numOfConnectedClients = 0;
struct SocketData {
    int clientID;
    int *socketDesc;
};

// Linked-list (queue) of Website structures for keeping track of each website's data
struct Website {
    unsigned int handle;
    //unsigned short numOfSitesInHandle;
    char url[50];
    short avgPing;
    short minPing;
    short maxPing;
    char status[10];
    struct Website *nextWebsiteInQueue;
};
struct Website *queueHead = NULL;             // Pointer to head of queue
struct Website *firstWebsiteInQueue = NULL;   // Pointer to first website in the queue
struct Website *lastWebsiteInQueue = NULL;    // Pointer to last website in the queue
static int websiteListSize = 0;

/***************************************************************************************************
 * Prototypes
 **************************************************************************************************/
void addWebsitesToQueue(
    struct Website *siteList[], 
    int size, 
    pthread_mutex_t *pmtx, 
    pthread_cond_t *pcond);
struct Website* getWebsiteFromQueue(pthread_mutex_t *pmtx);
void pingWebsite(struct Website *website);
void* handleRequestsLoop(void *tid);
void printReturnCode(int rc);
void* connectionHandler(void *sockData);
int parseWebsiteList(char *list);
void handleCommand(char cmd[], char arg[], struct SocketData *sockData);
void getHandleStatus(int handle, char *mesgOut);

/***************************************************************************************************
 * Main
 **************************************************************************************************/
// Main function
int main(int argc, char* argv[]) {
    // Initialize thread pool
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
    pthread_mutex_init(&requestMutex, &attr);
    pthread_t threadPool[NUM_WORKER_THREADS];
    
    // Create thread pool
    int i;
    for (i=0; i<NUM_WORKER_THREADS; i++) {
        pthread_create(&threadPool[i], NULL, handleRequestsLoop, (void*)&i);
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
        pthread_join(clientThread, NULL);
        printf("Connected clients: %d\n", numOfConnectedClients);
    }
    
    return 0;
}

/***************************************************************************************************
 * Definitions
 **************************************************************************************************/
// A function for adding a requested site to the queue
void addWebsitesToQueue(
struct Website *siteList[],
int size,
pthread_mutex_t *pmtx,
pthread_cond_t *pcond) {
    int returnCode;
    
    // Lock mutex to ensure exclusive access to queue before adding Website
    returnCode = pthread_mutex_lock(pmtx);
    if (returnCode) {
        printReturnCode(returnCode);
    }
    
    int i = 0;
    for (i=0; i<=size; i++) {
        if (siteList[i] == NULL) {
            break;
        }
        // If queue is empty, append first website
        if (firstWebsiteInQueue == NULL) {
            // Set queue head
            if (websiteListSize == 0) {
                queueHead = siteList[i];
            }
            // Move first and last website in queue to next available request
            firstWebsiteInQueue = lastWebsiteInQueue = siteList[i];
        }
        // Otherwise, append as usual
        else {
            lastWebsiteInQueue->nextWebsiteInQueue = siteList[i];
            lastWebsiteInQueue = siteList[i];
            lastWebsiteInQueue->nextWebsiteInQueue = NULL;
        }
        numRequests++;
        websiteListSize++;
        //printf("%s\n", siteList[i]->url);
    }

    // Unlock mutex
    returnCode = pthread_mutex_unlock(pmtx);
    if (returnCode) {
        printReturnCode(returnCode);
    }
    
    // Signal condition variable for threadpool
    returnCode = pthread_cond_signal(pcond);
    if (returnCode) {
        printReturnCode(returnCode);
    }
    
    // Go to handleRequestsLoop()
    return;
}

// A function for getting a Website from queue
struct Website* getWebsiteFromQueue(pthread_mutex_t *pmtx) {
    int returnCode;
    
    // Lock mutex to ensure exclusive access to queue
    returnCode = pthread_mutex_lock(pmtx);
    if (returnCode) {
        printReturnCode(returnCode);
    }
    
    struct Website *returnWebsite;
    
    returnWebsite = firstWebsiteInQueue;
    firstWebsiteInQueue = firstWebsiteInQueue->nextWebsiteInQueue;
    numRequests--;
    if (numRequests == 0) {
        pthread_cond_signal(&noRequests);
        pthread_cond_wait(&gotRequest, &requestMutex);
    }
    
    printf("Thread %ld grabbed %s\n", pthread_self(), returnWebsite->url);
    
    // Unlock mutex
    returnCode = pthread_mutex_unlock(pmtx);
    if (returnCode) {
        printReturnCode(returnCode);
    }
    
    return returnWebsite;
}

// A function to ping (/usr/bin/ping) Website and store its data. Need 'curl' and 'ping' installed
void pingWebsite(struct Website *website) {
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
        return;
    }
    
    // Collect data and close
    while (fgets(output, sizeof(output), fp) != NULL) {
        // printf("%s", output);
    }
    pclose(fp);
    
    // "curl -Is <URL> | head -n 1" returns nothing if URL is invalid
    if (strlen(output) <= 1) {
        strcpy(website->status, "INVALID_URL");
        return;
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
        return;
    }
    // Collect data and close
    while (fgets(output, sizeof(output), fp) != NULL) {
        printf("%s", output);
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
    strcpy(website->status, "COMPLETE");
    
    // Back to handleRequestsLoop()
    return;
}

// A loop for continuously handling requests from client
void* handleRequestsLoop(void *tid) {
    struct Website *website;
    
    while (1) {
        if (numRequests > 0) {
            website = getWebsiteFromQueue(&requestMutex);
            if (website) {
                pingWebsite(website);
            }
        }
    }
    
    return 0;
}

// Prints return code in case of an error
void printReturnCode(int rc) {
    printf("ERROR: Return code from pthread_ is %d\n", rc);
    exit(-1);
}

// Handler routine for each client
void* connectionHandler(void *sockData) {
    struct SocketData *sData = (struct SocketData*)sockData;
    int socket = *(int*)sData->socketDesc;
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
        handleCommand(command, arg, sData);
        
        // Clear memory buffers
        memset(mesgIn, '\0', MESG_SIZE*sizeof(char));
        memset(mesgOut, '\0', MESG_SIZE*sizeof(char));
        memset(command, '\0', MESG_SIZE*sizeof(char));
        memset(arg, '\0', MESG_SIZE*sizeof(char));
    }
    
    // Client disconnects
    printf("Client %d disconnected.\n", clientID);
    free(sData);
    numOfConnectedClients--;
    
    return 0;
}

// Takes command, validates and processes it
void handleCommand(char cmd[], char arg[], struct SocketData *sockData) {
    struct SocketData *sData = (struct SocketData*)sockData;
    int handle = -1;
    int socket = *(int*)sData->socketDesc;
    char mesgOut[MESG_SIZE];
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
        strcpy(temp1, "\nYour handle for this request is: ");
        strcpy(temp2, "To view status of this request, type\n\t showHandleStatus ");
        snprintf(mesgOut, MESG_SIZE, "%s%d\n%s%d\n\n", temp1, handle, temp2, handle);
        send(socket, mesgOut, strlen(mesgOut), MSG_NOSIGNAL);
    }
    else if (strcmp(cmd, "showHandles") == 0) {
    }
    else if (strcmp(cmd, "showHandleStatus") == 0) {
        getHandleStatus(handle, mesgOut);
        send(socket, mesgOut, strlen(mesgOut), MSG_NOSIGNAL);
    }
    else {
        strcpy(mesgOut, "\nError: Unrecognized command.\nType 'help'\n\n");
        send(socket, mesgOut, strlen(mesgOut), MSG_NOSIGNAL);
    }
    
    return;
}

// Parses a list of websites entered by client and adds them to queue
int parseWebsiteList(char *list) {
    handleCounter++;
    int handle = handleCounter;
    struct Website *siteList[MAX_WEBSITES];
    
    // Parse websites from list into separate URL strings
    char *parsedURLs[MAX_WEBSITES];
    const char *delim = ", \0";
    int i = 0;
    char *ptr;
    ptr = strtok(list, delim);
    while ((ptr) && (i<MAX_WEBSITES)) {
        parsedURLs[i] = ptr;
        i++;
        ptr = strtok(NULL, delim);
    }
    
    // Create and initialize Website structures from list
    for (i=0; i<MAX_WEBSITES; i++) {
        // End of list, break
        if (parsedURLs[i] == NULL) {
            break;
        }
        
        // Create new Website structure for element in list
        struct Website *newWebsite = (struct Website*)malloc(sizeof(struct Website));
        if (!newWebsite) {
            fprintf(stderr, "addWebsitesToQueue: Out of memory!\n");
            exit(1);
        }
        
        // Initialize Website structure
        newWebsite->handle = handle;
        strcpy(newWebsite->url, parsedURLs[i]);
        newWebsite->avgPing = -1;
        newWebsite->minPing = -1;
        newWebsite->maxPing = -1;
        strcpy(newWebsite->status, "IN_QUEUE");
        siteList[i] = newWebsite;
    }
    
    // Add *Website structure to queue
    addWebsitesToQueue(siteList, i, &requestMutex, &gotRequest);
    
    return handle;
}

// Returns status of handle requested
void getHandleStatus(int handle, char *mesgOut) {
    struct Website *siteList[MAX_WEBSITES];
    struct Website *itr = queueHead;
    
    // Empty list
    if (itr == NULL) {
        strcpy(mesgOut, "Nothing to show.\n");
        return;
    }
    
    // Loop through queue to find websites requested by handle and store into a list
    int i;
    int j = 0;
    for (i=0; i<websiteListSize; i++) {
        if (itr != NULL && itr->handle == handle) {
            siteList[j] = itr;
            j++;
        }
        itr = itr->nextWebsiteInQueue;
    }
    
    // Store formatted status and return
    char temp[MESG_SIZE];
    for (i=0; i<MAX_WEBSITES; i++) {
        if (siteList[i] == NULL) {
            break;
        }
        snprintf(
            temp,
            MESG_SIZE/MAX_WEBSITES,
            "\n%d\t%s\t\t%d\t%d\t%d\t%s\n",
            handle, siteList[i]->url, siteList[i]->avgPing,
            siteList[i]->minPing, siteList[i]->maxPing, siteList[i]->status
        );
        if (i == 0) {
            strcpy(mesgOut, temp);
        }
        else {
            strcat(mesgOut, temp);
        }
    }
    strcat(mesgOut, "\n");
    
    return;
}
