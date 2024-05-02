#include <stdlib.h>
#include <stdio.h>
#include "qemu/osdep.h"
#include "hw/pci/pci_device.h"
#include "hw/scsi/esp.h"
#include "hw/usb/ccid.h"
#include "chardev/char-fe.h"
#include "net/net.h"
#include "hw/net/mii.h"
#include <time.h>

#define MAX_NAME_LEN 16

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

FILE ** vmstate_init_statefile(char * filename, int num_fields);

int vmstate_save_field(FILE * metadata_fp, FILE * data_fp, int offset, 
                        int element_size, int n, char * name, void * values);

void randomize_nbytes(void * field, size_t n, int num_fields);
void init_rand(void);

