PingServer
==========
A server/client based implementation for pinging websites designed for the Linux operating system.

Dependencies:
-------------
    curl
    ping

Compiling:
----------
    gcc server.c -o server -Wall -lpthread
    gcc client.c -o client -Wall -lpthread

Running:
--------
    Terminal 1:     ./server
    Terminal 2+:    ./client

Commands:
---------
    * help - Displays a list of commands and their syntax.
    * pingSites <url list> - Up to 10 websites to ping.
        Example: pingSites www.google.com,www.espn.com,www.gentoo.org
    * showHandles`` - Lists the total amount of requests by all clients.
    * showHandleStatus [integer] - Displays the status of pinged websites for that handle.
        (Integer is optional, if left off, will display status of every handle)
    * exit - Disconnects from the server.

Design:
-------

                           firstHandleNodeInQueue             lastHandleNodeInQueue
                                      |                                 |
                                      v                                 v

                                      1        2        3      ...      n
                                    =====    =====    =====           =====
                                    | H | -> | H | -> | H | -> ... -> | H | -> \0
                                    =====    =====    =====           =====
                                      |        |        |               |
                                      v        v        v               v
                                    -----    -----    -----           -----
    firstWebsiteNodeInQueue --> 1   | W |    | W |    | W |           | W |    
                                    -----    -----    -----           -----
                                      |        |        |               |
                                      v        v        v               v
                                    -----    -----    -----           -----
                                2   | W |    | W |    | W |           | W |        H - HandleNode
                                    -----    -----    -----           -----        W - WebsiteNode
                                      |        |        |               |          n: n > 0
                                      v        v        v               v          m: 0 < m <= 10
                                .     .        .        .               .
                                .     .        .        .               .
                                .     .        .        .               .
                                      |        |        |               |
                                      v        v        v               v
                                    -----    -----    -----           -----
     lastWebsiteNodeInQueue --> m   | W |    | W |    | W |           | W |    
                                    -----    -----    -----           -----
                                      |        |        |               |
                                      v        v        v               v
                                      
                                      \0       \0       \0              \0


When a client enters a valid pingSites command, a HandleNode is created and added to the handle 
queue for processing. Each HandleNode has a linked-list of WebsiteNodes also in a queue. After the
HandleNode is added to the queue, the thread pool immediately begins work by pinging the
WebsiteNodes in the order they were added.

Once the thread pool finishes with one HandleNode, the next HandleNode (if available) is processed
in the same way until all HandleNodes are complete.

Each client is also provided their own thread for inputting commands to the server.
