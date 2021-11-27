/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "libsuspend"
//#define LOG_NDEBUG 0

#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <android-base/file.h>
// #include <android-base/logging.h>
#include <android-base/strings.h>
#include "autosuspend_ops.h"
#include <iostream>

#define BASE_SLEEP_TIME 100000
#define MAX_SLEEP_TIME 60000000
//#define BASE_SLEEP_TIME 100000
//#define MAX_SLEEP_TIME  100000

static int state_fd = -1;
static int wakeup_count_fd;

using android::base::ReadFdToString;
using android::base::Trim;
using android::base::WriteStringToFd;

static pthread_t suspend_thread;
static sem_t suspend_lockout;
static constexpr char sleep_state[] = "mem";
static void (*wakeup_func)(bool success) = NULL;
static int sleep_time = BASE_SLEEP_TIME;
static constexpr char sys_power_state[] = "/sys/power/state";
static constexpr char sys_power_wakeup_count[] = "/sys/power/wakeup_count";
static bool autosuspend_is_init = false;

static void update_sleep_time(bool success) {
    if (success) {
        sleep_time = BASE_SLEEP_TIME;
        return;
    }
    // double sleep time after each failure up to one minute
    sleep_time = MIN(sleep_time * 2, MAX_SLEEP_TIME);
}

static void* suspend_thread_func(void* arg __attribute__((unused))) {
    bool success = true;

    while (true) {
        update_sleep_time(success);
        usleep(sleep_time);
        success = false;
       /*[dba] LOG(VERBOSE)*/  std::cout << "read wakeup_count";
        lseek(wakeup_count_fd, 0, SEEK_SET);
        std::string wakeup_count;
        if (!ReadFdToString(wakeup_count_fd, &wakeup_count)) {
           /*[dba] PLOG(ERROR)*/  std::cout << "error reading from " << sys_power_wakeup_count;
            continue;
        }

        wakeup_count = Trim(wakeup_count);
        if (wakeup_count.empty()) {
           /*[dba] LOG(ERROR)*/  std::cout <<"empty wakeup count";
            continue;
        }

       /*[dba] LOG(VERBOSE)*/  std::cout << "wait";
        int ret = sem_wait(&suspend_lockout);
        if (ret < 0) {
           /*[dba] PLOG(ERROR)*/  std::cout << "error waiting on semaphore";
            continue;
        }

       /*[dba] LOG(VERBOSE)*/  std::cout << "write " << wakeup_count.c_str() << " to wakeup_count";
        if (WriteStringToFd(wakeup_count, wakeup_count_fd)) {
           /*[dba] LOG(VERBOSE)*/  std::cout << "write " << sleep_state << " to " << sys_power_state;
            success = WriteStringToFd(sleep_state, state_fd);

            void (*func)(bool success) = wakeup_func;
            if (func != NULL) {
                (*func)(success);
            }
        } else {
           /*[dba] PLOG(ERROR)*/  std::cout << "error writing to " << sys_power_wakeup_count;
        }

       /*[dba] LOG(VERBOSE)*/  std::cout << "release sem";
        ret = sem_post(&suspend_lockout);
        if (ret < 0) {
           /*[dba] PLOG(ERROR)*/  std::cout << "error releasing semaphore";
        }
    }
    return NULL;
}

static int init_state_fd(void) {
    if (state_fd >= 0) {
        return 0;
    }

    int fd = TEMP_FAILURE_RETRY(open(sys_power_state, O_CLOEXEC | O_RDWR));
    if (fd < 0) {
       /*[dba] PLOG(ERROR)*/  std::cout << "error opening " << sys_power_state;
        return -1;
    }

    state_fd = fd;
   /*[dba] LOG(INFO)*/  std::cout <<"init_state_fd success";
    return 0;
}

static int autosuspend_init(void) {
    if (autosuspend_is_init) {
        return 0;
    }

    int ret = init_state_fd();
    if (ret < 0) {
        return -1;
    }

    wakeup_count_fd = TEMP_FAILURE_RETRY(open(sys_power_wakeup_count, O_CLOEXEC | O_RDWR));
    if (wakeup_count_fd < 0) {
       /*[dba] PLOG(ERROR)*/  std::cout << "error opening " << sys_power_wakeup_count;
        goto err_open_wakeup_count;
    }

    ret = sem_init(&suspend_lockout, 0, 0);
    if (ret < 0) {
       /*[dba] PLOG(ERROR)*/  std::cout << "error creating suspend_lockout semaphore";
        goto err_sem_init;
    }

    ret = pthread_create(&suspend_thread, NULL, suspend_thread_func, NULL);
    if (ret) {
       /*[dba] LOG(ERROR)*/  std::cout <<"error creating thread: " << strerror(ret);
        goto err_pthread_create;
    }

   /*[dba] LOG(VERBOSE)*/  std::cout << "autosuspend_init success";
    autosuspend_is_init = true;
    return 0;

err_pthread_create:
    sem_destroy(&suspend_lockout);
err_sem_init:
    close(wakeup_count_fd);
err_open_wakeup_count:
    return -1;
}

static int autosuspend_wakeup_count_enable(void) {
   /*[dba] LOG(VERBOSE)*/  std::cout << "autosuspend_wakeup_count_enable";

    int ret = autosuspend_init();
    if (ret < 0) {
       /*[dba] LOG(ERROR)*/  std::cout <<"autosuspend_init failed";
        return ret;
    }

    ret = sem_post(&suspend_lockout);
    if (ret < 0) {
       /*[dba] PLOG(ERROR)*/  std::cout << "error changing semaphore";
    }

   /*[dba] LOG(VERBOSE)*/  std::cout << "autosuspend_wakeup_count_enable done";

    return ret;
}

static int autosuspend_wakeup_count_disable(void) {
   /*[dba] LOG(VERBOSE)*/  std::cout << "autosuspend_wakeup_count_disable";

    if (!autosuspend_is_init) {
        return 0;  // always successful if no thread is running yet
    }

    int ret = sem_wait(&suspend_lockout);

    if (ret < 0) {
       /*[dba] PLOG(ERROR)*/  std::cout << "error changing semaphore";
    }

   /*[dba] LOG(VERBOSE)*/  std::cout << "autosuspend_wakeup_count_disable done";

    return ret;
}

static int force_suspend(int timeout_ms) {
   /*[dba] LOG(VERBOSE)*/  std::cout << "force_suspend called with timeout: " << timeout_ms;

    int ret = init_state_fd();
    if (ret < 0) {
        return ret;
    }

    return WriteStringToFd(sleep_state, state_fd) ? 0 : -1;
}

static void autosuspend_set_wakeup_callback(void (*func)(bool success)) {
    if (wakeup_func != NULL) {
       /*[dba] LOG(ERROR)*/  std::cout <<"duplicate wakeup callback applied, keeping original";
        return;
    }
    wakeup_func = func;
}

struct autosuspend_ops autosuspend_wakeup_count_ops = {
    .enable = autosuspend_wakeup_count_enable,
    .disable = autosuspend_wakeup_count_disable,
    .force_suspend = force_suspend,
    .set_wakeup_callback = autosuspend_set_wakeup_callback,
};

struct autosuspend_ops* autosuspend_wakeup_count_init(void) {
    return &autosuspend_wakeup_count_ops;
}
