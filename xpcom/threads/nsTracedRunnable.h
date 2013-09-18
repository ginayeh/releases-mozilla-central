/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __NS_TRACERUNNABLE_H_
#define __NS_TRACERUNNABLE_H_

#include "nsIRunnable.h"
#include "nsAutoPtr.h"
#include "nsTracedCommon.h"

class nsTracedRunnable : public nsIRunnable {
public:

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIRUNNABLE

  nsTracedRunnable(nsIRunnable *aFactualObj);
  virtual ~nsTracedRunnable();

  uint64_t GetOriginTaskId() {
    return mOriginTaskId;
  }

  void SetOriginTaskId(uint64_t aId) {
    mOriginTaskId = aId;
  }

  // Allocates a TracedInfo for the current thread on its thread local storage
  // if not exist, and sets the origin taskId of this runnable to the currently-
  // traced taskID from the TracedInfo of current thread.
  void InitOriginTaskId();

  // Returns the original runnable object wrapped by this TracedRunnable.
  already_AddRefed<nsIRunnable> GetFatualObject() {
    nsCOMPtr<nsIRunnable> factualObj = mFactualObj;

    return factualObj.forget();
  }

private:
  // Before calling the Run() of its factual object, sets the currently-traced
  // taskID from the TracedInfo of current thread, to its origin's taskId.
  // Setup other information is needed.
  // Should call ClearTracedInfo() to reset them when done.
  void SetupTracedInfo() {
    *GetCurrentThreadTaskIdPtr() = GetOriginTaskId();
  }

  // Its own taskID, an unique number base on the tid of current thread and
  // a last unique taskID from the TracedInfo of current thread.
  uint64_t mTaskId;

  // The origin taskId, it's being set to the currently-traced taskID from the
  // TracedInfo of current thread in the call of InitOriginTaskId().
  uint64_t mOriginTaskId;

  // The factual runnable object wrapped by this TracedRunnable wrapper.
  nsCOMPtr<nsIRunnable> mFactualObj;
};


#endif /* __NS_TRACERUNNABLE_H_ */
