/**
 * @file    main.c
 * @brief   读 ini，跑各路相机，直到被打断。
 */
#include "app_run.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "conf/pet-monitor.ini";
    if (app_run(path) != BASE_OK)
    {
        fprintf(stderr, "app_run failed\n");
        return 1;
    }
    return 0;
}
