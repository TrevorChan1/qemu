#include "hw/state-save.h"

FILE ** vmstate_init_statefile(char * filename, int num_fields) {
    // Initialize file pointer output for metadata and data pointers
    FILE ** fp = malloc(2 * sizeof(FILE *));
    fp[0] = fopen(filename, "w");
    fp[1] = fopen(filename, "w");

    // Initialize metadata header values
    metadata_header header;
    header.num_fields = num_fields;
    header.metadata_offset = sizeof(metadata_header);

    // Write the metadata header to the file
    fwrite(&header, sizeof(metadata_header), 1, fp[0]);
    int data_offset = sizeof(metadata_header) + header.num_fields * sizeof(metadata_field);
    fseek(fp[1], data_offset, SEEK_SET);

    return fp;
}

int vmstate_save_field(FILE * metadata_fp, FILE * data_fp, int offset, 
                        int element_size, int n, char * name, void * values) {
    // Initialize metadata entry
    metadata_field md;
    strcpy(md.name, name);
    md.size = element_size * n;
    md.num_elements = n;
    md.element_size = element_size;
    md.offset = offset;
    
    // Write metadata entry to metadata
    fwrite(&md, sizeof(metadata_field), 1, metadata_fp);

    // Write data to data
    fwrite(values, md.element_size, md.num_elements, data_fp);

    // Return the offset to be used for the next entry
    int data_offset = offset += md.num_elements * md.element_size;
    return data_offset;
}

// Function for randomly generating n bytes
void randomize_nbytes(void * field, size_t n, int num_fields) {
    // Create n-byte random buffer stream
    int size = n * num_fields;
    unsigned char * stream = malloc(size);

    for (int i = 0; i < size; i++) {
        stream[i] = rand() % 256;
    }

    if (stream != NULL) {
        memcpy(field, stream, size);
        free(stream);
    }

}

// Function to initialize the random seed
void init_rand(void) {
    srand ((unsigned int) time (NULL));
}