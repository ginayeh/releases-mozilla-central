/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"

#include "BluetoothScoManager.h"

#include "BluetoothReplyRunnable.h"
#include "BluetoothService.h"
#include "BluetoothServiceUuid.h"

#include "AudioManager.h"
#include "mozilla/dom/bluetooth/BluetoothTypes.h"
//#include "nsContentUtils.h"

#undef LOG
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...) __android_log_print(ANDROID_LOG_INFO, "GonkDBus", args);
#else
#define BTDEBUG true
#define LOG(args...) if (BTDEBUG) printf(args);
#endif

USING_BLUETOOTH_NAMESPACE

static BluetoothScoManager* sInstance = nullptr;
static nsCOMPtr<nsIThread> sScoCommandThread;
//static bool sConnected = false;

/*class TestTask : public BluetoothVoidReplyRunnable
{
public:
  TestTask(nsIDOMDOMRequest* aReq)
    : BluetoothVoidReplyRunnable(aReq)
  {
  }

  ~TestTask()
  {
  }

  virtual bool ParseSuccessfulReply(jsval* aValue)
  {
    LOG("[SCO] TestTask::ParseSuccessfulReply");
  }

  void
  ReleaseMembers()
  {
  }

private:
};*/

BluetoothScoManager::BluetoothScoManager()
{
  if (!sScoCommandThread) {
    if (NS_FAILED(NS_NewThread(getter_AddRefs(sScoCommandThread)))) {
      NS_ERROR("Failed to new thread for sScoCommandThread");
    }
  }
  mConnected = false;
}

BluetoothScoManager::~BluetoothScoManager()
{
  // Shut down the command thread if it still exists.
  if (sScoCommandThread) {
    nsCOMPtr<nsIThread> thread;
    sScoCommandThread.swap(thread);
    if (NS_FAILED(thread->Shutdown())) {
      NS_WARNING("Failed to shut down the bluetooth hfpmanager command thread!");
    }
  }
}

//static
BluetoothScoManager*
BluetoothScoManager::Get()
{
  if (sInstance == nullptr) {
    sInstance = new BluetoothScoManager();
  }

  return sInstance;
}

// Virtual function of class SocketConsumer
void
BluetoothScoManager::ReceiveSocketData(mozilla::ipc::UnixSocketRawData* aMessage)
{
  const char* msg = (const char*)aMessage->mData;

  // XXX
}

bool
BluetoothScoManager::Connect(const nsAString& aDeviceObjectPath) //,
//                             nsRunnable* aRunnable)
{
  LOG("[Sco] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());

  BluetoothService* bs = BluetoothService::Get();
  if (!bs) {
    NS_WARNING("BluetoothService not available!");
    return false;
  }

  nsresult rv = bs->GetSocket(aDeviceObjectPath,
                              BluetoothSocketType::SCO,
                              true,
                              false,
                              this);

  return NS_FAILED(rv) ? false : true;
}

bool
BluetoothScoManager::Disconnect()//nsRunnable* aRunnable)
{
  MOZ_ASSERT(NS_IsMainThread());

  nsresult rv = NS_OK;
  CloseSocket();
  return NS_FAILED(rv) ? false : true;
}

bool
BluetoothScoManager::IsConnected()
{
  return mConnected;
}

void
BluetoothScoManager::SetConnected(bool aConnected)
{
  mConnected = aConnected;
}
