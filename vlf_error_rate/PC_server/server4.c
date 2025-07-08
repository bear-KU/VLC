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

#define RECEIVE_BUFFER_SIZE 1024
#define SEND_BUFFER_SIZE 1024
#define MAX_CONCURRENT_CONNECTIONS 2
#define SEND_JSON_SIZE 1100 // JSONデータのサイズ(これより大きくはない)

void *handle_client(void *arg);
int communication_sender(int sock_data);
int communication_receiver(int sock_data);
void generate_random_binary_data(char *buf, size_t size);
int generate_sender_data(char *json_string);

char sender_data[SEND_BUFFER_SIZE+1];
char receiver_data[RECEIVE_BUFFER_SIZE+1];
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
      int *sock_data_ptr = (int *)malloc(sizeof(int));
      if (sock_data_ptr == NULL) {
        printf("Failed to allocate memory for socket data pointer.\n");
        sleep(1);
        continue;
      }

      *sock_data_ptr = accept(sock_listen, NULL, NULL);
      if(*sock_data_ptr == -1) {
        printf("Failed to accept connection.\n");
        free(sock_data_ptr);
        if(errno == EINTR) continue;
        return 1; // Exit on error
      }

      // pthread_t thread_id;
      if (pthread_create(&thread_ids[i], NULL, handle_client, sock_data_ptr) != 0) {
        printf("Failed to create thread for client.\n");
        close(*sock_data_ptr);
        free(sock_data_ptr);
        continue; // Continue to the next iteration to accept another connection
      }
      
      // pthread_detach(thread_id); // Detach the thread to avoid memory leaks
      printf("[FD: %d] New client connected and handled by thread.\n", *sock_data_ptr);
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


void *handle_client(void *arg) {
  ssize_t received_len;
  int sock_data = *((int *)arg);
  free(arg);

  char recv_buf[RECEIVE_BUFFER_SIZE];

  printf("[Client FD: %d]Client connected.\n", sock_data);
  memset(recv_buf, 0, sizeof(recv_buf));
  received_len = recv(sock_data, recv_buf, sizeof(recv_buf) - 1, 0);
  if(received_len < 0) {
    printf("[Client FD: %d]Failed to receive data.\n\n", sock_data);
    close(sock_data);
    pthread_exit(NULL);
  }
  else if(received_len == 0) {
    printf("[Client FD: %d]Client disconnected.\n\n", sock_data);
    close(sock_data);
    pthread_exit(NULL);
  }
  recv_buf[received_len] = '\0';
  printf("[Client FD: %d]Received: %s.\n", sock_data, recv_buf);

  // SENDER または RECEIVER の役割を確認
  if(strcmp(recv_buf, "SENDER")==0) {
    int ret = communication_sender(sock_data);
    if(ret == 2) {
      printf("Failed to generate sender data.\n");
    }
    if(ret == 1) {
      printf("[SENDER FD: %d]Failed to send data.\n", sock_data);
    }
    printf("[SENDER FD: %d]Successfully sent data to SENDER.\n", sock_data);
  }
  else if(strcmp(recv_buf, "RECEIVER")==0) {
    int ret = communication_receiver(sock_data);
    if(ret == 1) {
      printf("[RECEIVER FD: %d]Failed to receive data.\n", sock_data);
    } else if(ret == 2) {
      printf("[RECEIVER FD: %d]Client disconnected.\n", sock_data);
    }
    printf("[RECEIVER FD: %d]Successfully receive data from RECEIVER.\n", sock_data);
  }
  else {
    printf("[Client FD: %d]Unknown role received: %s.\n\n", sock_data, recv_buf);
    close(sock_data);
    pthread_exit(NULL);
  }

  printf("[Client FD: %d]Connection closed.\n\n", sock_data);
  close(sock_data);
  pthread_exit(NULL);
}


// SENDER との通信を処理
int communication_sender(int sock_data) {
  char json_string[SEND_JSON_SIZE];
  int ret;

  generate_random_binary_data(sender_data, sizeof(sender_data));

  memset(json_string, 0, sizeof(json_string));
  ret = generate_sender_data(json_string);
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
  char temp_buf[RECEIVE_BUFFER_SIZE];

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

int generate_sender_data(char *json_string) {
  cJSON *root = cJSON_CreateObject();
  if(root == NULL) {
    printf("Failed to create JSON object.\n");
    return 1;
  }

  // Add data to the JSON object
  cJSON_AddStringToObject(root, "data", sender_data);
  cJSON_AddStringToObject(root, "chunk_size", chunk_size);
  cJSON_AddStringToObject(root, "signal_len", signal_len);

  if(!cJSON_PrintPreallocated(root, json_string, SEND_JSON_SIZE, 0)) {
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
