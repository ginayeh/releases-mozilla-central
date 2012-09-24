/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BluetoothOppManager.h"

#include "BluetoothReplyRunnable.h"
#include "BluetoothService.h"
#include "BluetoothServiceUuid.h"
#include "ObexBase.h"

#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...) __android_log_print(ANDROID_LOG_INFO, "Bluetooth", args)
#else
#define LOG(args...) printf(args); printf("\n");
#endif

USING_BLUETOOTH_NAMESPACE

static BluetoothOppManager* sInstance = nullptr;
static int sConnectionId = 0;
static char sLastCommand = 0;

BluetoothOppManager::BluetoothOppManager() : mConnected(false)
{
}

BluetoothOppManager::~BluetoothOppManager()
{
}

//static
BluetoothOppManager*
BluetoothOppManager::Get()
{
  if (sInstance == nullptr) {
    sInstance = new BluetoothOppManager();
  }

  return sInstance;
}

bool
BluetoothOppManager::Connect(const nsAString& aDeviceObjectPath,
                             BluetoothReplyRunnable* aRunnable)
{
  MOZ_ASSERT(NS_IsMainThread());

  BluetoothService* bs = BluetoothService::Get();
  if (!bs) {
    NS_WARNING("BluetoothService not available!");
    return false;
  }

  nsString serviceUuidStr =
    NS_ConvertUTF8toUTF16(mozilla::dom::bluetooth::BluetoothServiceUuidStr::ObjectPush);

  nsRefPtr<BluetoothReplyRunnable> runnable = aRunnable;

  nsresult rv = bs->GetSocketViaService(aDeviceObjectPath,
                                        serviceUuidStr,
                                        BluetoothSocketType::RFCOMM,
                                        true,
                                        false,
                                        this,
                                        runnable);

  runnable.forget();
  return NS_FAILED(rv) ? false : true;
}

bool
BluetoothOppManager::Disconnect(BluetoothReplyRunnable* aRunnable)
{
  MOZ_ASSERT(NS_IsMainThread());

  // BluetoothService* bs = BluetoothService::Get();
  // if (!bs) {
  //   NS_WARNING("BluetoothService not available!");
  //   return false;
  // }

  // nsRefPtr<BluetoothReplyRunnable> runnable = aRunnable;

  // nsresult rv = bs->CloseSocket(this, runnable);
  // runnable.forget();

  nsresult rv = NS_OK;
  CloseSocket();
  return NS_FAILED(rv) ? false : true;
}

bool
BluetoothOppManager::SendFile(const nsAString& aFileUri,
                              BluetoothReplyRunnable* aRunnable)
{
  // First, send OBEX connection request to remote device.
  SendConnectReqeust();

  return true;
}

bool
BluetoothOppManager::StopSendingFile(BluetoothReplyRunnable* aRunnable)
{
  return true;
}

// Virtual function of class SocketConsumer
void
BluetoothOppManager::ReceiveSocketData(mozilla::ipc::UnixSocketRawData* aMessage)
{
  char responseCode = aMessage->mData[0];
  int packetLength = (((int)aMessage->mData[1]) << 8) | aMessage->mData[2];
  int receivedLength = aMessage->mSize;

  LOG("Response Code: %x, Packet Length: %d, Received Length = %d",
      responseCode, packetLength, receivedLength);

  if (sLastCommand == ObexRequestCode::Connect) {
    if (responseCode != ObexResponseCode::Success) {
      LOG("[OBEX] Connect failed");
    } else {
      LOG("[OBEX] Connect ok");

      mConnected = true;
    }
  } else if (sLastCommand == ObexRequestCode::Disconnect) {
    if (responseCode != ObexResponseCode::Success) {
      LOG("[OBEX] Disconnect failed");
    } else {
      LOG("[OBEX] Disconnect ok");

      mConnected = false;
    }
  } 
}

void
BluetoothOppManager::SendConnectReqeust()
{
  ++sConnectionId;

  // IrOBEX 1.2 3.3.1
  // [opcode:1][length:2][version:1][flags:1][MaxPktSizeWeCanReceive:2][Headers:var]
  uint8_t req[255];
  int index = 7;

  req[3] = 0x10; // version:1.0
  req[4] = 0x00; // flag:0x00
  req[5] = BluetoothOppManager::MAX_PACKET_LENGTH >> 8;
  req[6] = BluetoothOppManager::MAX_PACKET_LENGTH;

  index += AppendHeaderConnectionId(&req[index], sConnectionId);
  SetObexPacketInfo(req, ObexRequestCode::Connect, index);
  sLastCommand = ObexRequestCode::Connect;

  mozilla::ipc::UnixSocketRawData* s = 
    new mozilla::ipc::UnixSocketRawData(req, index);
  SendSocketData(s);

  LOG("[Gecko] OPP sent connect request!");
}

