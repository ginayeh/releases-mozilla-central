/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#define TASK_TRACE_BUF_SIZE 2048

enum ActionType {
  ACTION_DISPATCH,
  ACTION_START,
  ACTION_FINISHED
};

/**
 * A snapshot of the current tracing activity.
 */
struct TracedActivity {
  ActionType actionType;
  uint64_t tm;                  // Current time in uni-seconds.
  uint64_t taskId;              // Task id.
  uint64_t originTaskId;        // Origin's task id.
};

/**
 * Each thread owns a TracedInfo on its tread local storage, keeping track of
 * all information of TracedRunnable tasks dispatched from this thread.
 */
struct TracedInfo {
  uint32_t threadId;            // Thread id of its owner thread.
  uint64_t currentTracedTaskId; // Task id of the currently-traced TracedRunnable task.
  uint32_t lastUniqueTaskId;    // A serial number to generate an unique task id for a new TracedRunnable.

  int actNext;
  int actFirst;
  TracedActivity activities[TASK_TRACE_BUF_SIZE];
};

/**
 * Returns the pointer of currently-traced task id, access from the TracedInfo
 * of current thread.
 */
uint64_t *GetCurrentThreadTaskIdPtr();

/**
 * Generates an unique task id for a TeacedRunnable base on its owner thread
 * and the last unique task id.
 */
uint64_t GenNewUniqueTaskId();

/**
 * Free the TracedInfo allocated on its tread local storage.
 */
void FreeTracedInfo();

/**
 * Returns the TracedInfo of this thread, allocate a new one if not exit.
 */
TracedInfo *GetTracedInfo();

/**
 * Log the snapshot the current tracing activity.
 */
void LogAction(ActionType aType, uint64_t aTid, uint64_t aOTid);

/**
 * Clear the TracedInfo when the Run() of its factual object is done,
 * this should be in pair with SetupTracedInfo().
 */
void ClearTracedInfo();
