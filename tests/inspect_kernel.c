/* Standalone artifact validator. It uses the same bounds checks as EFI. */
#include <stdio.h>
#include <stdlib.h>
#include "../uefi/XnuLoader/MachO.h"
int main(int argc, char **argv) {
    FILE *file;
    long length;
    void *data;
    struct b2l_macho kernel = {0};
    int result;
    if (argc != 2) { fprintf(stderr, "Usage: %s kernel\n", argv[0]); return 2; }
    file = fopen(argv[1], "rb");
    if (!file) { perror(argv[1]); return 2; }
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) <= 0 ||
        (unsigned long)length > 256UL * 1024 * 1024 || fseek(file, 0, SEEK_SET)) {
        fprintf(stderr, "Invalid kernel file size\n"); fclose(file); return 2;
    }
    data = malloc((size_t)length);
    if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) {
        fprintf(stderr, "Cannot read kernel\n"); fclose(file); free(data); return 2;
    }
    fclose(file);
    result = b2l_parse_macho(data, (size_t)length, &kernel);
    free(data);
    if (result) { fprintf(stderr, "Rejected kernel: parser result %d\n", result); return 1; }
    printf("ARM64 XNU: %u segments, VA 0x%llx..0x%llx, entry 0x%llx\n",
        kernel.segment_count, (unsigned long long)kernel.virtual_base,
        (unsigned long long)kernel.virtual_end, (unsigned long long)kernel.entry);
    return 0;
}
