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

  // Start parsing response
  ObexHeaderSet headerSet(sLastCommand);

  if (sLastCommand == ObexRequestCode::Connect) {
    if (responseCode != ObexResponseCode::Success) {
      LOG("[OBEX] Connect failed");
    } else {
      LOG("[OBEX] Connect ok");

      mRemoteObexVersion = aMessage->mData[3];
      mRemoteConnectionFlags = aMessage->mData[4];
      mRemoteMaxPacketLength = ((aMessage->mData[5] << 8) | aMessage->mData[6]);

      ParseHeaders((uint8_t*)&aMessage->mData[7], packetLength - 7, &headerSet);

      mConnected = true;

      // xxx Eric Temp
      // xxxxxxxxxxxxxxxxxxxxx File Name and Body
      uint8_t tempName[18] = {0x00, 0x74, 0x00, 0x65, 0x00, 0x73, 0x00, 0x74, 0x00, 0x2e, 0x00, 0x74,
                              0x00, 0x78, 0x00, 0x74, 0x00, 0x00};
      uint8_t tempBody[11] = {0x45, 0x72, 0x69, 0x63, 0x20, 0x54, 0x65, 0x73, 0x74, 0x2e, 0x0a};

      SendPutReqeust(tempName, 18, tempBody, 11);
    }
  } else if (sLastCommand == ObexRequestCode::Disconnect) {
    if (responseCode != ObexResponseCode::Success) {
      LOG("[OBEX] Disconnect failed");
    } else {
      LOG("[OBEX] Disconnect ok");

      mConnected = false;
    }
  } else if (sLastCommand == ObexRequestCode::Put) {
    if (responseCode != ObexResponseCode::Continue) {
      LOG("[OBEX] Put failed");
    } else {
      // Put: Do nothing, just reply responsecode
      LOG("[OBEX] Remote device received part of file, keep sending");
    }
  } else if (sLastCommand == ObexRequestCode::PutFinal) {
    if (responseCode != ObexResponseCode::Success) {
      LOG("[OBEX] PutFinal failed");
    } else {
      LOG("[OBEX] File sharing done");

      // Disconnect right after connected
      SendDisconnectReqeust();
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

void
BluetoothOppManager::SendDisconnectReqeust()
{
  // IrOBEX 1.2 3.3.2
  // [opcode:1][length:2][Headers:var]
  uint8_t req[255];
  int currentIndex = 3;

  SetObexPacketInfo(req, ObexRequestCode::Disconnect, currentIndex);
  sLastCommand = ObexRequestCode::Disconnect;

  mozilla::ipc::UnixSocketRawData* s 
    = new mozilla::ipc::UnixSocketRawData(req, currentIndex);
  SendSocketData(s);

  LOG("[Gecko] OPP sent disconnect request!");
}

void
BluetoothOppManager::SendPutReqeust(uint8_t* fileName, int fileNameLength,
                                    uint8_t* fileBody, int fileBodyLength)
{

  // IrOBEX 1.2 3.3.3
  // [opcode:1][length:2][Headers:var]
  if (!mConnected) return;

  uint8_t* req = new uint8_t[mRemoteMaxPacketLength];

  int sentFileBodyLength = 0;
  int index = 3;

  index += AppendHeaderConnectionId(&req[index], sConnectionId);
  index += AppendHeaderName(&req[index], fileName, fileNameLength);
  index += AppendHeaderLength(&req[index], fileBodyLength);

  while (fileBodyLength > sentFileBodyLength) {
    int packetLeftSpace = mRemoteMaxPacketLength - index - 3;

    if (fileBodyLength <= packetLeftSpace) {
      index += AppendHeaderBody(&req[index], &fileBody[sentFileBodyLength], fileBodyLength);
      sentFileBodyLength += fileBodyLength;
    } else {
      index += AppendHeaderBody(&req[index], &fileBody[sentFileBodyLength], packetLeftSpace);
      sentFileBodyLength += packetLeftSpace;
    }

    LOG("Sent file body length: %d", sentFileBodyLength);

    if (sentFileBodyLength >= fileBodyLength) {
      SetObexPacketInfo(req, ObexRequestCode::PutFinal, index);
      sLastCommand = ObexRequestCode::PutFinal;
    } else {
      SetObexPacketInfo(req, ObexRequestCode::Put, index);
      sLastCommand = ObexRequestCode::Put;
    }

    mozilla::ipc::UnixSocketRawData* s = 
      new mozilla::ipc::UnixSocketRawData(req, index);
    SendSocketData(s);

    index = 3;
  }

  delete [] req;
}
