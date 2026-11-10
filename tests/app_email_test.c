/**
 * @file    app_email_test.c
 * @brief   空参数应当直接失败，并且 C 程序能链上发信库。
 */
#include "app_email.h"

#include <stdio.h>

int main(void)
{
    char err[64];
    AppEmailStatus status = app_email_send(NULL, NULL, err, sizeof err);
    if (status != APP_EMAIL_ERR)
    {
        fprintf(stderr, "fail null send %d\n", (int)status);
        return 1;
    }
    return 0;
}
