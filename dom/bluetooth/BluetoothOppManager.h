/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluetoothoppmanager_h__
#define mozilla_dom_bluetooth_bluetoothoppmanager_h__

#include "BluetoothCommon.h"
#include "mozilla/ipc/UnixSocket.h"

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothReplyRunnable;

class BluetoothOppManager : public mozilla::ipc::UnixSocketConsumer
{
public:
  /*
   * Channel of reserved services are fixed values, please check
   * function add_reserved_service_records() in
   * external/bluetooth/bluez/src/adapter.c for more information.
   */
  static const int DEFAULT_OPP_CHANNEL = 10;

  ~BluetoothOppManager();
  static BluetoothOppManager* Get();
  void ReceiveSocketData(mozilla::ipc::UnixSocketRawData* aMessage);

  /*
   * If a application wnats to send a file, first, it needs to
   * call Connect() to create a valid RFCOMM connection. After
   * that, call SendFile()/StopSendingFile() to control file-sharing
   * process. At the end, the application will get a system message
   * indicating that te process is complete, then it can either call
   * Disconnect() to close RFCOMM connection or start another file-sending
   * thread via calling SendFile() again.
   */
  bool Connect(const nsAString& aDeviceObjectPath,
               BluetoothReplyRunnable* aRunnable);
  bool Disconnect(BluetoothReplyRunnable* aRunnable);

  bool SendFile(const nsAString& aFileUri,
                BluetoothReplyRunnable* aRunnable);
  bool StopSendingFile(BluetoothReplyRunnable* aRunnable);

private:
  BluetoothOppManager();
};

END_BLUETOOTH_NAMESPACE

#endif
