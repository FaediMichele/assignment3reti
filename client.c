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

typedef struct{
    char measure_type[6];
    uint16_t port;
    in_addr_t address;
    int n_probes;
    long msg_size;
    int server_delay;
} param_t;

/* <protocol_phase> <sp> <measure_type> <sp> <n_probes> <sp> <msg_size> <sp> <server_delay>\n */
const char hello_message_format[] = "%c %s %ld %ld %d \n";

/* <protocol_phase> <sp> <probe_seq_num> <sp> <payload>\n */
const char measurement_message_format[] = "%c %ld %s\n";


// Fill the param_t based on the input parameters. return -1 in case of error, else return the number of paramether used
int fillParam(int argc, char *argv[], param_t *param);

// Analyze the data received from the server and verify if is ok.
int analyzeData(char *received, int probe);

// Send the message to start the test to the sfd
int sendHello(int sfd, const param_t *param, char *sendData, char *receivedData);

void startChrono(struct timeval *time){
    gettimeofday(time, NULL);
}

long long stopChrono(struct timeval *time){
    struct timeval endTime;
    gettimeofday(&endTime, NULL);
    return (endTime.tv_usec - time->tv_usec) / 1000;
}

int main(int argc, char *argv[]){
    struct sockaddr_in server_addr; // struct containing server address information
    struct sockaddr_in client_addr; // struct containing client address information
    int sfd; // Server socket filed descriptor
    int cr; // Connect result
    int stop = 0;
    ssize_t byteRecv; // Number of bytes received
    ssize_t byteSent; // Number of bytes sent
    size_t msgLen;
    socklen_t serv_size;
    struct timeval chrono;
    long int time_all;
    
    param_t param;
    
    sfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    
    if (sfd < 0){
        perror("socket"); // Print error message
        exit(EXIT_FAILURE);
    }
    if( fillParam(argc, argv, &param) < 0){
        printf("Errore nei parametri\n");
        printf("%s <server IP (dotted notation)> <server port> <measure type (rtt / thput)> [-d delay][-p n_probes][-m messagesize]\n", argv[0]);
        exit(1);
    }
    char receivedData [param.msg_size]; // Data to be received
    char sendData [param.msg_size]; // Data to be sent
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = param.port;
    server_addr.sin_addr.s_addr = param.address;
    
    serv_size = sizeof(server_addr);

    cr = connect(sfd, (struct sockaddr *) &server_addr, sizeof(server_addr));

    if (cr < 0) {
        perror("connect"); // Print error message
        exit(EXIT_FAILURE);
    }
    
    sendHello(sfd, &param, sendData, receivedData);


    // MEASURE
    int probe = 1;
    while(probe < param.n_probes){
        snprintf( sendData, param.msg_size, measurement_message_format, PROTOCOL_PHASE_MEASURE, probe, "PAYLOAD START HERE:\n");
        startChrono(&chrono);
        byteSent = send(sfd, sendData, param.msg_size, 0);
        probe++;
        byteRecv = recv(sfd, receivedData, param.msg_size, 0);
        time_all += stopChrono(&chrono);
        if(analyzeData(receivedData, probe) < 0){
            printf("Error due a format problem\n");
        }
    }
    printf("Average RTT = %ld\n", time_all / probe);
    if( !strcmp(param.measure_type, MEASURE_THROUGHTPUT)){
        printf("THROUGHTPUT = %lf\n", param.msg_size / (time_all / probe));
    }
    return 0;
}
int analyzeData(char *received, int probe){
    char phase;
    int recvProbe;
    char *payload;
    sscanf(received, measurement_message_format, &phase, &recvProbe, &payload);
    if(phase != PROTOCOL_PHASE_MEASURE){
        return -1;
    }
    if(probe != recvProbe){
        return -1;
    }
    return 1;
}

int fillParam(int argc, char *argv[], param_t *param){
    int i;
    if (argc  < 3){
        return -1;
    }
    param->n_probes = 1024;
    param->msg_size = 256;
    param->server_delay = 0;
    param->port = htons(atoi(argv[2]));
    param->address = inet_addr(argv[1]);
    if( strcmp(argv[3], MEASURE_RTT) && strcmp(argv[3], MEASURE_THROUGHTPUT)){
        return -1;
    }
    strcpy(param->measure_type, argv[3]);

    for(i = 4; i < argc; i+= 2){
        if(!strcmp(argv[i], "-p") || !strcmp(argv[i], "--probes")){
            param->n_probes = atoi(argv[i + 1]);
            if(param->n_probes <= 0){
                return -1;
            }
        } else if(!strcmp(argv[i], "-m") || !strcmp(argv[i], "--messagesize")){
            param->msg_size = atoi(argv[i + 1]);
            if(param->msg_size <= 0){
                return -1;
            }
        } else if(!strcmp(argv[i], "-d" || !strcmp(argv[i], "--delay"))){
            param->server_delay = atoi(argv[i + 1]);
        } else{
            return -1;
        }
    }
    return argc - 3;
}

int sendHello(int sfd, const param_t *param, char *sendData, char *receivedData){
    snprintf( sendData, param->msg_size, hello_message_format, PROTOCOL_PHASE_HELLO,
        param->measure_type, param->n_probes, param->msg_size, param->server_delay);

    // HELLO
    send(sfd, sendData, strlen(sendData), 0);
    recv(sfd, receivedData, param->msg_size, 0);
    if( strcmp(receivedData, "200 OK - Ready")){
        printf("Error in the hello phase\n");
        if(strcmp(receivedData, "404 ERROR – Invalid Hello message")){
            printf("FATAL ERROR. Received message: \n%s", receivedData);
        }
    }
}