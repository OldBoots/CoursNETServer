#include <sys/socket.h>
//#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
//#include <errno.h>
#include <string.h>
#include <pthread.h>
//#include <sys/types.h>
//#include <signal.h>

// Задаем статичные данные, для удобного
//        заполнения параметров функциый
#define MAX_CLIENTS 100
#define BUFFER_SZ 2048
#define LENGTH 2048

#define SERVER_IP "127.0.0.1"

#define MAX_MSG_LEN 2048
#define MAX_USERNAME_LEN 32
#define MIN_USERNAME_LEN 2
#define PACKET_LEN MAX_MSG_LEN + MAX_USERNAME_LEN

static _Atomic unsigned int cli_count = 0;
static int uid = 10;

// Данные о клиенте
typedef struct {
    struct sockaddr_in address;
    int sockfd;
    int uid;
    char username[MAX_USERNAME_LEN];
} client_t;
// Массив структур
client_t *clients[MAX_CLIENTS];

//pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
// Добавление клиента в массив структур
void addClientArray(client_t *cl) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] == 0) { // empty slot in array
            clients[i] = cl; // assign client to empty slot
            break;
        }
    }
}

// Удаление клиента из массива структур
void removeClientFromArray(int uid) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] && clients[i]->uid == uid) {
                clients[i] = NULL;
                break;
        }
    }
}

// Рассылка сообщений всем клиентам
void broadcastMsg(char *s) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] != 0) {
            send(clients[i]->sockfd, s, strlen(s) + 1, 0);
        }
    }
}

// Хендлер клиентов
void *clientHandler(void *arg) {
    char buff_out[BUFFER_SZ];
    char username[MAX_USERNAME_LEN];
    cli_count++;
    client_t *cli = (client_t *)arg;
    // Ждем подключения пользователя
    if (recv(cli->sockfd, username, MAX_USERNAME_LEN, 0) <= 0) {
        printf("Invalid username length.\n");
    } else {
        // Если имя коррекктно то записываем username в структуру клиента
        strlcpy(cli->username, username, MAX_USERNAME_LEN);
        // Генерируем сообщение о подключении для пользователя
        sprintf(buff_out, "%s has joined\n", cli->username);
        // Выводим на сервере
        printf("%s", buff_out);
        // Выводим в чат
        broadcastMsg(buff_out);
    }
    // Чистим буфер
    memset(&buff_out, 0, BUFFER_SZ);
    char str[255];
    // Ждем сообщений клиента
    while (1) {
        int receive = recv(cli->sockfd, buff_out, BUFFER_SZ, 0);
        if (receive > 0) {
            // Если сообщение получено и это не ":exit", то вывести
            // его на сервере и в чат
            if (strlen(buff_out) > 0 && strcmp(buff_out, ":exit") != 0)  {
                sprintf(str, "%s\t-> %s\n", cli->username, buff_out);
                broadcastMsg(str);
                printf("%s", str);
            }
        }
        // Если клиент закрыл приложение или отправил команду ":exit",
        // сообщаем об этом в чат и на сервер и
        // прерываем цикл прослушки
        if (receive <= 0 || strcmp(buff_out, ":exit") == 0) {
            sprintf(buff_out, "%s has left\n", cli->username);
            printf("%s", buff_out);
            broadcastMsg(buff_out);
            break;
        }
        // Отчищаем буфер и вспомогательную строку
        memset(&str, 0, 255);
        memset(&buff_out, 0, BUFFER_SZ);
    }
    // Закрываем сокет
    close(cli->sockfd);
    // Удаляем клиента из массива структур по id
    removeClientFromArray(cli->uid);
    free(cli);
    cli_count--;
    // Завершаем поток
    pthread_detach(pthread_self());
    return NULL;
}

int main() {
    int listenfd = 0, connfd = 0;
    struct sockaddr_in serv_addr;
    struct sockaddr_in cli_addr;
    pthread_t tid;
    // Настраиваем сокет сервера
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    // Генерируем незанятый порт
    serv_addr.sin_port = 0;
    // Биндим сокет
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
    // Устанавливаем прослушку
    if (listen(listenfd, 10) < 0) {
        perror("ERROR: Socket listening failed");
        return EXIT_FAILURE;
    }
    // Вывод данных о сервере для подключения клиентов
    printf("IP: %s\n", inet_ntoa(serv_addr.sin_addr));
    printf("Server's port: %i\n", ntohs(serv_addr.sin_port));
    // Цикл, ожидающий подключения пользователей
    while (1) {
        socklen_t clilen = sizeof(cli_addr);
        connfd = accept(listenfd, (struct sockaddr *)&cli_addr, &clilen);
        // Записываем данные о клиенте, для внесения их в структуру данных
        client_t *cli = (client_t *)malloc(sizeof(client_t));
        cli->address = cli_addr;
        cli->sockfd = connfd;
        cli->uid = uid++;
        // Добавление подключившегося пользователяв массив структур
        addClientArray(cli);
        // Создаем поток прослушки сообщений пользователя
        pthread_create(&tid, NULL, &clientHandler, (void *)cli);
    }
    return EXIT_SUCCESS;
}
