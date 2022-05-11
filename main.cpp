#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>

#define MAX_CLIENTS 100
#define BUFFER_SZ 2048
#define LENGTH 2048

#define SERVER_IP "127.0.0.1"

#define MAX_MSG_LEN 2048
#define MAX_USERNAME_LEN 32
#define MIN_USERNAME_LEN 2
#define PACKET_LEN MAX_MSG_LEN + MAX_USERNAME_LEN

void trimStrLeft(char *arr, int length) {
    for (int i = 0; i < length; i++) { // trim \n
        if (arr[i] == '\n') {
            arr[i] = '\0';
            break;
        }
    }
}


static _Atomic unsigned int cli_count = 0;
static int uid = 10;

/* Client structure */
typedef struct {
    struct sockaddr_in address;
    int sockfd;
    int uid;
    char username[MAX_USERNAME_LEN];
} client_t;

client_t *clients[MAX_CLIENTS];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void printClientAddr(struct sockaddr_in clientAddr) {
    printf("%d.%d.%d.%d",
           clientAddr.sin_addr.s_addr & 0xff,
           (clientAddr.sin_addr.s_addr & 0xff00) >> 8,
           (clientAddr.sin_addr.s_addr & 0xff0000) >> 16,
           (clientAddr.sin_addr.s_addr & 0xff000000) >> 24);
}

// Add clients to array
void addClientArray(client_t *cl) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] == 0) { // empty slot in array
            clients[i] = cl; // assign client to empty slot
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Remove old client from array by UID
void removeClientFromArray(int uid) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] && clients[i]->uid == uid) {
                clients[i] = NULL;
                break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Broadcast msg to all clients
void broadcastMsg(char *s) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] != 0) {
            printf("%i ", i);
            send(clients[i]->sockfd, s, strlen(s) + 1, 0);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Client handling thread
void *clientHandler(void *arg) {
    char buff_out[BUFFER_SZ];
    char username[32];
//    int leave_flag = 0;
    cli_count++;
    client_t *cli = (client_t *)arg;
    // Receive username
    if (recv(cli->sockfd, username, MAX_USERNAME_LEN, 0) <= 0) {
        printf("Invalid username length.\n");
//        leave_flag = 1;
    } else {
        strcpy(cli->username, username);
        sprintf(buff_out, "%s has joined\n", cli->username);
        printf("%s", buff_out);
        broadcastMsg(buff_out);
    }
    memset(&buff_out, 0, BUFFER_SZ);
    while (1) {
        int receive = recv(cli->sockfd, buff_out, BUFFER_SZ, 0); // wait and recv msg from client
        if (receive > 0) {
            if (strlen(buff_out) > 0 && strcmp(buff_out, ":exit") != 0)  {
                broadcastMsg(buff_out);
//                trimStrLeft(buff_out, strlen(buff_out));
                printf("%s -> %s\n", cli->username, buff_out);
            }
        }
        if (receive <= 0 || strcmp(buff_out, ":exit") == 0) {
            sprintf(buff_out, "%s has left\n", cli->username);
            printf("%s", buff_out);
            broadcastMsg(buff_out);
//            leave_flag = 1;
            break;
        }
        memset(&buff_out, 0, BUFFER_SZ);
    }

    /* Delete client from queue and yield thread */
    close(cli->sockfd);
    removeClientFromArray(cli->uid);
    free(cli);
    cli_count--;
    pthread_detach(pthread_self());

    return NULL;
}

int main()
{
    int option = 1;
    int listenfd = 0, connfd = 0;
    struct sockaddr_in serv_addr;
    struct sockaddr_in cli_addr;
    pthread_t tid;
    /* Socket settings */
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    serv_addr.sin_port = 0;
    /* Ignore pipe signals */
    signal(SIGPIPE, SIG_IGN);
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (char *)&option, sizeof(option)) < 0) {
        perror("ERROR: setsockopt failed");
        return EXIT_FAILURE;
    }
    /* Bind */
    if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR: Socket binding failed");
        return EXIT_FAILURE;
    }
    int lenght = sizeof(serv_addr);
    if (::getsockname(listenfd, (struct sockaddr *)&serv_addr,
                    (socklen_t *)&lenght)) {
      perror("Error from getsockname()");
      exit(1);
    }
    /* Listen */
    if (listen(listenfd, 10) < 0) {
        perror("ERROR: Socket listening failed");
        return EXIT_FAILURE;
    }
    printf("IP: %s\n", inet_ntoa(serv_addr.sin_addr));
    printf("Server's port: %i\n", ntohs(serv_addr.sin_port));
    while (1) {
        socklen_t clilen = sizeof(cli_addr);
        connfd = accept(listenfd, (struct sockaddr *)&cli_addr, &clilen);
        // Max client checking
        if ((cli_count + 1) == MAX_CLIENTS)
        {
            printf("Too many clients. Rejected: ");
            printClientAddr(cli_addr);
            printf(":%d\n", cli_addr.sin_port);
            close(connfd);
            continue;
        }
        // Client settings
        client_t *cli = (client_t *)malloc(sizeof(client_t));
        cli->address = cli_addr;
        cli->sockfd = connfd;
        cli->uid = uid++;
        /* Add client to the queue and fork thread */
        addClientArray(cli);
        pthread_create(&tid, NULL, &clientHandler, (void *)cli);
        /* Reduce CPU usage */
        sleep(1);
    }
    return EXIT_SUCCESS;
}
