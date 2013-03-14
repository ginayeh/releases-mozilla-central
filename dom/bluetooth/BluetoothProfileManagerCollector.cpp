/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"

#include "BluetoothProfileManager.h"
#include "BluetoothProfileManagerCollector.h"

using namespace mozilla;
using namespace mozilla::ipc;
USING_BLUETOOTH_NAMESPACE

BluetoothProfileManagerCollector::BluetoothProfileManagerCollector()
{
  mBluetoothProfileManagerTable.Init(); 
}

BluetoothProfileManagerCollector::~BluetoothProfileManagerCollector()
{
  mBluetoothProfileManagerTable.Clear();
}

bool
BluetoothProfileManagerCollector::Find(uint16_t aProfileId,
                                       BluetoothProfileManager** aProfilePtr) const
{
  BT_LOG("[P] %s", __FUNCTION__);
  if (!mBluetoothProfileManagerTable.Get(aProfileId, aProfilePtr)) {
    BT_LOG("CANNOT find node");
    return false;
  }
//  BT_LOG("CAN find node, %p", **aProfilePtr);
  return true;

  // return mBluetoothProfileManagerTable.Get(aProfileId, aProfilePtr);
}

void
BluetoothProfileManagerCollector::Add(uint16_t aProfileId,
                                      BluetoothProfileManager* aProfilePtr)
{
  mBluetoothProfileManagerTable.Put(aProfileId, aProfilePtr);
/*    NS_WARNING("Failed to put an new entry");
    return false;
  }
  BT_LOG("[P] *aProfilePtr: %p", *aProfilePtr);
  return true;*/
//  return mBluetoothProfileManagerTable.Put(aProfileId, aProfilePtr);
}

void
BluetoothProfileManagerCollector::Delete(uint16_t aProfileId)
{
  mBluetoothProfileManagerTable.Remove(aProfileId);
/*    NS_WARNING("Failed to remove an entry");
    return false;
  }
  return true;*/
//  return mBluetoothProfileManagerTable.Remove(aProfileId);
}
