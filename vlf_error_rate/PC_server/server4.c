#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include "cJSON.h"

#define ROLE_SIZE 10
#define BINARY_DATA_BUFFER_SIZE 1024
#define HEX_DATA_BUFFER_SIZE (BINARY_DATA_BUFFER_SIZE / 4 + 1)
#define MAX_CONCURRENT_CONNECTIONS 2
#define SENDER_JSON_SIZE 1100 // JSONデータのサイズ(これより大きくはない)
#define ROLE_SENDER "SENDER"
#define ROLE_RECEIVER "RECEIVER"

typedef struct {
  int sock_data;
  char role[ROLE_SIZE];
} client_info;

void *handle_client(void *arg);
int communication_sender(int sock_data);
int communication_receiver(int sock_data);
void generate_random_binary_data(char *buf, size_t size);
int generate_sender_data(char *json_string, char *sender_data);
char binary_to_hex_char(char *binary);
void binary_to_hex_string(char *binary_buf, char *hex_buf) ;

char sender_data[HEX_DATA_BUFFER_SIZE+1];
char receiver_data[HEX_DATA_BUFFER_SIZE+1];
char chunk_size[10];
char signal_len[10];

pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char* argv[]) {
  int i;
  int sock_listen;
  int opt = 0;
  struct sockaddr_in sa;

  srand(time(NULL));

  memset((char *)&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET; // IPv4
  sa.sin_port = htons(61001); // Port number
  sa.sin_addr.s_addr = htonl(INADDR_ANY); // Set the IP address

  // 待受ソケット
  sock_listen = socket(sa.sin_family, SOCK_STREAM, 0);
  if(sock_listen == -1) {
    printf("Failed to create socket_listen.\n");
    return 1;
  }

  int yes = 1;
  setsockopt(sock_listen, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  // bind
  if(bind(sock_listen, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
    printf("Failed to bind socket_listen.\n");
    return 1;
  }

  // listen
  if(listen(sock_listen, MAX_CONCURRENT_CONNECTIONS) == -1) {
    printf("Failed to listen on socket_listen.\n");
    return 1;
  }

  // 入力部分
  printf("Server is listening on %s:%s\n", "192.168.1.49", "61001");
  while(1) {
    printf("Enter 1 to continue or 0 to shutdown the server: ");
    scanf("%d", &opt);
    if(!opt) {
      break; // Exit the loop to shutdown the server
    }
    printf("Enter the chunk size: ");
    scanf(" %10s", chunk_size);

    printf("Enter the signal length: ");
    scanf(" %10s", signal_len);

    
    // Accept a new connection
    printf("Waiting for a connection...\n");

    pthread_t thread_ids[MAX_CONCURRENT_CONNECTIONS];
    for (i=0; i<MAX_CONCURRENT_CONNECTIONS; i++) {
      client_info *info = (client_info *)malloc(sizeof(client_info));
      if (info == NULL) {
        printf("Failed to allocate memory for socket data pointer.\n");
        sleep(1);
        continue;
      }

      info->sock_data = accept(sock_listen, NULL, NULL);
      if(info->sock_data == -1) {
        printf("Failed to accept connection.\n");
        free(info);
        if(errno == EINTR) continue;
        return 1; // Exit on error
      }

      // 接続した順番に応じて ROLE を設定
      if(i == 0){
        strcpy(info->role, ROLE_RECEIVER);
      } else {
        strcpy(info->role, ROLE_SENDER);
      }
      printf("[FD: %d] New client connected with role: %s\n", info->sock_data, info->role);


      if (pthread_create(&thread_ids[i], NULL, handle_client, info) != 0) {
        printf("Failed to create thread for client.\n");
        close(info->sock_data);
        free(info);
        continue; // Continue to the next iteration to accept another connection
      }
      
      printf("[FD: %d] New client connected and handled by thread.\n", info->sock_data);
    }
    for (i=0; i<MAX_CONCURRENT_CONNECTIONS; i++) {
      pthread_join(thread_ids[i], NULL);
    }
    printf("All threads completed for this round.\n\n");

    printf("SENDER data: %s\n", sender_data);
    printf("RECEIVER data: %s\n", receiver_data);
  }
  close(sock_listen);
  printf("Server shutdown.\n");
  return 0;
}

// クライアントからの接続を処理する関数
void *handle_client(void *arg) {
  ssize_t received_len;
  client_info *info = (client_info *)arg;
  int sock_data = info->sock_data;
  char *role = info->role;
  char recv_role[ROLE_SIZE];

  // クライアントに ROLE を送信
  if(send(sock_data, role, strlen(role), 0) < 0) {
    printf("[Client FD: %d]Failed to send role.\n", sock_data);
    close(sock_data);
    free(info);
    pthread_exit(NULL);
  }
  printf("[Client FD: %d]Sent role: %s\n", sock_data, role);

  // 役割を取得
  memset(recv_role, 0, sizeof(recv_role));
  received_len = recv(sock_data, recv_role, sizeof(recv_role), 0);
  if(received_len < 0) {
    printf("[Client FD: %d]Failed to receive data.\n\n", sock_data);
    close(sock_data);
    free(info);
    pthread_exit(NULL);
  }
  else if(received_len == 0) {
    printf("[Client FD: %d]Client disconnected.\n\n", sock_data);
    close(sock_data);
    free(info);
    pthread_exit(NULL);
  }
  recv_role[received_len] = '\0';
  printf("[Client FD: %d]Received role: %s\n", sock_data, recv_role);

  // SENDER または RECEIVER の役割を確認
  if(strncmp(recv_role, ROLE_SENDER, strlen(recv_role))==0) {
    int ret = communication_sender(sock_data);
    if(ret != 0) {
      printf("[SENDER FD: %d]Communication failed.\n", sock_data);
    }
    printf("[SENDER FD: %d]Successfully sent data to SENDER.\n", sock_data);
  }
  else if(strncmp(recv_role, ROLE_RECEIVER, strlen(recv_role))==0) {
    int ret = communication_receiver(sock_data);
    if(ret != 0) {
      printf("[RECEIVER FD: %d]Communication failed\n", sock_data);
    }
    printf("[RECEIVER FD: %d]Successfully receive data from RECEIVER.\n", sock_data);
  }
  else {
    printf("[Client FD: %d]Unknown role received: %s\n\n", sock_data, recv_role);
    close(sock_data);
    pthread_exit(NULL);
  }

  printf("[Client FD: %d]Connection closed.\n\n", sock_data);
  close(sock_data);
  free(info);
  pthread_exit(NULL);
}

// SENDER との通信を処理
int communication_sender(int sock_data) {
  char json_string[SENDER_JSON_SIZE];
  char binary_data[BINARY_DATA_BUFFER_SIZE+1];
  int ret;

  generate_random_binary_data(binary_data, BINARY_DATA_BUFFER_SIZE);
  binary_to_hex_string(binary_data, sender_data);
  printf("Generated binary data: %s\n", binary_data);
  printf("Generated sender data: %s\n", sender_data);

  memset(json_string, 0, sizeof(json_string));
  ret = generate_sender_data(json_string, sender_data);
  if(ret != 0) {
    return 2;
  }

  pthread_mutex_lock(&data_mutex);

  if(send(sock_data, json_string, strlen(json_string), 0) < 0) {
    pthread_mutex_unlock(&data_mutex);
    return 1;
  }
  pthread_mutex_unlock(&data_mutex);
  return 0;
}

// RECEIVER との通信を処理
int communication_receiver(int sock_data) {
  ssize_t received_len;
  char temp_buf[HEX_DATA_BUFFER_SIZE];

  memset(temp_buf, 0, sizeof(temp_buf));
  received_len = recv(sock_data, temp_buf, sizeof(temp_buf) - 1, 0);
  if(received_len < 0) {
    return 1;
  }
  else if(received_len == 0) {
    return 2;
  }
  temp_buf[received_len] = '\0';
  
  pthread_mutex_lock(&data_mutex);
  memset(receiver_data, 0, sizeof(receiver_data));
  strncpy(receiver_data, temp_buf, sizeof(receiver_data) - 1);
  receiver_data[sizeof(receiver_data) - 1] = '\0';
  pthread_mutex_unlock(&data_mutex);
  
  return 0;
}

int generate_sender_data(char *json_string, char *sender_data) {
  cJSON *root = cJSON_CreateObject();
  if(root == NULL) {
    printf("Failed to create JSON object.\n");
    return 1;
  }

  // Add data to the JSON object
  cJSON_AddStringToObject(root, "data", sender_data);
  cJSON_AddStringToObject(root, "chunk_size", chunk_size);
  cJSON_AddStringToObject(root, "signal_len", signal_len);

  if(!cJSON_PrintPreallocated(root, json_string, SENDER_JSON_SIZE, 0)) {
    printf("Failed to print JSON object.\n");
    cJSON_Delete(root);
    return 1;
  }

  cJSON_Delete(root);
  return 0;
}

void generate_random_binary_data(char *buf, size_t size) {
  for(size_t i = 0; i < size; i++) {
    buf[i] = (rand() % 2) == 0 ? '0' : '1'; // int ではなく char
  }
  buf[size] = '\0';
}

char binary_to_hex_char(char *binary) {
  int value = 0;
  if(binary[0] == '1') value += 8;
  if(binary[1] == '1') value += 4;
  if(binary[2] == '1') value += 2;
  if(binary[3] == '1') value += 1;

  if(value < 10) {
    return '0' + value; // 数字の場合
  } else {
    return 'A' + (value - 10); // A-F の場合
  }
}

void binary_to_hex_string(char *binary_buf, char *hex_buf) {
  size_t len = strlen(binary_buf);
  size_t hex_index = 0;
  size_t i = 0;

  for (i=0; (i+3)<len; i+=4) {
    hex_buf[hex_index++] = binary_to_hex_char(&binary_buf[i]);
  }

  // 末尾が余る場合の処理
  size_t remainder = len % 4;
  if (remainder > 0) {
    char temp[5] = {0};
    strncpy(temp, &binary_buf[len - remainder], remainder);

    hex_buf[hex_index++] = binary_to_hex_char(temp);
  }

  hex_buf[hex_index] = '\0';
}

// 受信データとの整合性が必要
// 余りが出た場合0埋されているかわからない
// 最後がcのとき，1100 となり，1100なのか 110 なのか 11 なのか
// データの長さは管理される必要がある
