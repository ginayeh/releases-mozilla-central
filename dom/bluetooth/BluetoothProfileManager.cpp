/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"

#include "BluetoothProfileManager.h"

using namespace mozilla;
using namespace mozilla::ipc;
USING_BLUETOOTH_NAMESPACE

/*BluetoothProfileManager::BluetoothProfileManager()
{
  mProfilePtr = nullptr;
}

BluetoothProfileManager::~BluetoothProfileManager()
{
  mProfilePtr = nullptr;
}*/

/*bool
BluetoothProfileManager::SetProfilePtr(unsigned short aProfileId)
{
  bool result = true;
  switch (aProfileId) {
    case 0x111E:
      mProfilePtr = BluetoothHfpManager::Get();
      break;
    case 0x1105:
      mProfilePtr = BluetoothOopManager::Get();
      break;
    default:
      mProfilePtr = nullptr;
      NS_WARNING("Unknown profile");
      result = false;
  }

  return result;
}*/

/*bool
BluetoothProfileManager::ConnectProfile(unsigned short aProfileId,
                                 const nsAString& aDevicePath,
                                 BluetoothReplyRunnable* aRunnable)
{
  if (SetProfilePtr(aProfileId) && mProfilePtr->Connect(aDevicePath, aRunnable)) {
    return true;
  }

  return false;
}

void
BluetoothProfileManager::DisconnectProfile(unsigned short aProfileId)
{
  if (SetProfilePtr(aProfileId)) {
    mProfilePtr->Disconnect();
  }
}

bool
BluetoothProfileManager::ListenProfile(unsigned short aProfileId)
{
  if (SetProfilePtr(aProfileId) && mProfilePtr->Listen()) {
    return true;
  }

  return false; 
}*/
