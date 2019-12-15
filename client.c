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
#define MEASURE_RTT "rtt"
#define MEASURE_THROUGHTPUT "thput"

#define HELLO_ACCEPTED "200 OK - Ready"
#define HELLO_REFUSED "404 ERROR – Invalid Hello message"
#define MEASURE_INVALID "404 ERROR - Invalid Measurement message"
#define HELLO_MESSAGE_SIZE 27
#define MEASURE_MESSAGE_SIZE 8 // without the payload

typedef struct{
    char measure_type[6];
    struct sockaddr_in server_addr;
    int n_probes;
    int measure_size_index;
    int server_delay;
} param_t;

/* <protocol_phase> <sp> <measure_type> <sp> <n_probes> <sp> <msg_size> <sp> <server_delay>\n */
const char hello_message_format[] = "%c %s %05d %05d %05d\n";

/* <protocol_phase> <sp> <probe_seq_num> <sp> <payload>\n */
const char measurement_message_format[] = "%c %05d %s\n";

const char bye_message[] = "b\n";
const int measure_size_rtt[] = {1, 100, 200, 400, 800, 1000};
const int measure_size_thput[] = {1000, 2000, 4000, 16000, 32000};


// Fill the param_t based on the input parameters. return -1 in case of error, else return the number of paramether used
int fillParam(int argc, char *argv[], param_t *param);

// Analyze the data received from the server and verify if is ok.
int analyzeData(char *received);

// Send the message to start the test to the sfd
int sendHello(int sfd, const param_t *param, char *sendData, char *receivedData, const int *measure_size_touse);

int sendByeMessage(int sfd, param_t *param);

void initializePayload(char *str, ssize_t lenght);

void startChrono(struct timeval *time);

int isErrorMeasure(char *message);

// Return the milliseocond passed.
double stopChrono(struct timeval time);

int main(int argc, char *argv[]){
    int sfd; // Server socket filed descriptor
    ssize_t byteRecv; // Number of bytes received
    ssize_t byteSent; // Number of bytes sent
    socklen_t serv_size;
    struct timeval chrono;
    double time_all;
    param_t param;
    int probe = 0;
    //int receivedProbe; // Probe received as ACK
    double rtt;
    int loss = 0;
    int measure_index = 0;
    
    if( fillParam(argc, argv, &param) < 0){
        printf("%s: unrecognized paramether.\n", argv[0]);
        printf("Usage = ./client <server IP (dotted notation)> [-d delay][-p n_probes] [-m measure(rtt / thput)]\n");
        exit(EXIT_FAILURE);
    }
    const int *measure_size_touse = strcmp(param.measure_type, MEASURE_RTT) ? 
            measure_size_thput:
            measure_size_rtt;
    long n_measure = measure_size_touse == measure_size_rtt ? sizeof(measure_size_rtt) : sizeof(measure_size_thput);
    n_measure/=sizeof(int);
    
    fflush(stdout);
    // for every payload size for the selected measure.
    for(measure_index = 0; measure_index < n_measure; measure_index++){
        long msg_size = measure_size_touse[measure_index] + MEASURE_MESSAGE_SIZE;
        char receivedData[msg_size]; // Data to be received
        char sendData[msg_size]; // Data to be sent
        initializePayload(sendData, msg_size);
        serv_size = sizeof(param.server_addr);
        time_all = 0;
        
        if ((sfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){
            perror("socket"); // Print error message
            exit(EXIT_FAILURE);
        }
        if (connect(sfd, (struct sockaddr *) &param.server_addr, serv_size) < 0) {
            perror("connect"); // Print error message
            exit(EXIT_FAILURE);
        }
        
        sendHello(sfd, &param, sendData, receivedData, measure_size_touse);


        // MEASURE
        probe = 0;
        
        while(probe < param.n_probes){

            snprintf(sendData, msg_size, measurement_message_format, PROTOCOL_PHASE_MEASURE,
                probe, "\nPAYLOAD START HERE:\n");
            sendData[msg_size - 1] = '\n';
            
            startChrono(&chrono);
            byteSent = send(sfd, sendData, msg_size, 0);
            if( byteSent <= 0) {
                perror("Error sending message");
                exit(EXIT_FAILURE);
            }
            
            byteRecv = recv(sfd, receivedData, msg_size, 0);
            if( byteRecv < 0){
                perror("Error receiving message");
                exit(EXIT_FAILURE);
            } else if( byteRecv == 0){
                printf("Connection closed by the server\n");
                exit(EXIT_FAILURE);
            }
            rtt = stopChrono(chrono);
            time_all += rtt;
            fwrite(receivedData, sizeof(char), msg_size, stdout);

            printf("probe_seq=%d rtt=%lf ms\n", probe, rtt);
            fflush(stdout);
            if(isErrorMeasure(receivedData)){
                printf("Server closed the connection due an error on received probes\n");
                close(sfd);
                exit(EXIT_FAILURE);
            }
            /*  Here the client resend the message that have lost
                I managed it for a mistake but I will keep it. 
            if((receivedProbe = analyzeData(receivedData)) < 0){
                printf("Error due a format problem\n");
            } else if(receivedProbe != probe){
                printf("Received wrong probe. Resending the last correct probe(%d)\n", receivedProbe);
                probe--;
                loss++;
            }*/
            
            probe++;
        }
        sendByeMessage(sfd, &param);
        printf("--- %s measure statistics ---\n", argv[1]);
        printf("%d message trasmitted, %d%% message loss, %ld message size, rtt = %gms, time = %gms\n",
            probe, loss * 100 / probe, msg_size - MEASURE_MESSAGE_SIZE,
            time_all / probe, time_all);
        if( !strcmp(param.measure_type, MEASURE_THROUGHTPUT)){
            // the /1000 is for kbps and *1000 for seconds.
            printf("Average throughtput = %.04lf kbps\n",
                ((double) msg_size) / ((time_all * 1000 / (double) probe) / 1000));
        }
        fflush(stdout);
        close(sfd);
        param.measure_size_index++;
    }
    
    return 0;
}
int analyzeData(char *received){
    char phase;
    int recvProbe;
    char *payload = NULL;
    sscanf(received, measurement_message_format, &phase, &recvProbe, payload);
    if(phase != PROTOCOL_PHASE_MEASURE){
        return -1;
    }
    return recvProbe;
}

int fillParam(int argc, char *argv[], param_t *param){
    int i;
    int measure_setted = 0;
    
    if (argc  < 3 || argc > 10 || argc % 2 != 1){
        return -1;
    }
    param->n_probes = 20;
    param->measure_size_index = 0;
    param->server_delay = 0;
    param->server_addr.sin_family = AF_INET;
    param->server_addr.sin_port = htons(atoi(argv[2]));
    param->server_addr.sin_addr.s_addr = inet_addr(argv[1]);

    for(i = 3; i < argc; i+= 2){
        if(!strcmp(argv[i], "-p") || !strcmp(argv[i], "--probes")){
            param->n_probes = atoi(argv[i + 1]);
            if(param->n_probes <= 0){
                return -1;
            }
        } else if(!strcmp(argv[i], "-d") || !strcmp(argv[i], "--delay")){
            param->server_delay = atoi(argv[i + 1]);
            if(param->server_delay < 0){
                return -1;
            }
        } else if(!strcmp(argv[i], "-m") || !strcmp(argv[i], "--measure")){
            printf("%s", argv[i + 1]);
            if( !strcmp(argv[i + 1], MEASURE_RTT) || !strcmp(argv[i + 1], MEASURE_THROUGHTPUT)){
                strcpy(param->measure_type, argv[i + 1]);
                measure_setted = 1;
            } else{
                return -1;
            }
        } else{
            return -1;
        }
    }

    // if the paramether -m is not used it will be asked
    if(!measure_setted){
        int len1 = strlen(MEASURE_RTT);
        int len2 = strlen(MEASURE_THROUGHTPUT);
        char measure[len1 > len2 ? len1 : len2];
        i = 0;
        do{
            printf("Witch measure (rtt / thput): ");
            scanf("%s", measure);
            if( strcmp(measure, MEASURE_RTT) && strcmp(measure, MEASURE_THROUGHTPUT)){
                i = 1;
            }
        } while(i);
        strncpy(param->measure_type, measure, sizeof(param->measure_type));
    }

    return argc - 2;
}

int sendHello(int sfd, const param_t *param, char *sendData, char *receivedData, const int *measure_size_touse){
    snprintf( sendData, HELLO_MESSAGE_SIZE,
        hello_message_format, PROTOCOL_PHASE_HELLO,
        param->measure_type, param->n_probes,
        measure_size_touse[param->measure_size_index], param->server_delay);
    
    send(sfd, sendData, strlen(sendData), 0);
    recv(sfd, receivedData, HELLO_MESSAGE_SIZE, 0);
    if( strcmp(receivedData, HELLO_ACCEPTED)){
        if(!strcmp(receivedData, HELLO_REFUSED)){
            printf("Hello refused. Received message: \n%s", receivedData);
        } else {
            printf("Error sending the hello message\n%s\n%s\n", sendData, receivedData);
        }
        fflush(stdout);
        return -1;
    }
    return 1;
}

int sendByeMessage(int sfd, param_t *param){
    return send(sfd, bye_message, sizeof(bye_message), 0) != sizeof(bye_message);
}


void initializePayload(char *str, ssize_t lenght){
    long i;
    for(i = 0; i < lenght; i++){
        str[i] = '-';
    }
}

void startChrono(struct timeval *time){
    if(gettimeofday(time, NULL) < 0){
        perror("Time");
    }
}

double stopChrono(struct timeval time){
    struct timeval endTime, res;
    if(gettimeofday(&endTime, NULL) < 0){
        perror("Time");
    }
    timersub(&endTime, &time, &res);
    return ((double) res.tv_sec) * 1000 + ((double) res.tv_usec) / 1000;
}

int isErrorMeasure(char *message){
    return !strcmp(message, MEASURE_INVALID);
}