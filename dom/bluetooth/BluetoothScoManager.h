/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluetoothscomanager_h__
#define mozilla_dom_bluetooth_bluetoothscomanager_h__

#include "BluetoothCommon.h"
#include "mozilla/ipc/UnixSocket.h"

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothReplyRunnable;

class BluetoothScoManager : public mozilla::ipc::UnixSocketConsumer
{
public:
  ~BluetoothScoManager();

  static BluetoothScoManager* Get();
  void ReceiveSocketData(mozilla::ipc::UnixSocketRawData* aMessage);

  bool Connect(const nsAString& aDeviceObjectPath); //,
//               nsRunnable* aRunnable);
  bool Disconnect();// nsRunnable* aRunnable);
  void SetConnected(bool aConnected);
  bool IsConnected();
//  void SendLine(const char* aMessage);
/*  void CallStateChanged(int aCallIndex, int aCallState,
                        const char* aNumber, bool aIsActive);*/

private:
  BluetoothScoManager();
  void CreateScoSocket(const nsAString& aDevicePath);
  bool mConnected;
};

END_BLUETOOTH_NAMESPACE

#endif
