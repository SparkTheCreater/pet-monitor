/**
 * @file    app_mail.c
 * @brief   发信线程。会话线程只把 JPEG 放进队列。
 */
#include "app_mail.h"

#include "media_jpeg.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define APP_MAIL_SLOT 4

typedef struct
{
    char name[APP_NAME_MAX];
    uint8_t *jpeg;
    size_t len;
}
Job;

struct AppMail
{
    const AppConfig *cfg;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int stop;
    int count;
    int head;
    Job job[APP_MAIL_SLOT];
    pthread_t thread;
    int on;
};

static void FreeJob(Job *job)
{
    media_jpeg_free(job->jpeg);
    job->jpeg = NULL;
    job->len = 0;
    job->name[0] = '\0';
}

static void SendJob(const AppConfig *cfg, const Job *job)
{
    AppEmailFile file;
    AppEmailMessage message;
    char subject[APP_NAME_MAX + 32];
    char filename[APP_NAME_MAX + 8];
    char err[128];
    int status;
    snprintf(subject, sizeof(subject), "%s 看到猫", job->name);
    snprintf(filename, sizeof(filename), "%s.jpg", job->name);
    memset(&file, 0, sizeof(file));
    file.data = job->jpeg;
    file.size = job->len;
    file.name = filename;
    file.type = "image/jpeg";
    memset(&message, 0, sizeof(message));
    message.from = cfg->from;
    message.to = cfg->to;
    message.subject = subject;
    message.text = subject;
    message.files = &file;
    message.file_count = 1;
    err[0] = '\0';
    status = app_email_send(&cfg->email, &message, err, sizeof(err));
    if (status != APP_EMAIL_OK)
    {
        fprintf(stderr, "[app] %s mail %d %s\n", job->name, status, err);
    }
}

static void *Worker(void *arg)
{
    AppMail *mail = arg;
    for (;;)
    {
        Job job;
        pthread_mutex_lock(&mail->mu);
        while (mail->count == 0 && !mail->stop)
        {
            pthread_cond_wait(&mail->cv, &mail->mu);
        }
        if (mail->count == 0)
        {
            pthread_mutex_unlock(&mail->mu);
            break;
        }
        job = mail->job[mail->head];
        mail->job[mail->head].jpeg = NULL;
        mail->head = (mail->head + 1) % APP_MAIL_SLOT;
        mail->count -= 1;
        pthread_cond_signal(&mail->cv);
        pthread_mutex_unlock(&mail->mu);
        if (mail->cfg->email_enable)
        {
            SendJob(mail->cfg, &job);
        }
        FreeJob(&job);
    }
    return NULL;
}

BaseStatus app_mail_open(const AppConfig *cfg, AppMail **out)
{
    AppMail *mail;
    if (out != NULL)
    {
        *out = NULL;
    }
    if (cfg == NULL || out == NULL)
    {
        return BASE_ERR;
    }
    mail = calloc(1, sizeof(*mail));
    if (mail == NULL)
    {
        return BASE_ERR;
    }
    mail->cfg = cfg;
    if (pthread_mutex_init(&mail->mu, NULL) != 0)
    {
        free(mail);
        return BASE_ERR;
    }
    if (pthread_cond_init(&mail->cv, NULL) != 0)
    {
        pthread_mutex_destroy(&mail->mu);
        free(mail);
        return BASE_ERR;
    }
    if (pthread_create(&mail->thread, NULL, Worker, mail) != 0)
    {
        pthread_cond_destroy(&mail->cv);
        pthread_mutex_destroy(&mail->mu);
        free(mail);
        return BASE_ERR;
    }
    mail->on = 1;
    *out = mail;
    return BASE_OK;
}

void app_mail_close(AppMail *mail)
{
    int i;
    if (mail == NULL)
    {
        return;
    }
    pthread_mutex_lock(&mail->mu);
    mail->stop = 1;
    pthread_cond_broadcast(&mail->cv);
    pthread_mutex_unlock(&mail->mu);
    if (mail->on)
    {
        pthread_join(mail->thread, NULL);
    }
    for (i = 0; i < APP_MAIL_SLOT; i++)
    {
        FreeJob(&mail->job[i]);
    }
    pthread_cond_destroy(&mail->cv);
    pthread_mutex_destroy(&mail->mu);
    free(mail);
}

BaseStatus app_mail_post(AppMail *mail, const char *camera, uint8_t *jpeg, size_t len)
{
    Job *job;
    int tail;
    if (mail == NULL || camera == NULL || camera[0] == '\0' || jpeg == NULL || len == 0)
    {
        media_jpeg_free(jpeg);
        return BASE_ERR;
    }
    pthread_mutex_lock(&mail->mu);
    if (mail->stop || mail->count >= APP_MAIL_SLOT)
    {
        pthread_mutex_unlock(&mail->mu);
        media_jpeg_free(jpeg);
        return BASE_ERR;
    }
    tail = (mail->head + mail->count) % APP_MAIL_SLOT;
    job = &mail->job[tail];
    snprintf(job->name, sizeof(job->name), "%s", camera);
    job->jpeg = jpeg;
    job->len = len;
    mail->count += 1;
    pthread_cond_signal(&mail->cv);
    pthread_mutex_unlock(&mail->mu);
    return BASE_OK;
}
