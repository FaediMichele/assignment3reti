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
#define MEASURE_MESSAGE_SIZE 8 // without the payload
#define HELLO_MESSAGE_SIZE 27
#define BACK_LOG 10 // Maximum queued requests

typedef struct{
    unsigned long msg_size; // message size
    int expected_probe;     // numbre of expected probe
    int delay;              // time to wait to send the response
} param_t;

/* <protocol_phase> <sp> <measure_type> <sp> <n_probes> <sp> <msg_size> <sp> <server_delay>\n */
const char hello_message_format[] = "%c %s %d %d %d\n";

/* <protocol_phase> <sp> <probe_seq_num> <sp> <payload>\n */
const char measurement_message_format[] = "%c %d %s\n";

const char bye_message_response[] = "200 OK - Closing";

/* <protocol_phase>\n */
const char bye_message_format[] = "%c\n";

const char measure_accepted_m[] = "200 OK - Ready";

void open(char **argv, struct sockaddr_in *server_addr, int *sfd, socklen_t *cli_size);

int isByeMessage(char *message);

int parseHello(char *message, param_t *param);
int manageHello(int sfd, param_t *param);

int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr; // struct containing server address information
    struct sockaddr_in client_addr; // struct containing client address information
    int sfd; // Server socket filed descriptor
    int newsfd; // Client communication socket - Accept result
    ssize_t byteRecv; // Number of bytes received
    ssize_t byteSent; // Number of bytes to be sent
    socklen_t cli_size;
    param_t param;
    char client_add_s[INET_ADDRSTRLEN]; // String for the client address.
    int ok; // used like boolean

    if(argc != 2){
        printf("Errore nei parametri\n");
        printf("%s <server port (0 - 65535)> \n", argv[0]);
        exit(EXIT_FAILURE);
    }
    

    open(argv, &server_addr, &sfd, &cli_size);
    
    for(;;){
        printf("Waiting new request...\n");
        fflush(stdout);
        // Wait for incoming requests
        if (( newsfd = accept(sfd, (struct sockaddr *) &client_addr, &cli_size)) < 0){
            perror("accept"); // Print error message
            exit(EXIT_FAILURE);
        }
        
        inet_ntop( AF_INET, &client_addr, client_add_s, INET_ADDRSTRLEN );
        printf("Connection accepted: address: %s:%d\n", client_add_s, client_addr.sin_port);
        
        if(!manageHello(newsfd, &param)){
            perror("Error with the hello");
            close(newsfd);
            continue;
        }
        char receivedData[param.msg_size]; // also the send data
        ok = 0;
        while(1){
            byteRecv = recv(newsfd, receivedData, param.msg_size, 0);
            if (byteRecv < 0){
                perror("recv on measure");
                ok = 1;
            } else if (byteRecv == 0){
                perror("Connection closed by client");
                ok = 1;
            }
            if(isByeMessage(receivedData)){
                break;
            }
            fwrite(receivedData, sizeof(char), param.msg_size, stdout);
            usleep(param.delay * 1000);
            fflush(stdout);
            byteSent = send(newsfd, receivedData, param.msg_size, 0);
            if(byteSent != byteRecv){
                ok = 1;
            }
            if(ok){
                close(newsfd);
                newsfd = -1;
                break;
            }
        }
        if(newsfd < 0){
            continue;
        }
        send(newsfd, bye_message_response, sizeof(bye_message_response), 0);
        close(newsfd);
        printf("Connection closed from: %s:%hu\n", client_add_s, client_addr.sin_port);
    } // End of for(;;)
    close(sfd);
    return 0;
}

/*
    Initialize the socket with the port.
    The program is closed in case of error.
*/
void open(char **argv, struct sockaddr_in *server_addr, int *sfd, socklen_t *cli_size){
    int port = atoi(argv[1]);
    // Parse the port.
    if(port <= 0 || port >= __UINT16_MAX__){
        printf("Errore nei parametri\n");
        printf("%s <server port (0 - 65535)> \n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    *sfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (*sfd < 0){
        perror("socket"); // Print error message
        exit(EXIT_FAILURE);
    }
    
    // Initialize server address information.
    server_addr->sin_family = AF_INET;
    server_addr->sin_port = htons((uint16_t) port);
    server_addr->sin_addr.s_addr = INADDR_ANY;
    *cli_size = sizeof(server_addr);

    // Bind the address to the socket file descriptor.
    if (bind(*sfd, (struct sockaddr *) server_addr, sizeof(*server_addr)) < 0){
        perror("bind"); // Print error message
        exit(EXIT_FAILURE);
    }
    // Listen the socket file descriptor.
    if (listen(*sfd, BACK_LOG) < 0){
        perror("listen"); // Print error message
        exit(EXIT_FAILURE);
    }
}

/*  Verify if the message for the hello phase is correct.
    Return 0 in case of error
*/
int parseHello(char *message, param_t *param) {
    char phase;
    char measure[MEASURE_TYPE_SIZE];
    int n_probes;
    int msg_size;
    int delay;
    int res = sscanf(message, hello_message_format, &phase, measure, &n_probes, &msg_size, &delay);
    if(res < 0 || phase != 'h'){
        return 0;
    }
    param->delay = delay;
    fflush(stdout);
    param->msg_size = msg_size + MEASURE_MESSAGE_SIZE;
    return 1;
}

/*  Verify if the message is for the end phase
    Return 0 in case of error
*/
int isByeMessage(char *message){
    char phase;
    int res = sscanf(message, bye_message_format, &phase);
    if(res < 0 || phase != 'b'){
        return 0;
    }
    return 1;
}

int manageHello(int sfd, param_t *param){
    ssize_t byteRecv, byteSent;
    char helloMessage[HELLO_MESSAGE_SIZE];
    byteRecv = recv(sfd, helloMessage, HELLO_MESSAGE_SIZE, 0);
    if (byteRecv <= 0){
        printf("recv on hello\n");
        return 0;
    }
    
    if(parseHello(helloMessage, param) < 0){
        printf("hello message error\n");
        return 0;
    }
    
    byteSent = send(sfd, measure_accepted_m, sizeof(measure_accepted_m), 0);
    if(byteSent <= 0){
        printf("recv on hello\n");
        return 0;
    }
    
    return 1;
}