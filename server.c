#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <pthread.h>
#include <libftdi>

#define DMX_CHANNELS 512
#define PORT 5005

unsigned char dmx_data[DMX_CHANNELS];
pthread_mutex_t dmx_mutex;

void initialize_dmx_data(unsigned char *dmx_data) {
    for (int i = 0; i < DMX_CHANNELS; i++) {
        dmx_data[i] = 0;
    }
}

void send_dmx(struct ftdi_context *ftdi, unsigned char *dmx_data) {
    unsigned char break_signal = 0x00;
    ftdi_set_line_property(ftdi, BITS_8, STOP_BIT_2, NONE);
    ftdi_set_baudrate(ftdi, 250000);

    ftdi_usb_purge_buffers(ftdi);
    ftdi_set_baudrate(ftdi, 9600);
    ftdi_write_data(ftdi, &break_signal, 1);
    usleep(100); // 100us break time

    ftdi_set_baudrate(ftdi, 250000);
    ftdi_write_data(ftdi, dmx_data, DMX_CHANNELS);

    printf("DMX Data Sent: ");
    for (int i = 0; i < DMX_CHANNELS; i++) {
        printf("%d ", dmx_data[i]);
    }
    printf("\n");
}

void *send_dmx_thread(void *arg) {
    struct ftdi_context *ftdi = (struct ftdi_context *)arg;
    while (1) {
        pthread_mutex_lock(&dmx_mutex);
        unsigned char data[DMX_CHANNELS];
        memcpy(data, dmx_data, DMX_CHANNELS);
        pthread_mutex_unlock(&dmx_mutex);

        send_dmx(ftdi, data);
        usleep(25000); // Отправка данных каждые 25 мс
    }
    return NULL;
}


int main() {
    struct ftdi_context *ftdi;
    if ((ftdi = ftdi_new()) == 0) {
        fprintf(stderr, "ftdi_new failed\n");
        return EXIT_FAILURE;
    }

    if (ftdi_usb_open(ftdi, 0x0403, 0x6001) < 0) {
        fprintf(stderr, "Unable to open FTDI device: %s\n", ftdi_get_error_string(ftdi));
        ftdi_free(ftdi);
        return EXIT_FAILURE;
    }

    initialize_dmx_data(dmx_data);
    pthread_mutex_init(&dmx_mutex, NULL);

    pthread_t dmx_thread;
    if (pthread_create(&dmx_thread, NULL, send_dmx_thread, ftdi) != 0) {
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
    ftdi_usb_close(ftdi);
    ftdi_free(ftdi);
    pthread_mutex_destroy(&dmx_mutex);

    return EXIT_SUCCESS;
}
