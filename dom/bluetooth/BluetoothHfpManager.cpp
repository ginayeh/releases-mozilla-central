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
#include "BluetoothUtils.h"

#include "mozilla/dom/bluetooth/BluetoothTypes.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPtr.h"
#include "nsContentUtils.h"
#include "nsIAudioManager.h"
#include "nsIObserverService.h"
#include "nsIRadioInterfaceLayer.h"
#include "nsVariant.h"

#include <unistd.h> /* usleep() */

#define MOZSETTINGS_CHANGED_ID "mozsettings-changed"
#define BLUETOOTH_SCO_STATUS_CHANGED "bluetooth-sco-status-changed"
#define AUDIO_VOLUME_MASTER "audio.volume.master"
#define HANDSFREE_UUID mozilla::dom::bluetooth::BluetoothServiceUuidStr::Handsfree
#define HEADSET_UUID mozilla::dom::bluetooth::BluetoothServiceUuidStr::Headset

#undef LOG
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "GonkDBus", args);
#else
#define BTDEBUG true
#define LOG(args...) if (BTDEBUG) printf(args);
#endif

#undef LOGV
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOGV(args...)  __android_log_print(ANDROID_LOG_INFO, "GonkDBusV", args);
#else
#define BTDEBUG true
#define LOG(args...) if (BTDEBUG) printf(args);
#endif

using namespace mozilla;
using namespace mozilla::ipc;
USING_BLUETOOTH_NAMESPACE

/* CallState for sCINDItems[CINDType::CALL].value
 * - NO_CALL: there are no calls in progress
 * - IN_PROGRESS: at least one call is in progress
 */
enum CallState {
  NO_CALL,
  IN_PROGRESS
};

/* CallSetupState for sCINDItems[CINDType::CALLSETUP].value
 * - NO_CALLSETUP: not currently in call set up
 * - INCOMING: an incoming call process ongoing
 * - OUTGOING: an outgoing call set up is ongoing
 * - OUTGOING_ALERTING: remote party being alerted in an outgoing call
 */
enum CallSetupState {
  NO_CALLSETUP,
  INCOMING,
  OUTGOING,
  OUTGOING_ALERTING
};

/* CallHeldState for sCINDItems[CINDType::CALLHELD].value
 * - NO_CALLHELD: no calls held
 * - ONHOLD_ACTIVE: both an active and a held call
 * - ONHOLD_NOACTIVE: call on hold, no active call
 */
enum CallHeldState {
  NO_CALLHELD,
  ONHOLD_ACTIVE,
  ONHOLD_NOACTIVE
};

typedef struct {
  const char* name;
  const char* range;
  int value;
} CINDItem;

enum CINDType {
  BATTCHG = 1,
  SIGNAL,
  SERVICE,
  CALL,
  CALLSETUP,
  CALLHELD,
  ROAM,
};

static CINDItem sCINDItems[] = {
  {},
  {"battchg", "0-5", 5},
  {"signal", "0-5", 5},
  {"service", "0,1", 1},
  {"call", "0,1", CallState::NO_CALL},
  {"callsetup", "0-3", CallSetupState::NO_CALLSETUP},
  {"callheld", "0-2", CallHeldState::NO_CALLHELD},
  {"roam", "0,1", 0}
};

class mozilla::dom::bluetooth::BluetoothHfpManagerObserver : public nsIObserver
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  BluetoothHfpManagerObserver()
  {
  }

  bool Init()
  {
    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    MOZ_ASSERT(obs);
    if (NS_FAILED(obs->AddObserver(this, MOZSETTINGS_CHANGED_ID, false))) {
      NS_WARNING("Failed to add settings change observer!");
      return false;
    }

    if (NS_FAILED(obs->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID, false))) {
      NS_WARNING("Failed to add shutdown observer!");
      return false;
    }

    return true;
  }

  bool Shutdown()
  {
    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    if (!obs ||
        (NS_FAILED(obs->RemoveObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID)) ||
         NS_FAILED(obs->RemoveObserver(this, MOZSETTINGS_CHANGED_ID)))) {
      NS_WARNING("Can't unregister observers, or already unregistered!");
      return false;
    }
    return true;
  }

  ~BluetoothHfpManagerObserver()
  {
    Shutdown();
  }
};

namespace {
  StaticRefPtr<BluetoothHfpManager> gBluetoothHfpManager;
  StaticRefPtr<BluetoothHfpManagerObserver> sHfpObserver;
  bool gInShutdown = false;
  static nsCOMPtr<nsIThread> sHfpCommandThread;
  static bool sStopSendingRingFlag = true;

  static int kRingInterval = 3000000;  //unit: us
} // anonymous namespace

NS_IMPL_ISUPPORTS1(BluetoothHfpManagerObserver, nsIObserver)

NS_IMETHODIMP
BluetoothHfpManagerObserver::Observe(nsISupports* aSubject,
                                     const char* aTopic,
                                     const PRUnichar* aData)
{
  MOZ_ASSERT(gBluetoothHfpManager);

  if (!strcmp(aTopic, MOZSETTINGS_CHANGED_ID)) {
    return gBluetoothHfpManager->HandleVolumeChanged(nsDependentString(aData));
  } else if (!strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID)) {
    return gBluetoothHfpManager->HandleShutdown();
  }

  MOZ_ASSERT(false, "BluetoothHfpManager got unexpected topic!");
  return NS_ERROR_UNEXPECTED;
}

class SendRingIndicatorTask : public nsRunnable
{
public:
  SendRingIndicatorTask()
  {
    MOZ_ASSERT(NS_IsMainThread());
  }

  NS_IMETHOD Run()
  {
    MOZ_ASSERT(!NS_IsMainThread());
    LOG("[Hfp] SendRingIndicatorTask::Run");

    while (!sStopSendingRingFlag) {
      gBluetoothHfpManager->SendLine("RING");

      usleep(kRingInterval);
    }

    return NS_OK;
  }
};

void
OpenScoSocket(const nsAString& aDevicePath)
{
  MOZ_ASSERT(NS_IsMainThread());

  LOG("[Hfp] %s", __FUNCTION__);
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
  LOG("[Hfp] %s", __FUNCTION__);

  nsCOMPtr<nsIAudioManager> am = do_GetService("@mozilla.org/telephony/audiomanager;1");
  if (!am) {
    NS_WARNING("Failed to get AudioManager Service!");
    return;
  }
  am->SetForceForUse(am->USE_COMMUNICATION, am->FORCE_NONE);

  BluetoothScoManager* sco = BluetoothScoManager::Get();
  if (!sco) {
    NS_WARNING("BluetoothScoManager is not available!");
    return;
  }

  if (sco->GetConnected()) {
    nsCOMPtr<nsIObserverService> obs = do_GetService("@mozilla.org/observer-service;1");
    if (obs) {
      if (NS_FAILED(obs->NotifyObservers(nullptr, BLUETOOTH_SCO_STATUS_CHANGED, nullptr))) {
        NS_WARNING("Failed to notify bluetooth-sco-status-changed observsers!");
        return;
      }
    }
    sco->Disconnect();
  }
}

BluetoothHfpManager::BluetoothHfpManager()
  : mCurrentVgs(-1)
  , mCurrentCallIndex(0)
  , mCurrentCallState(nsIRadioInterfaceLayer::CALL_STATE_DISCONNECTED)
  , mConnected(false)
{
  Listen();
  sCINDItems[CINDType::CALL].value = CallState::NO_CALL;
  sCINDItems[CINDType::CALLSETUP].value = CallSetupState::NO_CALLSETUP;
  sCINDItems[CINDType::CALLHELD].value = CallHeldState::NO_CALLHELD;
}

bool
BluetoothHfpManager::Init()
{
  sHfpObserver = new BluetoothHfpManagerObserver();
  if (!sHfpObserver->Init()) {
    NS_WARNING("Cannot set up Hfp Observers!");
  }

  mListener = new BluetoothRilListener();
  if (!mListener->StartListening()) {
    NS_WARNING("Failed to start listening RIL");
    return false;
  }

  if (!sHfpCommandThread) {
    if (NS_FAILED(NS_NewThread(getter_AddRefs(sHfpCommandThread)))) {
      NS_ERROR("Failed to new thread for sHfpCommandThread");
      return false;
    }
  }

  return true;
}

BluetoothHfpManager::~BluetoothHfpManager()
{
  Cleanup();
}

void
BluetoothHfpManager::Cleanup()
{
  LOGV("[Hfp] %s", __FUNCTION__);
  if (!mListener->StopListening()) {
    NS_WARNING("Failed to stop listening RIL");
  }
  mListener = nullptr;

  // Shut down the command thread if it still exists.
  if (sHfpCommandThread) {
    nsCOMPtr<nsIThread> thread;
    sHfpCommandThread.swap(thread);
    if (NS_FAILED(thread->Shutdown())) {
      NS_WARNING("Failed to shut down the bluetooth hfpmanager command thread!");
    }
  }

  sHfpObserver->Shutdown();
  sHfpObserver = nullptr;
}

//static
BluetoothHfpManager*
BluetoothHfpManager::Get()
{
  LOGV("[Hfp] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());

  // If we already exist, exit early
  if (gBluetoothHfpManager) {
    return gBluetoothHfpManager;
  }

  // If we're in shutdown, don't create a new instance
  if (gInShutdown) {
    NS_WARNING("BluetoothHfpManager can't be created during shutdown");
    return nullptr;
  }

  // Create new instance, register, return
  nsRefPtr<BluetoothHfpManager> manager = new BluetoothHfpManager();
  NS_ENSURE_TRUE(manager, nullptr);

  if (!manager->Init()) {
    return nullptr;
  }

  gBluetoothHfpManager = manager;
  return gBluetoothHfpManager;
}

void
BluetoothHfpManager::NotifySettings()
{
  nsString type, name;
  BluetoothValue v;
  InfallibleTArray<BluetoothNamedValue> parameters;
  type.AssignLiteral("bluetooth-hfp-status-changed");

  name.AssignLiteral("connected");
  v = mConnected;
  parameters.AppendElement(BluetoothNamedValue(name, v));

  name.AssignLiteral("address");
  v = GetAddressFromObjectPath(mDevicePath);
  parameters.AppendElement(BluetoothNamedValue(name, v));

  if (!BroadcastSystemMessage(type, parameters)) {
    NS_WARNING("Failed to broadcast system message to dialer");
    return;
  }
}

void
BluetoothHfpManager::NotifyDialer(const nsAString& aCommand)
{
  nsString type, name, command;
  command = aCommand;
  InfallibleTArray<BluetoothNamedValue> parameters;
  type.AssignLiteral("bluetooth-dialer-command");

  BluetoothValue v(command);
  parameters.AppendElement(BluetoothNamedValue(type, v));

  if (!BroadcastSystemMessage(type, parameters)) {
    NS_WARNING("Failed to broadcast system message to dialer");
    return;
  }
}

nsresult
BluetoothHfpManager::HandleVolumeChanged(const nsAString& aData)
{
  MOZ_ASSERT(NS_IsMainThread());

  if (!mConnected) {
    return NS_OK;
  }

  LOG("[Hfp] %s", __FUNCTION__);

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

/*  nsDiscriminatedUnion du;
  du.mType = 0;
  du.u.mInt8Value = mCurrentVgs;

  nsCString vgs;
  if (NS_FAILED(nsVariant::ConvertToACString(du, vgs))) {
    NS_WARNING("Failed to convert volume to string");
    return NS_ERROR_FAILURE;
  }*/

  SendCommand("+VGS: ", mCurrentVgs);
/*  nsAutoCString newVgs;
  newVgs += "+VGS: ";
  newVgs += vgs;

  SendLine(newVgs.get());*/

  return NS_OK;
}

nsresult
BluetoothHfpManager::HandleShutdown()
{
  MOZ_ASSERT(NS_IsMainThread());
  gInShutdown = true;
  mConnected = false;
  CloseSocket();
  gBluetoothHfpManager = nullptr;
  return NS_OK;
}

/*bool
AppendIntegerToString(nsAutoCString& aString, int aValue)
{
  nsDiscriminatedUnion du;
  du.mType = nsIDataType::VTYPE_INT8;
  du.u.mInt8Value = aValue;

  nsCString temp;
  if (NS_FAILED(nsVariant::ConvertToACString(du, temp))) {
    NS_WARNING("Failed to convert sCINDItems[CINDType::CALL].value/sCINDItems[CINDType::CALLSETUP].value/sCINDItems[CINDType::CALLHELD].value to string");
    return false;
  }
  aString += ",";
  aString += temp;
  return true;
}*/

// Virtual function of class SocketConsumer
void
BluetoothHfpManager::ReceiveSocketData(UnixSocketRawData* aMessage)
{
  LOG("[Hfp] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());

  const char* msg = (const char*)aMessage->mData;

  // For more information, please refer to 4.34.1 "Bluetooth Defined AT
  // Capabilities" in Bluetooth hands-free profile 1.6
  if (!strncmp(msg, "AT+BRSF=", 8)) {
//    SendLine("+BRSF: 23");
    SendCommand("+BRSF: ", 23);
    SendLine("OK");
  } else if (!strncmp(msg, "AT+CIND=?", 9)) {
/*    nsAutoCString cindRange;

    cindRange += "+CIND: ";
    cindRange += "(\"battchg\",(0-5)),";
    cindRange += "(\"signal\",(0-5)),";
    cindRange += "(\"service\",(0,1)),";
    cindRange += "(\"call\",(0,1)),";
    cindRange += "(\"callsetup\",(0-3)),";
    cindRange += "(\"callheld\",(0-2)),";
    cindRange += "(\"roam\",(0,1))";

    SendLine(cindRange.get());*/
    SendCommand("+CIND: ", 0);
    SendLine("OK");
  } else if (!strncmp(msg, "AT+CIND?", 8)) {
/*    nsAutoCString cind;
    cind += "+CIND: 5,5,1";

    if (!AppendIntegerToString(cind, sCINDItems[CINDType::CALL].value) ||
        !AppendIntegerToString(cind, sCINDItems[CINDType::CALLSETUP].value) ||
        !AppendIntegerToString(cind, sCINDItems[CINDType::CALLHELD].value)) {
//    if (!AppendIntegerToString(cind, sCINDItems[CINDType::CALL].value) ||
//        !AppendIntegerToString(cind, sCINDItems[CINDType::CALLSETUP].value) ||
//        !AppendIntegerToString(cind, sCINDItems[CINDType::CALLHELD].value)) {
      NS_WARNING("Failed to convert sCINDItems[CINDType::CALL].value/sCINDItems[CINDType::CALLSETUP].value/sCINDItems[CINDType::CALLHELD].value to string");
    } 
    cind += ",0";

    SendLine(cind.get());*/
//    LOG("[Hfp] command '%s'", cind);
    SendCommand("+CIND: ", 1);
    SendLine("OK");
  } else if (!strncmp(msg, "AT+CMER=", 8)) {
    // SLC establishment
    SendLine("OK");
  } else if (!strncmp(msg, "AT+CHLD=?", 9)) {
    SendLine("+CHLD: (0,1,2,3)");
    SendLine("OK");
  } else if (!strncmp(msg, "AT+CHLD=", 8)) {
    // FIXME:
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
      os->NotifyObservers(nullptr, "bluetooth-volume-change", NS_LITERAL_STRING("up").get());
    } else if (newVgs < mCurrentVgs) {
      os->NotifyObservers(nullptr, "bluetooth-volume-change", NS_LITERAL_STRING("down").get());
    }

    mCurrentVgs = newVgs;

    SendLine("OK");
  } else if (!strncmp(msg, "AT+BLDN", 7)) {
    NotifyDialer(NS_LITERAL_STRING("BLDN"));
    SendLine("OK");
  } else if (!strncmp(msg, "ATA", 3)) {
    NotifyDialer(NS_LITERAL_STRING("ATA"));
    SendLine("OK");
  } else if (!strncmp(msg, "AT+CHUP", 7)) {
    NotifyDialer(NS_LITERAL_STRING("CHUP"));
    SendLine("OK");
  } else if (!strncmp(msg, "AT+CKPD", 7)) {
    // For Headset
    switch (mCurrentCallState) {
      case nsIRadioInterfaceLayer::CALL_STATE_INCOMING:
        NotifyDialer(NS_LITERAL_STRING("ATA"));
        break;
      case nsIRadioInterfaceLayer::CALL_STATE_CONNECTED:
      case nsIRadioInterfaceLayer::CALL_STATE_DIALING:
      case nsIRadioInterfaceLayer::CALL_STATE_ALERTING:
        NotifyDialer(NS_LITERAL_STRING("CHUP"));
        break;
      case nsIRadioInterfaceLayer::CALL_STATE_DISCONNECTED:
        NotifyDialer(NS_LITERAL_STRING("BLDN"));
        break;
      default:
#ifdef DEBUG
        NS_WARNING("Not handling state changed");
#endif
        break;
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
                             const bool aIsHandsfree,
                             BluetoothReplyRunnable* aRunnable)
{
  MOZ_ASSERT(NS_IsMainThread());
  if (mConnected) {
    NS_WARNING("BluetoothHfpManager has connected to a headset/handsfree!");
    return false;
  }
  LOG("[Hfp] %s", __FUNCTION__);

  if (gInShutdown) {
    MOZ_ASSERT(false, "Connect called while in shutdown!");
    return false;
  }

  CloseSocket();

  BluetoothService* bs = BluetoothService::Get();
  if (!bs) {
    NS_WARNING("BluetoothService not available!");
    return false;
  }
  mDevicePath = aDeviceObjectPath;

  nsString serviceUuidStr;
  if (aIsHandsfree) {
    serviceUuidStr = NS_ConvertUTF8toUTF16(HANDSFREE_UUID);
  } else {
    serviceUuidStr = NS_ConvertUTF8toUTF16(HEADSET_UUID);
  }

  nsCOMPtr<nsIRILContentHelper> ril =
    do_GetService("@mozilla.org/ril/content-helper;1");
  NS_ENSURE_TRUE(ril, false);
  ril->EnumerateCalls(mListener->GetCallback());

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
BluetoothHfpManager::Listen()
{
  LOG("[Hfp] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());

  if (gInShutdown) {
    MOZ_ASSERT(false, "Listen called while in shutdown!");
    return false;
  }

  mConnected = false;
  CloseSocket();

  BluetoothService* bs = BluetoothService::Get();
  if (!bs) {
    NS_WARNING("BluetoothService not available!");
    return false;
  }

  nsresult rv = bs->ListenSocketViaService(BluetoothReservedChannels::HANDSFREE_AG,
                                           BluetoothSocketType::RFCOMM,
                                           true,
                                           false,
                                           this);
  return NS_FAILED(rv) ? false : true;
}

void
BluetoothHfpManager::Disconnect()
{
  LOG("[Hfp] %s", __FUNCTION__);

  // TODO: move these to OnDisconnectSuccess()
  CloseSocket();
  mConnected = false;
  NotifySettings();
  sCINDItems[CINDType::CALL].value = CallState::NO_CALL;
  sCINDItems[CINDType::CALLSETUP].value = CallSetupState::NO_CALLSETUP;
  sCINDItems[CINDType::CALLHELD].value = CallHeldState::NO_CALLHELD;
}

bool
BluetoothHfpManager::SendLine(const char* aMessage)
{
  LOG("[Hfp] sending '%s'", aMessage);
  const char* kHfpCrlf = "\xd\xa";
  nsAutoCString msg;

  msg += kHfpCrlf;
  msg += aMessage;
  msg += kHfpCrlf;

  return SendSocketData(msg);
}

bool
BluetoothHfpManager::SendCommand(const char* aCommand, const int aValue)
{
  nsAutoCString message;
  int value = aValue;
  message += aCommand;

  if (!strcmp(aCommand, "+CIEV: ")) {
    message.AppendInt(aValue);
    message += ",";
    switch (aValue) {
      case CINDType::CALL:
        message.AppendInt(sCINDItems[CINDType::CALL].value);
        break;
      case CINDType::CALLSETUP:
        message.AppendInt(sCINDItems[CINDType::CALLSETUP].value);
        break;
      case CINDType::CALLHELD:
			  message.AppendInt(sCINDItems[CINDType::CALLHELD].value);
        break;
      default:
#ifdef DEBUG
        NS_WARNING("unexpected CINDType for CIEV command");
#endif        
        return false;
    } 
  } else if (!strcmp(aCommand, "+CIND: ")) {
    if (aValue) {
      for (uint8_t i = 1; i < ArrayLength(sCINDItems); i++) {
        message += "(\"";
        message += sCINDItems[i].name;
        message += "\",(";
        message += sCINDItems[i].range;
        message += ")";
        if (i == (ArrayLength(sCINDItems) - 1)) {
          message +=")";
          break;
        }
        message += "),";
      }
    } else {
      for (uint8_t i = 1; i < ArrayLength(sCINDItems); i++) {
        message.AppendInt(sCINDItems[i].value);
        if (i == (ArrayLength(sCINDItems) - 1)) {
          break;
        }
        message += ",";
      }
    }
  } else {
//  AppendIntegerToString(message, value);
    message.AppendInt(value);
  }

  return SendLine(message.get());
}

/*
 * EnumerateCallState will be called for each call
 */
void
BluetoothHfpManager::EnumerateCallState(int aCallIndex, int aCallState,
                                        const char* aNumber, bool aIsActive)
{
  if (sCINDItems[CINDType::CALLHELD].value == CallHeldState::ONHOLD_NOACTIVE && aIsActive) {
    sCINDItems[CINDType::CALLHELD].value = CallHeldState::ONHOLD_ACTIVE;
  }

  if (sCINDItems[CINDType::CALL].value || sCINDItems[CINDType::CALLSETUP].value) {
    return;
  }

  switch (aCallState) {
    case nsIRadioInterfaceLayer::CALL_STATE_ALERTING:
      sCINDItems[CINDType::CALL].value = CallState::IN_PROGRESS;
      sCINDItems[CINDType::CALLSETUP].value = CallSetupState::OUTGOING_ALERTING;
      break;
    case nsIRadioInterfaceLayer::CALL_STATE_INCOMING:
      sCINDItems[CINDType::CALL].value = CallState::IN_PROGRESS;
      sCINDItems[CINDType::CALLSETUP].value = CallSetupState::INCOMING;
      break;
    case nsIRadioInterfaceLayer::CALL_STATE_CONNECTING:
    case nsIRadioInterfaceLayer::CALL_STATE_CONNECTED:
    case nsIRadioInterfaceLayer::CALL_STATE_DISCONNECTING:
    case nsIRadioInterfaceLayer::CALL_STATE_DISCONNECTED:
    case nsIRadioInterfaceLayer::CALL_STATE_BUSY:
      sCINDItems[CINDType::CALL].value = CallState::IN_PROGRESS;
      sCINDItems[CINDType::CALLSETUP].value = CallSetupState::OUTGOING;
      break;
    case nsIRadioInterfaceLayer::CALL_STATE_HOLDING:
    case nsIRadioInterfaceLayer::CALL_STATE_HELD:
    case nsIRadioInterfaceLayer::CALL_STATE_RESUMING:
      sCINDItems[CINDType::CALL].value = CallState::IN_PROGRESS;
      sCINDItems[CINDType::CALLHELD].value = CallHeldState::ONHOLD_NOACTIVE;
    default:
      sCINDItems[CINDType::CALL].value = CallState::NO_CALL;
      sCINDItems[CINDType::CALLSETUP].value = CallSetupState::NO_CALLSETUP;
  }
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
  nsRefPtr<nsRunnable> sendRingTask;
  if (!mConnected) {
    return;
  }
	LOG("[Hfp] %s", __FUNCTION__);

  switch (aCallState) {
    case nsIRadioInterfaceLayer::CALL_STATE_INCOMING:
      // Send "CallSetup = 1"
      sCINDItems[CINDType::CALLSETUP].value = CallSetupState::INCOMING;
      SendCommand("+CIEV: ", CINDType::CALLSETUP);
//    SendLine("+CIEV: 5,1");

      // Start sending RING indicator to HF
      sStopSendingRingFlag = false;
      sendRingTask = new SendRingIndicatorTask();

      if (NS_FAILED(sHfpCommandThread->Dispatch(sendRingTask, NS_DISPATCH_NORMAL))) {
        NS_WARNING("Cannot dispatch ring task!");
        return;
      };
      break;
    case nsIRadioInterfaceLayer::CALL_STATE_DIALING:
      // Send "CallSetup = 2"
      sCINDItems[CINDType::CALLSETUP].value = CallSetupState::OUTGOING;
      SendCommand("+CIEV: ", CINDType::CALLSETUP);
//      SendLine("+CIEV: 5,2");
      if (mConnected) {
        OpenScoSocket(mDevicePath);
      }
      break;
    case nsIRadioInterfaceLayer::CALL_STATE_ALERTING:
      // Send "CallSetup = 3"
      if (mCurrentCallIndex == nsIRadioInterfaceLayer::CALL_STATE_DIALING) {
        sCINDItems[CINDType::CALLSETUP].value = CallSetupState::OUTGOING_ALERTING;
        SendCommand("+CIEV: ", CINDType::CALLSETUP);
//        SendLine("+CIEV: 5,3");
      } else {
#ifdef DEBUG
        NS_WARNING("Not handling state changed");
#endif
      }
      break;
    case nsIRadioInterfaceLayer::CALL_STATE_CONNECTED:
      switch (mCurrentCallState) {
        case nsIRadioInterfaceLayer::CALL_STATE_INCOMING:
          sStopSendingRingFlag = true;
          // Continue executing, no break
        case nsIRadioInterfaceLayer::CALL_STATE_DIALING:
          // Send "Call = 1, CallSetup = 0"
          sCINDItems[CINDType::CALL].value = CallState::IN_PROGRESS;
          SendCommand("+CIEV: ", CINDType::CALL);
//          SendLine("+CIEV: 4,1");
          sCINDItems[CINDType::CALLSETUP].value = CallSetupState::NO_CALLSETUP;
          SendCommand("+CIEV: ", CINDType::CALLSETUP);
//          SendLine("+CIEV: 5,0");
          break;
        default:
#ifdef DEBUG
          NS_WARNING("Not handling state changed");
#endif
          break;
      }
      OpenScoSocket(mDevicePath);
      break;
    case nsIRadioInterfaceLayer::CALL_STATE_DISCONNECTED:
      switch (mCurrentCallState) {
        case nsIRadioInterfaceLayer::CALL_STATE_INCOMING:
          sStopSendingRingFlag = true;
          // Continue executing, no break
        case nsIRadioInterfaceLayer::CALL_STATE_DIALING:
        case nsIRadioInterfaceLayer::CALL_STATE_ALERTING:
          // Send "CallSetup = 0"
          sCINDItems[CINDType::CALLSETUP].value = CallSetupState::NO_CALLSETUP;
          SendCommand("+CIEV: ", CINDType::CALLSETUP);
//          SendLine("+CIEV: 5,0");
          break;
        case nsIRadioInterfaceLayer::CALL_STATE_CONNECTED:
          // Send "Call = 0"
          sCINDItems[CINDType::CALL].value = CallState::NO_CALL;
          SendCommand("+CIEV: ", CINDType::CALL);
//          SendLine("+CIEV: 4,0");
          break;
        default:
#ifdef DEBUG
          NS_WARNING("Not handling state changed");
#endif
          break;
      }
      if (mConnected) {
        CloseScoSocket();
      }
      break;

    default:
#ifdef DEBUG
      NS_WARNING("Not handling state changed");
#endif
      break;
  }

  mCurrentCallIndex = aCallIndex;
  mCurrentCallState = aCallState;
}

void
BluetoothHfpManager::OnConnectSuccess()
{
  MOZ_ASSERT(NS_IsMainThread());
  LOG("[Hfp] %s", __FUNCTION__);

  mConnected = true;
  NotifySettings();

  nsCOMPtr<nsIAudioManager> am = do_GetService("@mozilla.org/telephony/audiomanager;1");
  if (!am) {
    NS_WARNING("Failed to get AudioManager service!");
  }
  am->SetForceForUse(am->USE_COMMUNICATION, am->FORCE_BT_SCO);

  nsCOMPtr<nsIObserverService> obs = do_GetService("@mozilla.org/observer-service;1");
  if (obs) {
    if (NS_FAILED(obs->NotifyObservers(nullptr, BLUETOOTH_SCO_STATUS_CHANGED, mDevicePath.get()))) {
      NS_WARNING("Failed to notify bluetooth-sco-status-changed observsers!");
      return NS_ERROR_FAILURE;
    }
  }
  mConnected = true;
}

void
BluetoothHfpManager::OnConnectError()
{
  LOG("[Hfp] %s", __FUNCTION__);
  CloseSocket();
  mConnected = false;
  // If connecting for some reason didn't work, restart listening
  Listen();
}

void
BluetoothHfpManager::OnDisconnect()
{
  NS_WARNING("GOT DISCONNECT!");
}
