#include <inc/lib.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>

#define PORT 7

#define BUFFSIZE 1024
#define MAXPENDING 5    // Max connection requests
#define MAXCLIENTS 128    // Max concurrent clients
#define NO_CLIENT (~0)

// reserve space in virtual memory in each client
__attribute__((aligned(PGSIZE)))
char receive_page[PGSIZE];

envid_t broadcast_env = 0;

static void
die(char *m)
{
	cprintf("%s\n", m);
	exit();
}

void handle_broadcast() {
    int clients[MAXCLIENTS] = {};
    memset(clients, NO_CLIENT, MAXCLIENTS);
    int perm;
    envid_t source_id;

    while (1) {
        int sock = ipc_recv(&source_id, receive_page, &perm);
        if (sock < 0) {
            cprintf("error while receiving message in broadcast");
            continue;
        }

        if (perm == 0) {
            int i;

            // check if this is an existing socket to be removed
            for (i = 0; i < MAXCLIENTS; i++) {
                if (clients[i] == sock) {
                    clients[i] = NO_CLIENT;
                    break;
                }
            }

            if (i != MAXCLIENTS) {
                // send aknowlegement of removal
                cprintf("removing client %d\n", sock);
                ipc_send(source_id, true, NULL, 0);
                continue;
            }

            for (i=0; i < MAXCLIENTS; i++) {
                // check if there is spare room for a new client
                if (clients[i] == NO_CLIENT) {
                    clients[i] = sock;
                    break;
                }
            }

            if (i != MAXCLIENTS) {
                // send aknowlegement of addition
                cprintf("adding client %d\n", sock);
                ipc_send(source_id, true, NULL, 0);
                continue;
            } else {
                // notify caller that no new clients can be added
                cprintf("couldnt add client %d\n", sock);
                ipc_send(source_id, false, NULL, 0);
                continue;
            }
        }
    }
}

void
handle_client(int sock)
{
	char buffer[BUFFSIZE];
    int received = -1;

    envid_t envid = fork();
    if (envid < 0) {
        cprintf("failed to spawn client");
        return;
    } else if (envid != 0) {
        return;
    }

    do {
        // Receive message
	    if ((received = read(sock, buffer, BUFFSIZE)) < 0)
		    die("Failed to receive bytes from client");

        int r = sys_page_alloc(curenv->env_id, receive_page, PTE_P | PTE_U | PTE_W);
        memcpy(receive_page, buffer, received);
        ipc_send(broadcast_env, sock, receive_page, PTE_P | PTE_U);
        sys_page_unmap(curenv->env_id, receive_page);

    } while (received > 0);
	close(sock);
    // notify broadcast that this client should be deleted
    ipc_send(broadcast_env, sock, NULL, 0);
    // wait for the client to be deleted
    ipc_recv(NULL, NULL, NULL);
    exit();
}

void
umain(int argc, char **argv)
{
	int serversock, clientsock;
	struct sockaddr_in chatserver, chatclient;
	char buffer[BUFFSIZE];
	unsigned int chatlen;
	int received = 0;

	// Create the TCP socket
	if ((serversock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
		die("Failed to create socket");

	cprintf("opened socket\n");

	// Construct the server sockaddr_in structure
	memset(&chatserver, 0, sizeof(chatserver));       // Clear struct
	chatserver.sin_family = AF_INET;                  // Internet/IP
	chatserver.sin_addr.s_addr = htonl(INADDR_ANY);   // IP address
	chatserver.sin_port = htons(PORT);		  // server port

	cprintf("trying to bind\n");

	// Bind the server socket
	if (bind(serversock, (struct sockaddr *) &chatserver,
		 sizeof(chatserver)) < 0) {
		die("Failed to bind the server socket");
	}

	// Listen on the server socket
	if (listen(serversock, MAXPENDING) < 0)
		die("Failed to listen on server socket");

	cprintf("bound\n");

    broadcast_env = fork();
    if (broadcast_env < 0) {
        cprintf("failed to spawn broadcast env");
        return;
    } else if (broadcast_env == 0) {
        handle_broadcast();
    }

	// Run until canceled
	while (1) {
		unsigned int clientlen = sizeof(chatclient);
		// Wait for client connection
		if ((clientsock =
		     accept(serversock, (struct sockaddr *) &chatclient,
			    &clientlen)) < 0) {
			die("Failed to accept client connection");
		}
		cprintf("Client connected: %s\n", inet_ntoa(chatclient.sin_addr));

        // add new client to broadcast
        ipc_send(broadcast_env, clientsock, NULL, 0);
        if (ipc_recv(NULL, NULL, NULL) <= 0) {
            char *error = "unable to add client";
            write(clientsock, error, strlen(error));
            close(clientsock);
        } else {
            handle_client(clientsock);
        }
	}

	close(serversock);

}
