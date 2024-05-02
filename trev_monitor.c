#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
# define MAX_NAME_LEN 16
#define MAX_DEVICE_NAME 16
#define MAX_NUM_DEVICE 32

typedef struct {
    char name[MAX_NAME_LEN];
    uint32_t size;
    uint32_t offset;
    uint32_t num_elements;
    uint32_t element_size;
} metadata_field;

typedef struct {
    uint16_t num_fields;
    uint16_t metadata_offset;
} metadata_header;

int vmstate_compare(char * device, char * file1, char * file2, FILE * log_file);

void sigkill_handler(int);

// Initialize socket as global int so can be closed when program is killed
int sockfd;

// Script that will interact with QEMU monitor
int main(int argc, char* argv[]) {

    // Grab input file information
    if (argc <= 1) {
        printf("ERROR: No File inputted\n");
        return -1;
    }

    // Store all of the virtual device names
    FILE * input_file = fopen(argv[1], "r");
    char device[MAX_DEVICE_NAME];
    char device_list_save[MAX_NUM_DEVICE][MAX_DEVICE_NAME];
    char device_list_load[MAX_NUM_DEVICE][MAX_DEVICE_NAME];
    char device_list[MAX_NUM_DEVICE][MAX_DEVICE_NAME];
    int num_device = 0;
    printf("Devices to be scanned:\n");
    
    while (fgets(device, MAX_DEVICE_NAME, input_file) != NULL && num_device < MAX_NUM_DEVICE) {

        // Remove any trailing newline from device name
        size_t len = strlen(device);
        if (len > 0 && device[len - 1] == '\n') {
            device[len - 1] = '\0';
        }

        snprintf(device_list_save[num_device], MAX_DEVICE_NAME+5, "save_%s", device);
        snprintf(device_list_load[num_device], MAX_DEVICE_NAME+5, "load_%s", device);
        strcpy(device_list[num_device++], device);

        printf("\t%s\n", device);
    }

    // Initialize the kill signals to handle cleanup
    signal(SIGINT, sigkill_handler);
    signal(SIGKILL, sigkill_handler);
    struct sockaddr_un addr;
    char buffer[1024] = {0};

    // Create socket
    if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation error");
        exit(EXIT_FAILURE);
    }

    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, "/tmp/qemu-monitor.sock");

    // Connect to QEMU monitor socket (try 5 times)
    int count = 1;
    int conn = connect(sockfd, (struct sockaddr *)&addr, sizeof(addr));
    while (conn < 0 && count < 5) {
        sleep(1);
        connect(sockfd, (struct sockaddr *)&addr, sizeof(addr));
        count++;
    }

    if (conn < 0) {
        perror("ERROR: Failed to connect to QEMU Monitor");
        exit(EXIT_FAILURE);
    }

    // Initialize the setup: Delete previous snapshot, save new one
    const char *del = "delvm test\n";
    send(sockfd, del, strlen(del), 0);
    sleep(3);

    const char *cmd = "savevm test\n";
    send(sockfd, cmd, strlen(cmd), 0);

    // Receive and print the response (takes a while to save a snapshot)
    sleep(90);
    read(sockfd, buffer, 1024);
    printf("%s\n", buffer);

    // Initialize values to be used during fuzzing loop
    const char *load = "loadvm test\n";
    FILE * log_fd = fopen("fuzz_log.txt", "w");

    // Fuzzing loop
    int fuzz_count = 0;
    while(++fuzz_count <= 5) {
        printf("Running Load: Test %d\n", fuzz_count);
        // 1. Load VM (will collect info about loaded state)
        send(sockfd, load, strlen(load), 0);
        sleep(15);
        // Check Monitor output (for debug purposes)
        read(sockfd, buffer, 1024);
        printf("%s\n", buffer);

        fprintf(log_fd, "================Test %d================\n", fuzz_count);

        // 2. Compare the saved vs. loaded states of each inputted file
        for (int i = 0; i < num_device; i++) {
            if (vmstate_compare(device_list[i], device_list_save[i], device_list_load[i], log_fd) < 0) {
                perror("ERROR: Failed to compare state files\n");
                exit(0);
            }
            fprintf(log_fd, "\n");
        }

    }
    fclose(log_fd);

    return 0;
}

void sigkill_handler(int sig) {
    signal(sig, SIG_IGN);
    close(sockfd);
    exit(0);
}

int vmstate_compare(char * device, char* file1, char* file2, FILE * log_file) {

    // Open file pointer for metadata and data
    FILE * fd1 = fopen(file1, "r");
    FILE * fd2 = fopen(file2, "r");

    // Get metadata header
    metadata_header header1;
    metadata_header header2;
    fread(&header1, sizeof(metadata_header), 1, fd1);
    fread(&header2, sizeof(metadata_header), 1, fd2);

    fprintf(log_file, "Device: %s\n", device);

    // Check that the number of fields is the same
    if (header1.num_fields != header2.num_fields) {
        fprintf(log_file, "Number of fields differ, exiting.\n");
        return -1;
    }
    // Cast the binary data to metadata fields
    metadata_field md1[header1.num_fields];
    metadata_field md2[header2.num_fields];
    fread(&md1, sizeof(metadata_field), header1.num_fields, fd1);
    fread(&md2, sizeof(metadata_field), header2.num_fields, fd2);

    int differences = 0;

    // Iterate through each of the fields, compare each one
    for (int i = 0; i < header1.num_fields; i++){
        fseek(fd1, md1[i].offset, SEEK_SET);
        fseek(fd2, md2[i].offset, SEEK_SET);

        // Check if the data sizes match
        if (md1[i].size != md2[i].size) {
            differences++;
            fprintf(log_file, "\t%s Size differs: %d %d\n", md1[i].name, md1[i].size, md2[i].size);
        }
        else {
            // Allocate memory for both files' data
            void* buffer1 = malloc(md1[i].size);
            void* buffer2 = malloc(md2[i].size);

            // Read data from files
            fread(buffer1, md1[i].element_size, md1[i].num_elements, fd1);
            fread(buffer2, md2[i].element_size, md2[i].num_elements, fd2);

            // Check if the memory is different
            if (memcmp(buffer1, buffer2, md1[i].size) != 0) {
                differences++;
                fprintf(log_file, "\tField '%s': Data differs\n", md1[i].name);
            }

            // Free allocated memory
            free(buffer1);
            free(buffer2);
        }
    }

    fprintf(log_file, "Total # of Fields: %d\n", header1.num_fields);
    fprintf(log_file, "Total # of Differing Fields: %d\n", differences);

    // Close the files
    fclose(fd1);
    fclose(fd2);
    return differences;
}