/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluetoothhfpmanager_h__
#define mozilla_dom_bluetooth_bluetoothhfpmanager_h__

#include "BluetoothCommon.h"
#include "BluetoothRilListener.h"
#include "mozilla/ipc/UnixSocket.h"
#include "nsIObserver.h"

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothReplyRunnable;

class BluetoothHfpManager : public mozilla::ipc::UnixSocketConsumer
                          , public nsIObserver
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  ~BluetoothHfpManager();

  static BluetoothHfpManager* Get();
  void ReceiveSocketData(mozilla::ipc::UnixSocketRawData* aMessage);

  bool Connect(const nsAString& aDeviceObjectPath,
               BluetoothReplyRunnable* aRunnable);
  bool Disconnect(BluetoothReplyRunnable* aRunnable);
  void SendLine(const char* aMessage);
  void CallStateChanged(int aCallIndex, int aCallState,
                        const char* aNumber, bool aIsActive);

private:
  BluetoothHfpManager();


  nsresult HandleVolumeChanged(const nsAString& aData);
  bool BroadcastSystemMessage(const char* aCommand,
                              const int aCommandLength);

  int mCurrentVgs;
  int mCurrentCallIndex;
  int mCurrentCallState;
  BluetoothRilListener *mListener;
};

END_BLUETOOTH_NAMESPACE

#endif
