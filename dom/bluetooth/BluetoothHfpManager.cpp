/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"

#include "BluetoothHfpManager.h"

#include "BluetoothReplyRunnable.h"
#include "BluetoothService.h"
#include "BluetoothServiceUuid.h"

#include "mozilla/Services.h"
#include "nsIDOMDOMRequest.h"
#include "mozilla/dom/bluetooth/BluetoothTypes.h"
#include "nsIObserverService.h"
#include "nsVariant.h"

#include "nsISystemMessagesInternal.h"
#include "nsContentUtils.h"

#define VOLUMN_UP_SETTING "volumnup"
#define VOLUMN_DOWN_SETTING "volumndown"

#undef LOG
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "GonkDBus", args);
#else
#define BTDEBUG true
#define LOG(args...) if (BTDEBUG) printf(args);
#endif

USING_BLUETOOTH_NAMESPACE

static BluetoothHfpManager* sInstance = nullptr;

BluetoothHfpManager::BluetoothHfpManager() : mCurrentVgs(-1)
{
}

BluetoothHfpManager::~BluetoothHfpManager()
{
}

//static
BluetoothHfpManager*
BluetoothHfpManager::Get()
{
  if (sInstance == nullptr) {
    sInstance = new BluetoothHfpManager();
  }

  return sInstance;
}

nsresult
BluetoothHfpManager::HandleSettingsChanged(const nsAString& aData)
{
  MOZ_ASSERT(NS_IsMainThread());

  // The string that we're interested in will be a JSON string that looks like:
  //  {"key":"volumnup", "value":?}
  //  {"key":"volumndown", "value":?}

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
  if (JS_StringEqualsAscii(cx, key.toString(), VOLUMN_UP_SETTING, &match)) {
    nsAutoCString newVgs;
    newVgs += "VGS: ";

    if (mCurrentVgs == 15) {
      // already MAX
    } else {
      nsCString vgs;
      ConvertToACString(mCurrentVgs+1, vgs);

      SendLine(newVgs.get());

      mCurrentVgs++;
    }
  } else if (JS_StringEqualsAscii(cx, key.toString(), VOLUMN_DOWN_SETTING, &match)) {
    nsAutoCString newVgs;
    newVgs += "VGS: ";

    if (mCurrentVgs == 0) {
      // already MIN
    } else {
      nsCString vgs;
      ConvertToACString(mCurrentVgs-1, vgs);
      newVgs += vgs;

      SendLine(newVgs.get());

      mCurrentVgs++;
    }
  } else {
    MOZ_ASSERT(!JS_IsExceptionPending(cx));
    return NS_ERROR_OUT_OF_MEMORY;
  }

  if (!match) {
    return NS_OK;
  }

/*  if (mSettingsCheckInProgress) {
    // Somehow the setting for bluetooth has been flipped before our first
    // settings check completed. Flip this flag so that we ignore the result
    // of that check whenever it finishes.
    mSettingsCheckInProgress = false;
  }*/

  return NS_OK;
}

nsresult
BroadcastSystemMessage(const char* aCommand,
                       const int aCommandLength)
{
  nsString type;
  type.AssignLiteral("bluetooth-dialercommand");

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
BluetoothHfpManager::ReceiveSocketData(mozilla::ipc::SocketRawData* aMessage)
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
    if (!BroadcastSystemMessage("BLDN", 4)) {
      NS_WARNING("Failed to broadcast system message to dialer");
      return;
    }
    SendLine("OK");
  } else if (!strncmp(msg, "ATA", 3)) {
    if (!BroadcastSystemMessage("ATA", 3)) {
      NS_WARNING("Failed to broadcast system message to dialer");
      return;
    }
    SendLine("OK");
  } else if (!strncmp(msg, "AT+CHUP", 7)) {
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
  LOG("[H] Connect");
  MOZ_ASSERT(NS_IsMainThread());

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

  BluetoothService* bs = BluetoothService::Get();
  if (!bs) {
    NS_WARNING("BluetoothService not available!");
    return false;
  }

  nsRefPtr<BluetoothReplyRunnable> runnable = aRunnable;

  nsresult rv = bs->CloseSocket(this, runnable);
  runnable.forget();

  return NS_FAILED(rv) ? false : true;
}

void
BluetoothHfpManager::SendLine(const char* aMessage)
{
  const char* kHfpCrlf = "\xd\xa";
  nsAutoCString msg;

  msg += kHfpCrlf;
  msg += aMessage;
  msg += kHfpCrlf;

  SendSocketData(new mozilla::ipc::SocketRawData(msg.get()));
}

