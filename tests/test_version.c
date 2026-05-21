#include "gymsocial.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    const char *version = gymsocial_version();

    if (version == NULL) {
        fprintf(stderr, "FAIL: gymsocial_version() retornou NULL\n");
        return 1;
    }

    if (strlen(version) == 0) {
        fprintf(stderr, "FAIL: gymsocial_version() retornou string vazia\n");
        return 1;
    }

    printf("PASS: gymsocial_version() = %s\n", version);
    return 0;
}
