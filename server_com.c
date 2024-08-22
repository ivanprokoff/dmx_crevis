#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <termios.h>

#define DMX_CHANNELS 512
#define PORT 5005

unsigned char dmx_data[DMX_CHANNELS];
pthread_mutex_t dmx_mutex;

void initialize_dmx_data(unsigned char *dmx_data) {
    for (int i = 0; i < DMX_CHANNELS; i++) {
        dmx_data[i] = 0;
    }
}

int open_serial_port(const char *device) {
    int fd = open(device, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        perror("Unable to open serial port");
        exit(EXIT_FAILURE);
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd, &tty) != 0) {
        perror("Error from tcgetattr");
        close(fd);
        exit(EXIT_FAILURE);
    }

    cfsetospeed(&tty, B250000);
    cfsetispeed(&tty, B250000);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag &= ~IGNBRK;               
    tty.c_lflag = 0;                   
    tty.c_oflag = 0;                     
    tty.c_cc[VMIN] = 1;                    
    tty.c_cc[VTIME] = 5;                

    tty.c_cflag |= (CLOCAL | CREAD);         
    tty.c_cflag &= ~(PARENB | PARODD);     
    tty.c_cflag |= 0;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("Error from tcsetattr");
        close(fd);
        exit(EXIT_FAILURE);
    }

    return fd;
}

void send_dmx(int fd, unsigned char *dmx_data) {
    unsigned char break_signal = 0x00;

    tcflush(fd, TCOFLUSH);               

    cfsetospeed(&tty, B9600);              
    cfsetispeed(&tty, B9600);
    tcsetattr(fd, TCSANOW, &tty);
    write(fd, &break_signal, 1);           
    usleep(100);                         

    cfsetospeed(&tty, B250000);               
    cfsetispeed(&tty, B250000);
    tcsetattr(fd, TCSANOW, &tty);
    write(fd, dmx_data, DMX_CHANNELS);         

    printf("DMX Data Sent: ");
    for (int i = 0; i < DMX_CHANNELS; i++) {
        printf("%d ", dmx_data[i]);
    }
    printf("\n");
}

void *send_dmx_thread(void *arg) {
    int fd = *((int *)arg);
    while (1) {
        pthread_mutex_lock(&dmx_mutex);
        unsigned char data[DMX_CHANNELS];
        memcpy(data, dmx_data, DMX_CHANNELS);
        pthread_mutex_unlock(&dmx_mutex);

        send_dmx(fd, data);
        usleep(25000);
    }
    return NULL;
}

int main() {
    const char *serial_port = "/dev/ttyS0";
    int fd = open_serial_port(serial_port);

    initialize_dmx_data(dmx_data);
    pthread_mutex_init(&dmx_mutex, NULL);

    pthread_t dmx_thread;
    if (pthread_create(&dmx_thread, NULL, send_dmx_thread, &fd) != 0) {
        fprintf(stderr, "Error creating DMX thread\n");
        return EXIT_FAILURE;
    }

    int sockfd;
    struct sockaddr_in servaddr, cliaddr;
    char buffer[1024];
    socklen_t len;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    memset(&cliaddr, 0, sizeof(cliaddr));

    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(PORT);

    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    while (1) {
        len = sizeof(cliaddr);
        int n = recvfrom(sockfd, (char *)buffer, 1024, 0, (struct sockaddr *)&cliaddr, &len);
        if (n < 0) {
            perror("recvfrom error");
            continue;
        }
        buffer[n] = '\0';
        int channel, value;
        printf("Received data: %s\n", buffer);
        if (sscanf(buffer, "{\"channel\":%d,\"value\":%d}", &channel, &value) == 2) {
            if (channel >= 1 && channel <= DMX_CHANNELS && value >= 0 && value <= 255) {
                pthread_mutex_lock(&dmx_mutex);
                dmx_data[channel] = value;
                pthread_mutex_unlock(&dmx_mutex);
                printf("Parsed - Channel: %d, Value: %d\n", channel, value);
            } else {
                fprintf(stderr, "Invalid data: Channel %d, Value %d\n", channel, value);
            }
        } else {
            fprintf(stderr, "Failed to parse input data: %s\n", buffer);
        }
    }

    close(sockfd);
    close(fd);
    pthread_mutex_destroy(&dmx_mutex);

    return EXIT_SUCCESS;
}
