/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:set ts=4 sw=4 sts=4 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsTracedRunnable.h"

NS_IMPL_ISUPPORTS1(nsTracedRunnable, nsIRunnable)

NS_IMETHODIMP
nsTracedRunnable::Run()
{
    LogAction(ACTION_START, mTaskId, mOriginTaskId);

    SetupTracedInfo();
    nsresult rv = mFactualObj->Run();
    ClearTracedInfo();

    LogAction(ACTION_FINISHED, mTaskId, mOriginTaskId);
    return rv;
}

nsTracedRunnable::nsTracedRunnable(nsIRunnable *aFatualObj)
    : mOriginTaskId(0)
    , mFactualObj(aFatualObj)
{
    mTaskId = GenNewUniqueTaskId();
}

nsTracedRunnable::~nsTracedRunnable()
{
}

void
nsTracedRunnable::InitOriginTaskId()
{
    uint64_t origintaskid = *GetCurrentThreadTaskIdPtr();
    SetOriginTaskId(origintaskid);

    LogAction(ACTION_DISPATCH, mTaskId, mOriginTaskId);
}
