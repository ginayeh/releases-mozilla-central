/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:set ts=4 sw=4 sts=4 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsTracedCommon.h"
#include "jsapi.h"

#include "mozilla/ThreadLocal.h"
#include "nsThreadUtils.h"
#include "prenv.h"
#include "prthread.h"

#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>

static bool sDebugRunnable = false;

#ifdef MOZ_WIDGET_GONK
#include <android/log.h>
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "Task", args)
#else
#define LOG(args...) do {} while (0)
#endif

#if defined(__GLIBC__)
// glibc doesn't implement gettid(2).
#include <sys/syscall.h>
static pid_t gettid()
{
    return (pid_t) syscall(SYS_gettid);
}
#endif

#define MAX_THREAD_NUM 64

static TracedInfo sAllTracedInfo[MAX_THREAD_NUM];
static mozilla::ThreadLocal<TracedInfo *> sTracedInfo;
static pthread_mutex_t sTracedInfoLock = PTHREAD_MUTEX_INITIALIZER;

static TracedInfo *
AllocTraceInfo(int aTid)
{
    pthread_mutex_lock(&sTracedInfoLock);
    for (int i = 0; i < MAX_THREAD_NUM; i++) {
        if (sAllTracedInfo[i].threadId == 0) {
            TracedInfo *info = sAllTracedInfo + i;
            info->threadId = aTid;
            PRThread *thread = PR_GetCurrentThread();
            pthread_mutex_unlock(&sTracedInfoLock);
            return info;
        }
    }
    NS_ABORT();
    return NULL;
}

static void
_FreeTraceInfo(uint64_t aTid)
{
    pthread_mutex_lock(&sTracedInfoLock);
    for (int i = 0; i < MAX_THREAD_NUM; i++) {
        if (sAllTracedInfo[i].threadId == aTid) {
            TracedInfo *info = sAllTracedInfo + i;
            memset(info, 0, sizeof(TracedInfo));
            break;
        }
    }
    pthread_mutex_unlock(&sTracedInfoLock);
}

void
FreeTracedInfo()
{
    _FreeTraceInfo(gettid());
}

TracedInfo *
GetTracedInfo()
{
    if (!sTracedInfo.get()) {
        sTracedInfo.set(AllocTraceInfo(gettid()));
    }
    return sTracedInfo.get();
}

static const char*
GetCurrentThreadName()
{
    if (gettid() == getpid()) {
        return "main";
    } else if (const char *threadName = PR_GetThreadName(PR_GetCurrentThread())) {
        return threadName;
    } else {
        return "unknown";
    }
}

void
LogAction(ActionType aType, uint64_t aTid, uint64_t aOTid)
{
    TracedInfo *info = GetTracedInfo();

    if (sDebugRunnable && aOTid) {
        LOG("(tid: %d (%s)), task: %lld, orig: %lld", gettid(), GetCurrentThreadName(), aTid, aOTid);
    }
    TracedActivity *activity = info->activities + info->actNext;
    info->actNext = (info->actNext + 1) % TASK_TRACE_BUF_SIZE;
    activity->actionType = aType;
    activity->tm = (uint64_t)JS_Now();
    activity->taskId = aTid;
    activity->originTaskId = aOTid;
}

#ifndef NS_EXPORT
#define NS_EXPORT __attribute__ ((visibility("default")))
#endif

NS_EXPORT void
InitRunnableTrace()
{
    // This will be called during startup.
    //MOZ_ASSERT(NS_IsMainThread());
    if (!sTracedInfo.initialized()) {
        sTracedInfo.init();
    }

    if (PR_GetEnv("MOZ_DEBUG_RUNNABLE") || true) {
        sDebugRunnable = true;
    }
}

NS_EXPORT void
LogSamplerEnter(const char *aInfo)
{
    if (uint64_t currTid = *GetCurrentThreadTaskIdPtr() && sDebugRunnable) {
        LOG("(tid: %d), task: %lld, >> %s", gettid(), *GetCurrentThreadTaskIdPtr(), aInfo);
    }
}

NS_EXPORT void
LogSamplerExit(const char *aInfo)
{
    if (uint64_t currTid = *GetCurrentThreadTaskIdPtr() && sDebugRunnable) {
        LOG("(tid: %d), task: %lld, << %s", gettid(), *GetCurrentThreadTaskIdPtr(), aInfo);
    }
}

uint64_t *
GetCurrentThreadTaskIdPtr()
{
    TracedInfo *info = GetTracedInfo();
    return &info->currentTracedTaskId;
}

uint64_t
GenNewUniqueTaskId()
{
    pid_t tid = gettid();
    uint64_t taskid =
        ((uint64_t)tid << 32) | ++GetTracedInfo()->lastUniqueTaskId;
    return taskid;
}

void ClearTracedInfo() {
  *GetCurrentThreadTaskIdPtr() = 0;
}
