/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"

#include "BluetoothUtils.h"
#include "jsapi.h"
#include "mozilla/Scoped.h"
#include "mozilla/dom/bluetooth/BluetoothTypes.h"
#include "nsContentUtils.h"
#include "nsIScriptContext.h"
#include "nsISystemMessagesInternal.h"
#include "nsTArray.h"
#include "nsTArrayHelpers.h"
#include "nsString.h"

USING_BLUETOOTH_NAMESPACE

nsresult
mozilla::dom::bluetooth::SetJsObject(JSContext* aContext,
                                     const BluetoothValue& aValue,
                                     JSObject* aObj)
{
  MOZ_ASSERT(aContext && aObj);

  if (aValue.type() == BluetoothValue::TArrayOfnsString) {
    nsTArray<nsString> sourceArray = aValue.get_ArrayOfnsString();
    if (NS_FAILED(nsTArrayToJSArray(aContext,
                                    sourceArray,
                                    &aObj))) {
#ifdef DEBUG
      NS_WARNING("Cannot set JS UUIDs object!");
#endif
      return NS_ERROR_FAILURE;
    }

    return NS_OK;
  } else  if (aValue.type() == BluetoothValue::TArrayOfBluetoothNamedValue) {
    InfallibleTArray<BluetoothNamedValue> arr =
      aValue.get_ArrayOfBluetoothNamedValue();

    for (uint32_t i = 0; i < arr.Length(); i++) {
      jsval val;
      BluetoothValue& v = arr[i].value();
      JSString* JsData;
 
      switch(v.type()) {
        case BluetoothValue::TnsString:
          JsData =
            JS_NewStringCopyN(aContext,
                              NS_ConvertUTF16toUTF8(v.get_nsString()).get(),
                              v.get_nsString().Length());
          NS_ENSURE_TRUE(JsData, NS_ERROR_FAILURE);
          val = STRING_TO_JSVAL(JsData);
          break;
        case BluetoothValue::Tuint32_t:
          val = INT_TO_JSVAL(v.get_uint32_t());
          break;
        case BluetoothValue::Tbool:
          val = BOOLEAN_TO_JSVAL(v.get_bool());
          break;
        default:
#ifdef DEBUG
          NS_WARNING("SetJsObject: Parameter is not handled");
#endif
          break;
      }

      if (!JS_SetProperty(aContext, aObj,
                          NS_ConvertUTF16toUTF8(arr[i].name()).get(),
                          &val)) {
        NS_WARNING("Failed to set property");
        return NS_ERROR_FAILURE;
      }
    }
    return NS_OK;
  }

#ifdef DEBUG
  NS_WARNING("Not handle the type of BluetoothValue!");
#endif
  return NS_ERROR_FAILURE;
}

nsString
mozilla::dom::bluetooth::GetObjectPathFromAddress(const nsAString& aAdapterPath,
                                                  const nsAString& aDeviceAddress)
{
  // The object path would be like /org/bluez/2906/hci0/dev_00_23_7F_CB_B4_F1,
  // and the adapter path would be the first part of the object path, according
  // to the example above, it's /org/bluez/2906/hci0.
  nsString devicePath(aAdapterPath);
  devicePath.AppendLiteral("/dev_");
  devicePath.Append(aDeviceAddress);
  devicePath.ReplaceChar(':', '_');
  return devicePath;
}

nsString
mozilla::dom::bluetooth::GetAddressFromObjectPath(const nsAString& aObjectPath)
{
  // The object path would be like /org/bluez/2906/hci0/dev_00_23_7F_CB_B4_F1,
  // and the adapter path would be the first part of the object path, according
  // to the example above, it's /org/bluez/2906/hci0.
  nsString address(aObjectPath);
  int addressHead = address.RFind("/") + 5;

  MOZ_ASSERT(addressHead + BLUETOOTH_ADDRESS_LENGTH == address.Length());

  address.Cut(0, addressHead);
  address.ReplaceChar('_', ':');

  return address;
}

bool
mozilla::dom::bluetooth::BroadcastSystemMessage(
  const nsAString& aType,
  const InfallibleTArray<BluetoothNamedValue>& aData)
{
  JSContext* cx = nsContentUtils::GetSafeJSContext();
  NS_ASSERTION(!::JS_IsExceptionPending(cx),
      "Shouldn't get here when an exception is pending!");

  JSAutoRequest jsar(cx);
  JSObject* obj = JS_NewObject(cx, NULL, NULL, NULL);
  if (!obj) {
    NS_WARNING("Failed to new JSObject for system message!");
    return false;
  }

  if (NS_FAILED(SetJsObject(cx, aData, obj))) {
    NS_WARNING("Failed to set properties of system message!");
    return false;
  }

  nsCOMPtr<nsISystemMessagesInternal> systemMessenger =
    do_GetService("@mozilla.org/system-message-internal;1");
  NS_ENSURE_TRUE(systemMessenger, false);
  systemMessenger->BroadcastMessage(aType, OBJECT_TO_JSVAL(obj));

  return true;
}

