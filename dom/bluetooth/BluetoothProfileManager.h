/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_BluetoothProfileManager_h
#define mozilla_dom_bluetooth_BluetoothProfileManager_h

#include "BluetoothCommon.h"
#include "mozilla/ipc/UnixSocket.h"

using namespace mozilla::ipc;

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothReplyRunnable;

class BluetoothProfileManager
{
public:
/*  BluetoothProfileManager() = 0;
  ~BluetoothProfileManager() = 0;*/

//  virtual bool Connect(const nsAString& aDeviceAddress, BluetoothReplyRunnable* aRunnable) = 0;
  virtual void Disconnect() = 0;
  virtual bool Listen() = 0;
//  virtual int GetConnectedDevices(nsTArray<nsAString>& aDeviceArray) = 0;
/*  bool ConnectProfile(unsigned short aProfileId,
                      const nsAString& aDevicePath,
                      BluetoothReplyRunnable* aRunnable);
  void DisconnectProfile(unsigned short aProfileId);
  bool ListenProfile(unsigned short aProfileId);*/

private:
//  bool SetProfilePtr(unsigned short aProfileId);
//  nsRefPtr<BluetoothProfileManager> mProfilePtr;
};

END_BLUETOOTH_NAMESPACE

#endif
