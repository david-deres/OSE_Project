#include <inc/lib.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>

#define PORT 194

#define BUFFSIZE 1024
#define MAXPENDING 5    // Max connection requests
#define MAXCLIENTS 128    // Max concurrent clients
#define NO_CLIENT 0

// reserve space in virtual memory in each client
__attribute__((aligned(PGSIZE)))
char receive_page[PGSIZE];

envid_t broadcast_env = 0;

// used to share sockets across different envs

static int
fd2sockid(int fd)
{
	struct Fd *sfd;
	int r;

	if ((r = fd_lookup(fd, &sfd)) < 0)
		return r;
	if (sfd->fd_dev_id != devsock.dev_id)
		return -E_NOT_SUPP;
	return sfd->fd_sock.sockid;
}

// allocates a fd from a socket id
// returns the fd on success and negative value on error
static int alloc_socket_fd(int sockid) {
    struct Fd *sfd;
	int r;
    if ((r = fd_alloc(&sfd)) < 0
	    || (r = sys_page_alloc(0, sfd, PTE_P|PTE_W|PTE_U|PTE_SHARE)) < 0) {
		fd_close(sfd, false);
		return r;
	}
    sfd->fd_dev_id = devsock.dev_id;
	sfd->fd_omode = O_RDWR;
	sfd->fd_sock.sockid = sockid;
    return fd2num(sfd);
}

// deallocates a fd for a socket
// returns 0 on success and negative value on error
static int free_socket_fd(int fd) {
    struct Fd *sfd;
	int r;

	if ((r = fd_lookup(fd, &sfd)) < 0)
		return r;
    sys_page_unmap(0, sfd);
    return 0;
}

static int
alloc_sockfd(int sockid)
{
	struct Fd *sfd;
	int r;

	if ((r = fd_alloc(&sfd)) < 0
	    || (r = sys_page_alloc(0, sfd, PTE_P|PTE_W|PTE_U|PTE_SHARE)) < 0) {
		fd_close(sfd, false);
		return r;
	}

	sfd->fd_dev_id = devsock.dev_id;
	sfd->fd_omode = O_RDWR;
	sfd->fd_sock.sockid = sockid;
	return fd2num(sfd);
}

static void
die(char *m)
{
	cprintf("%s\n", m);
	exit();
}

void handle_broadcast() {
    int clients[MAXCLIENTS] = {};
    int perm;
    envid_t source_id;

    while (1) {
        int sockid = ipc_recv(&source_id, receive_page, &perm);
        if (sockid < 0) {
            cprintf("error while receiving message in broadcast");
            continue;
        }

        if (perm == 0) {
            int i;

            // check if this is an existing socket to be removed
            for (i = 0; i < MAXCLIENTS; i++) {
                if (clients[i] == sockid) {
                    clients[i] = NO_CLIENT;
                    break;
                }
            }

            if (i != MAXCLIENTS) {
                // send aknowlegement of removal
                cprintf("removing client %d\n", sockid);
                ipc_send(source_id, true, NULL, 0);
                continue;
            }

            for (i=0; i < MAXCLIENTS; i++) {
                // check if there is spare room for a new client
                if (clients[i] == NO_CLIENT) {
                    clients[i] = sockid;
                    break;
                }
            }

            if (i != MAXCLIENTS) {
                // send aknowlegement of addition
                cprintf("adding client %d\n", sockid);

                write_to_socket(sockid, "welcome to the chat!\n");

                ipc_send(source_id, true, NULL, 0);
                continue;
            } else {
                // notify caller that no new clients can be added
                cprintf("couldnt add client %d\n", sockid);
                ipc_send(source_id, false, NULL, 0);
                continue;
            }
        } else if (perm == (PTE_P | PTE_U)) {
            int i;

            // send message to all clients
            cprintf("client %d: %s\n", sockid, receive_page);
            for (i = 0; i < MAXCLIENTS; i++) {
                if (clients[i] != NO_CLIENT && clients[i] != sockid) {
                    int sock_fd = alloc_socket_fd(clients[i]);
                    fprintf(sock_fd, "client %d says: %s", sockid, receive_page);
                    free_socket_fd(sock_fd);00
                }
            }

            // send aknowlegement of message
            ipc_send(source_id, true, NULL, 0);
        }
    }
}

void
handle_client(int sock)
{
	char buffer[BUFFSIZE + 1] = {};
    int received = -1;

    envid_t envid = fork();
    if (envid < 0) {
        cprintf("failed to spawn client");
        return;
    } else if (envid != 0) {
        return;
    }

    int sockid = fd2sockid(sock);
    if (sockid < 0) {
        exit();
    }

    do {
        // Receive message
	    if ((received = read(sock, buffer, BUFFSIZE)) < 0)
		    die("Failed to receive bytes from client");

        // broadcast message
        int r = sys_page_alloc(curenv->env_id, receive_page, PTE_P | PTE_U | PTE_W);
        strncpy(receive_page, buffer, received);
        ipc_send(broadcast_env, sockid, receive_page, PTE_P | PTE_U);
        ipc_recv(NULL, NULL, NULL);
        sys_page_unmap(curenv->env_id, receive_page);

    } while (received > 0);

    // notify broadcast that this client should be deleted
    ipc_send(broadcast_env, sockid, NULL, 0);
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
        int sockid = fd2sockid(clientsock);
        if (sockid < 0) {
            continue;
        }

        ipc_send(broadcast_env, sockid, NULL, 0);
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
