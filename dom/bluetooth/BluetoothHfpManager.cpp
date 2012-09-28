/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"

#include "BluetoothHfpManager.h"

#include "BluetoothReplyRunnable.h"
#include "BluetoothScoManager.h"
#include "BluetoothService.h"
#include "BluetoothServiceUuid.h"

#include "mozilla/Services.h"
#include "mozilla/dom/bluetooth/BluetoothTypes.h"
#include "nsContentUtils.h"
#include "nsIDOMDOMRequest.h"
#include "nsIObserverService.h"
#include "nsISystemMessagesInternal.h"
#include "nsIRadioInterfaceLayer.h"

#include "nsVariant.h"

#include <unistd.h> /* usleep() */

#define MOZSETTINGS_CHANGED_ID "mozsettings-changed"
#define AUDIO_VOLUME_MASTER "audio.volume.master"

#undef LOG
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...) __android_log_print(ANDROID_LOG_INFO, "GonkDBus", args);
#else
#define BTDEBUG true
#define LOG(args...) if (BTDEBUG) printf(args);
#endif

USING_BLUETOOTH_NAMESPACE

static nsRefPtr<BluetoothHfpManager> sInstance = nullptr;
static nsCOMPtr<nsIThread> sHfpCommandThread;
static bool sStopSendingRingFlag = true;

static int kRingInterval = 3000000;  //unit: us

NS_IMPL_ISUPPORTS1(BluetoothHfpManager, nsIObserver)

class SendRingIndicatorTask : public nsRunnable
{
public:
  SendRingIndicatorTask()
  {
    LOG("[H] SendRingIndicatorTask");
    MOZ_ASSERT(NS_IsMainThread());
  }

  NS_IMETHOD Run()
  {
    MOZ_ASSERT(!NS_IsMainThread());

    LOG("[H] SendRingIndicatorTask::Run, sStopSendingRingFlag = %d", sStopSendingRingFlag);
    while (!sStopSendingRingFlag) {
      sInstance->SendLine("RING");

      usleep(kRingInterval);
    }

    return NS_OK;
  }
};

void
OpenScoSocket(const nsAString& aDevicePath)
{
  MOZ_ASSERT(NS_IsMainThread());

  BluetoothScoManager* sco = BluetoothScoManager::Get();
  if (!sco) {
    NS_WARNING("BluetoothScoManager is not available!");
    return;
  }

  if (!sco->Connect(aDevicePath)) {
    NS_WARNING("Failed to create a sco socket!");
  }
}

void
CloseScoSocket()
{
  MOZ_ASSERT(NS_IsMainThread());

  BluetoothScoManager* sco = BluetoothScoManager::Get();
  if (!sco) {
    NS_WARNING("BluetoothScoManager is not available!");
    return;
  }
  sco->Disconnect();
}

BluetoothHfpManager::BluetoothHfpManager()
  : mCurrentVgs(-1)
  , mCurrentCallIndex(0)
  , mCurrentCallState(nsIRadioInterfaceLayer::CALL_STATE_DISCONNECTED)
{
  mListener = new BluetoothRilListener();
  if (!mListener->StartListening()) {
    NS_WARNING("Failed to start listening RIL");
  }

  if (!sHfpCommandThread) {
    if (NS_FAILED(NS_NewThread(getter_AddRefs(sHfpCommandThread)))) {
      NS_ERROR("Failed to new thread for sHfpCommandThread");
    }
  }

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();

  if (obs && NS_FAILED(obs->AddObserver(sInstance, MOZSETTINGS_CHANGED_ID, false))) {
    NS_WARNING("Failed to add settings change observer!");
  }
}

BluetoothHfpManager::~BluetoothHfpManager()
{
  mListener = nullptr;
  if (!mListener->StopListening()) {
    NS_WARNING("Failed to stop listening RIL");
  }

  // Shut down the command thread if it still exists.
  if (sHfpCommandThread) {
    nsCOMPtr<nsIThread> thread;
    sHfpCommandThread.swap(thread);
    if (NS_FAILED(thread->Shutdown())) {
      NS_WARNING("Failed to shut down the bluetooth hfpmanager command thread!");
    }
  }
}

//static
BluetoothHfpManager*
BluetoothHfpManager::Get()
{
  MOZ_ASSERT(NS_IsMainThread());

  if (sInstance == nullptr) {
    sInstance = new BluetoothHfpManager();
  }

  return sInstance;
}

nsresult
BluetoothHfpManager::HandleVolumeChanged(const nsAString& aData)
{
  MOZ_ASSERT(NS_IsMainThread());

  // The string that we're interested in will be a JSON string that looks like:
  //  {"key":"volumeup", "value":1.0}
  //  {"key":"volumedown", "value":0.2}

  JSContext* cx = nsContentUtils::GetSafeJSContext();
  if (!cx) {
    return NS_OK;
  }

  JS::Value val;
  if (!JS_ParseJSON(cx, aData.BeginReading(), aData.Length(), &val)) {
    return JS_ReportPendingException(cx) ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
  }

  if (!val.isObject()) {
    return NS_OK;
  }

  JSObject& obj(val.toObject());

  JS::Value key;
  if (!JS_GetProperty(cx, &obj, "key", &key)) {
    MOZ_ASSERT(!JS_IsExceptionPending(cx));
    return NS_ERROR_OUT_OF_MEMORY;
  }

  if (!key.isString()) {
    return NS_OK;
  }

  JSBool match;
  if (!JS_StringEqualsAscii(cx, key.toString(), AUDIO_VOLUME_MASTER, &match)) {
    MOZ_ASSERT(!JS_IsExceptionPending(cx));
    return NS_ERROR_OUT_OF_MEMORY;
  }

  if (!match) {
    return NS_OK;
  }

  JS::Value value;
  if (!JS_GetProperty(cx, &obj, "value", &value)) {
    MOZ_ASSERT(!JS_IsExceptionPending(cx));
    return NS_ERROR_OUT_OF_MEMORY;
  }

  if (!value.isNumber()) {
    return NS_ERROR_UNEXPECTED;
  }

  // AG volume range: [0.0, 1.0]
  float volume = value.toNumber();

  // HS volume range: [0, 15]
  mCurrentVgs = ceil(volume * 15);

  nsDiscriminatedUnion du;
  du.mType = 0;
  du.u.mInt8Value = mCurrentVgs;

  nsCString vgs;
  if (NS_FAILED(nsVariant::ConvertToACString(du, vgs))) {
    NS_WARNING("Failed to convert volume to string");
    return NS_ERROR_FAILURE;
  }

  nsAutoCString newVgs;
  newVgs += "+VGS: ";
  newVgs += vgs;

  SendLine(newVgs.get());

  return NS_OK;
}

nsresult
BluetoothHfpManager::Observe(nsISupports* aSubject,
                             const char* aTopic,
                             const PRUnichar* aData)
{
  if (!strcmp(aTopic, MOZSETTINGS_CHANGED_ID)) {
    return HandleVolumeChanged(nsDependentString(aData));
  } else {
    MOZ_ASSERT(false, "BluetoothHfpManager got unexpected topic!");
  }
  return NS_ERROR_UNEXPECTED;
}

bool
BluetoothHfpManager::BroadcastSystemMessage(const char* aCommand,
                                            const int aCommandLength)
{
  LOG("[H] %s", __FUNCTION__);
  nsString type;
  type.AssignLiteral("bluetooth-dialer-command");

  JSContext* cx = nsContentUtils::GetSafeJSContext();
  NS_ASSERTION(!::JS_IsExceptionPending(cx),
               "Shouldn't get here when an exception is pending!");

  JSAutoRequest jsar(cx);
  JSObject* obj = JS_NewObject(cx, NULL, NULL, NULL);
  if (!obj) {
    NS_WARNING("Failed to new JSObject for system message!");
    return false;
  }

  JSString* JsData = JS_NewStringCopyN(cx, aCommand, aCommandLength);
  if (!JsData) {
    NS_WARNING("JS_NewStringCopyN is out of memory");
    return false;
  }

  jsval v = STRING_TO_JSVAL(JsData);
  if (!JS_SetProperty(cx, obj, "command", &v)) {
    NS_WARNING("Failed to set properties of system message!");
    return false;
  }

  LOG("[H] message is ready!");
  nsCOMPtr<nsISystemMessagesInternal> systemMessenger =
    do_GetService("@mozilla.org/system-message-internal;1");

  if (!systemMessenger) {
    NS_WARNING("Failed to get SystemMessenger service!");
    return false;
  }

  LOG("[H] broadcast message");
  systemMessenger->BroadcastMessage(type, OBJECT_TO_JSVAL(obj));

  return true;
}

// Virtual function of class SocketConsumer
void
BluetoothHfpManager::ReceiveSocketData(mozilla::ipc::UnixSocketRawData* aMessage)
{
  const char* msg = (const char*)aMessage->mData;

  // For more information, please refer to 4.34.1 "Bluetooth Defined AT
  // Capabilities" in Bluetooth hands-free profile 1.6
  if (!strncmp(msg, "AT+BRSF=", 8)) {
    SendLine("+BRSF: 23");
    SendLine("OK");
  } else if (!strncmp(msg, "AT+CIND=?", 9)) {
    nsAutoCString cindRange;

    cindRange += "+CIND: ";
    cindRange += "(\"battchg\",(0-5)),";
    cindRange += "(\"signal\",(0-5)),";
    cindRange += "(\"service\",(0,1)),";
    cindRange += "(\"call\",(0,1)),";
    cindRange += "(\"callsetup\",(0-3)),";
    cindRange += "(\"callheld\",(0-2)),";
    cindRange += "(\"roam\",(0,1))";

    SendLine(cindRange.get());
    SendLine("OK");
  } else if (!strncmp(msg, "AT+CIND", 7)) {
    // TODO(Eric)
    // This value reflects current status of telephony, roaming, battery ...,
    // so obviously fixed value must be wrong here. Need a patch for this.
    SendLine("+CIND: 5,5,1,0,0,0,0");
    SendLine("OK");
  } else if (!strncmp(msg, "AT+CMER=", 8)) {
    SendLine("OK");
  } else if (!strncmp(msg, "AT+CHLD=?", 9)) {
    SendLine("+CHLD: (0,1,2,3)");
    SendLine("OK");
  } else if (!strncmp(msg, "AT+CHLD=", 8)) {
    SendLine("OK");
  } else if (!strncmp(msg, "AT+VGS=", 7)) {
    // VGS range: [0, 15]
    int newVgs = msg[7] - '0';

    if (strlen(msg) > 8) {
      newVgs *= 10;
      newVgs += (msg[8] - '0');
    }

#ifdef DEBUG
    NS_ASSERTION(newVgs >= 0 && newVgs <= 15, "Received invalid VGS value");
#endif

    // Currently, we send volume up/down commands to represent that
    // volume has been changed by Bluetooth headset, and that will affect
    // the main stream volume of our device. In the future, we may want to
    // be able to set volume by stream.
    nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
    if (newVgs > mCurrentVgs) {
      os->NotifyObservers(nullptr, "bluetooth-volume-up", nullptr);
    } else if (newVgs < mCurrentVgs) {
      os->NotifyObservers(nullptr, "bluetooth-volume-down", nullptr);
    }

    mCurrentVgs = newVgs;

    SendLine("OK");
  } else if (!strncmp(msg, "AT+BLDN", 7)) {
    LOG("[H] Receive 'AT+BLDN'");
    if (!BroadcastSystemMessage("BLDN", 4)) {
      NS_WARNING("Failed to broadcast system message to dialer");
      return;
    }
    SendLine("OK");
  } else if (!strncmp(msg, "ATA", 3)) {
    LOG("[H] Receive 'ATA'");
    if (!BroadcastSystemMessage("ATA", 3)) {
      NS_WARNING("Failed to broadcast system message to dialer");
      return;
    }
    SendLine("OK");
  } else if (!strncmp(msg, "AT+CHUP", 7)) {
    LOG("[H] Receive 'AT+CHUP'");
    if (!BroadcastSystemMessage("CHUP", 4)) {
      NS_WARNING("Failed to broadcast system message to dialer");
      return;
    }
    SendLine("OK");
  } else {
#ifdef DEBUG
    nsCString warningMsg;
    warningMsg.AssignLiteral("Not handling HFP message, reply ok: ");
    warningMsg.Append(msg);
    NS_WARNING(warningMsg.get());
#endif
    SendLine("OK");
  }
}

bool
BluetoothHfpManager::Connect(const nsAString& aDeviceObjectPath,
                             BluetoothReplyRunnable* aRunnable)
{
  LOG("[H] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());

  mDevicePath = aDeviceObjectPath;

  BluetoothService* bs = BluetoothService::Get();
  if (!bs) {
    NS_WARNING("BluetoothService not available!");
    return false;
  }

  nsString serviceUuidStr =
    NS_ConvertUTF8toUTF16(mozilla::dom::bluetooth::BluetoothServiceUuidStr::Handsfree);

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
BluetoothHfpManager::Disconnect(BluetoothReplyRunnable* aRunnable)
{
  MOZ_ASSERT(NS_IsMainThread());

  // BluetoothService* bs = BluetoothService::Get();
  // if (!bs) {
  //   NS_WARNING("BluetoothService not available!");
  //   return false;
  // }

  // nsRefPtr<BluetoothReplyRunnable> runnable = aRunnable;

  nsresult rv = NS_OK;
  CloseSocket();
  return NS_FAILED(rv) ? false : true;
}

void
BluetoothHfpManager::SendLine(const char* aMessage)
{
  LOG("[H] %s - %s", __FUNCTION__, aMessage);
  const char* kHfpCrlf = "\xd\xa";
  nsAutoCString msg;

  msg += kHfpCrlf;
  msg += aMessage;
  msg += kHfpCrlf;

  SendSocketData(msg);
}

/*
 * CallStateChanged will be called whenever call status is changed, and it 
 * also means we need to notify HS about the change. For more information, 
 * please refer to 4.13 ~ 4.15 in Bluetooth hands-free profile 1.6.
 */
void
BluetoothHfpManager::CallStateChanged(int aCallIndex, int aCallState,
                                      const char* aNumber, bool aIsActive)
{
  LOG("[H] %s", __FUNCTION__);
  nsRefPtr<nsRunnable> sendRingTask;

  switch (aCallState) {
    case nsIRadioInterfaceLayer::CALL_STATE_INCOMING:
      LOG("[H] CALL_STATE_INCOMING");
      // Send "CallSetup = 1"
      SendLine("+CIEV: 5,1");

      // Start sending RING indicator to HF
      sStopSendingRingFlag = false;
      sendRingTask = new SendRingIndicatorTask();

      if (NS_FAILED(sHfpCommandThread->Dispatch(sendRingTask, NS_DISPATCH_NORMAL))) {
        NS_WARNING("Cannot dispatch ring task!");
        return;
      };
      break;
    case nsIRadioInterfaceLayer::CALL_STATE_DIALING:
      LOG("[H] CALL_STATE_DIALING");
      // Send "CallSetup = 2"
      SendLine("+CIEV: 5,2");
      break;
    case nsIRadioInterfaceLayer::CALL_STATE_ALERTING:
      LOG("[H] CALL_STATE_ALERTING");
      // Send "CallSetup = 3"
      if (mCurrentCallIndex == nsIRadioInterfaceLayer::CALL_STATE_DIALING) {
        SendLine("+CIEV: 5,3");
      } else {
#ifdef DEBUG
        LOG("%s: Impossible state changed from %d to %d",
            __FUNCTION__, mCurrentCallState, aCallState);
#endif
      }
      break;
    case nsIRadioInterfaceLayer::CALL_STATE_CONNECTED:
      LOG("[H] CALL_STATE_CONNECTED");
      switch (mCurrentCallState) {
        case nsIRadioInterfaceLayer::CALL_STATE_INCOMING:
          sStopSendingRingFlag = true;
          // Continue executing, no break
        case nsIRadioInterfaceLayer::CALL_STATE_DIALING:
          // Send "Call = 1, CallSetup = 0"
          SendLine("+CIEV: 4,1");
          SendLine("+CIEV: 5,0");
          break;
        default:
#ifdef DEBUG
          LOG("%s: Impossible state changed from %d to %d",
               __FUNCTION__, mCurrentCallState, aCallState);
#endif
          break;
      }
      OpenScoSocket(mDevicePath);
      break;

    case nsIRadioInterfaceLayer::CALL_STATE_DISCONNECTED:
      LOG("[H] CALL_STATE_DISCONNECTED");
      switch (mCurrentCallState) {
        case nsIRadioInterfaceLayer::CALL_STATE_INCOMING:
          sStopSendingRingFlag = true;
          // Continue executing, no break
        case nsIRadioInterfaceLayer::CALL_STATE_DIALING:
        case nsIRadioInterfaceLayer::CALL_STATE_ALERTING:
          // Send "CallSetup = 0"
          SendLine("+CIEV: 5,0");
          break;
        case nsIRadioInterfaceLayer::CALL_STATE_CONNECTED:
          // Send "Call = 0"
          SendLine("+CIEV: 4,0");
          break;
        default:
#ifdef DEBUG
          LOG("%s: Impossible state changed from %d to %d",
               __FUNCTION__, mCurrentCallState, aCallState);
#endif
          break;
      }
      CloseScoSocket();
      break;

    default:
//#ifdef DEBUG
      LOG("%s: current state:%d, not handling state: %d",
          __FUNCTION__, mCurrentCallState, aCallState);
//#endif
      break;
  }

  mCurrentCallIndex = aCallIndex;
  mCurrentCallState = aCallState;
}
