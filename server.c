#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <arpa/inet.h>

#define PROTOCOL_PHASE_HELLO 'h'
#define PROTOCOL_PHASE_MEASURE 'm'
#define PROTOCOL_PHASE_BYE 'b'
#define MEASURE_RTT "rtt"
#define MEASURE_THROUGHTPUT "thput"
#define MEASURE_TYPE_SIZE 6
#define HELLO_MESSAGE_SIZE 128
#define BACK_LOG 10 // Maximum queued requests

typedef struct{
    unsigned long msg_size;          // message size
    int expected_probe;     // numbre of expected probe
    int delay;              // time to wait to send the response
} param_t;

/* <protocol_phase> <sp> <measure_type> <sp> <n_probes> <sp> <msg_size> <sp> <server_delay>\n */
const char hello_message_format[] = "%c %s %d %lu %d \n";

/* <protocol_phase> <sp> <probe_seq_num> <sp> <payload>\n */
const char measurement_message_format[] = "%c %d %s\n";

const char measure_accepted_m[] = "200 OK - Ready";

void open(char **argv, struct sockaddr_in *server_addr, int *sfd, socklen_t *cli_size);

int parseMeasure(char *message, int actual_probes);

int parseHello(char *message, param_t *param);

int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr; // struct containing server address information
    struct sockaddr_in client_addr; // struct containing client address information
    int sfd; // Server socket filed descriptor
    int newsfd; // Client communication socket - Accept result
    ssize_t byteRecv; // Number of bytes received
    ssize_t byteSent; // Number of bytes to be sent
    socklen_t cli_size;
    param_t param;
    if(argc != 2){
        printf("Errore nei parametri\n");
        printf("%s <server port (0 - 65535)> \n", argv[0]);
        exit(EXIT_FAILURE);
    }
    char helloMessage[HELLO_MESSAGE_SIZE];

    open(argv, &server_addr, &sfd, &cli_size);
    
    for(;;){
        // Wait for incoming requests
        if (( newsfd = accept(sfd, (struct sockaddr *) &client_addr, &cli_size)) < 0){
            perror("accept"); // Print error message
            exit(EXIT_FAILURE);
        }
        byteRecv = recv(newsfd, helloMessage, HELLO_MESSAGE_SIZE, 0);
        if (byteRecv <= 0){
            perror("recv on hello");
            exit(EXIT_FAILURE);
        }
        if(parseHello(helloMessage, &param) < 0){
            perror("hello message error");
            break;
        }
        byteSent = send(newsfd, measure_accepted_m, sizeof(measure_accepted_m), 0);
        if(byteSent <= 0){
            perror("recv on hello");
            exit(EXIT_FAILURE);
        }

        char receivedData1[param.msg_size]; // also the send data
        char receivedData2[param.msg_size]; // also the send data
        // There is two because if the server receive a wrong probe number must be send the previous correct ack.
        int probe = 1;
        char *buff;
        while(probe != param.expected_probe){
            buff = probe % 2 == 1 ? receivedData1 : receivedData2;
            byteRecv = recv(newsfd, receivedData1, param.msg_size, 0);
            if (byteRecv <= 0){
                printf("recv on measure(size = %ld - actual = %ld): %s", param.msg_size, byteRecv, strerror(errno));
                perror("recv on measure");
                exit(EXIT_FAILURE);
            }
            usleep(param.delay);
            if(parseMeasure(buff, probe) < 0){
                probe--;
            }
            buff = probe % 2 == 1 ? receivedData1 : receivedData2;
            byteSent = send(newsfd, buff, param.msg_size, 0);
            if(byteSent != byteRecv){
                printf("sens on measure(size = %ld - actual = %ld): %s", param.msg_size, byteSent, strerror(errno));
                exit(EXIT_FAILURE);
            }
            probe++;
            
        }
    } // End of for(;;)
    close(sfd);
    return 0;
}

void open(char **argv, struct sockaddr_in *server_addr, int *sfd, socklen_t *cli_size){
    int port = atoi(argv[1]);
    if(port <= 0 || port >= __UINT16_MAX__){
        printf("Errore nei parametri\n");
        printf("%s <server port (0 - 65535)> \n", argv[0]);
        exit(1);
    }
    
    *sfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (*sfd < 0){
        perror("socket"); // Print error message
        exit(EXIT_FAILURE);
    }
    printf("PORT = %u\tSFD = %d\n", (uint16_t) port, *sfd);
    fflush(stdout);
    // Initialize server address information
    server_addr->sin_family = AF_INET;
    server_addr->sin_port = htons((uint16_t) port);
    server_addr->sin_addr.s_addr = INADDR_ANY;
    if (bind(*sfd, (struct sockaddr *) server_addr, sizeof(*server_addr)) < 0){
        printf("%s", strerror(errno));
        perror("bind"); // Print error message
        exit(EXIT_FAILURE);
    }
    *cli_size = sizeof(server_addr);
    // Listen for incoming requests
    if (listen(*sfd, BACK_LOG) < 0){
        perror("listen"); // Print error message
        exit(EXIT_FAILURE);
    }
}

int parseHello(char *message, param_t *param){
    char phase;
    char measure[MEASURE_TYPE_SIZE];
    int n_probes;
    unsigned long msg_size;
    int delay;
    int res = sscanf(message, hello_message_format, &phase, measure, &n_probes, &msg_size, &delay);
    if(res < 0 || phase != 'h'){
        return -1;
    }
    param->delay = delay;
    param->msg_size = msg_size;
    return 1;
}

int parseMeasure(char *message, int actual_probes){
    char phase;
    int probe;
    char *payload = NULL;
    int res = sscanf(message, measurement_message_format, &phase, &probe, payload);
    if(res < 0 || phase != 'm' || probe != actual_probes){
        return -1;
    }
    return 1;
}