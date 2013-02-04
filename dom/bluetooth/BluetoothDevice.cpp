/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"
#include "BluetoothDevice.h"
#include "BluetoothReplyRunnable.h"
#include "BluetoothService.h"
#include "BluetoothUtils.h"

#include "nsDOMClassInfo.h"
#include "nsContentUtils.h"
#include "nsTArrayHelpers.h"

#include "mozilla/dom/bluetooth/BluetoothTypes.h"

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
#define LOGV(args...) if (BTDEBUG) printf(args);
#endif

USING_BLUETOOTH_NAMESPACE

DOMCI_DATA(BluetoothDevice, BluetoothDevice)

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(BluetoothDevice,
                                               nsDOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mJsUuids)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mJsServices)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(BluetoothDevice,
                                                  nsDOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_SCRIPT_OBJECTS
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(BluetoothDevice,
                                                nsDOMEventTargetHelper)
  tmp->Unroot();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(BluetoothDevice)
  NS_INTERFACE_MAP_ENTRY(nsIDOMBluetoothDevice)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(BluetoothDevice)
NS_INTERFACE_MAP_END_INHERITING(nsDOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(BluetoothDevice, nsDOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(BluetoothDevice, nsDOMEventTargetHelper)

BluetoothDevice::BluetoothDevice(nsPIDOMWindow* aWindow,
                                 const nsAString& aAdapterPath,
                                 const BluetoothValue& aValue) :
  BluetoothPropertyContainer(BluetoothObjectType::TYPE_DEVICE),
  mJsUuids(nullptr),
  mJsServices(nullptr),
  mAdapterPath(aAdapterPath),
  mIsRooted(false)
{
  LOG("[D] %s", __FUNCTION__);
  MOZ_ASSERT(aWindow);

  BindToOwner(aWindow);
  const InfallibleTArray<BluetoothNamedValue>& values =
    aValue.get_ArrayOfBluetoothNamedValue();
  for (uint32_t i = 0; i < values.Length(); ++i) {
    SetPropertyByValue(values[i]);
  }

  BluetoothService* bs = BluetoothService::Get();
  NS_ENSURE_TRUE_VOID(bs);
  bs->RegisterBluetoothSignalHandler(mPath, this);
}

BluetoothDevice::~BluetoothDevice()
{
  LOG("[D] %s", __FUNCTION__);
  BluetoothService* bs = BluetoothService::Get();
  // bs can be null on shutdown, where destruction might happen.
  NS_ENSURE_TRUE_VOID(bs);
  bs->UnregisterBluetoothSignalHandler(mPath, this);
  Unroot();
}

void
BluetoothDevice::Root()
{
  LOGV("[D] %s", __FUNCTION__);
  if (!mIsRooted) {
    NS_HOLD_JS_OBJECTS(this, BluetoothDevice);
    mIsRooted = true;
  }
}

void
BluetoothDevice::Unroot()
{
  LOGV("[D] %s", __FUNCTION__);
  if (mIsRooted) {
    mJsUuids = nullptr;
    mJsServices = nullptr;
    NS_DROP_JS_OBJECTS(this, BluetoothDevice);
    mIsRooted = false;
  }
}

static void PrintProperty(const nsAString& aName, const BluetoothValue& aValue);

void
PrintProperty(const nsAString& aName, const BluetoothValue& aValue)
{
  if (aValue.type() == BluetoothValue::TnsString) {
    LOGV("[D] %s, <%s, %s>", __FUNCTION__, NS_ConvertUTF16toUTF8(aName).get(), NS_ConvertUTF16toUTF8(aValue.get_nsString()).get());
    return;
  } else if (aValue.type() == BluetoothValue::Tuint32_t) {
    LOGV("[D] %s, <%s, %d>", __FUNCTION__, NS_ConvertUTF16toUTF8(aName).get(), aValue.get_uint32_t());
    return;
  } else if (aValue.type() == BluetoothValue::Tbool) {
    LOGV("[D] %s, <%s, %d>", __FUNCTION__, NS_ConvertUTF16toUTF8(aName).get(), aValue.get_bool());
    return;
  } else if (aValue.type() == BluetoothValue::TArrayOfBluetoothNamedValue) {
    LOGV("[D] %s, <%s, Array of BluetoothNamedValue>", __FUNCTION__, NS_ConvertUTF16toUTF8(aName).get());
    return;
  } else if (aValue.type() == BluetoothValue::TArrayOfnsString) {
    InfallibleTArray<nsString> tmp(aValue.get_ArrayOfnsString());
    for (int i = 0; i < tmp.Length(); i++) {
      LOGV("[D] %s, <%s, %s>", __FUNCTION__, NS_ConvertUTF16toUTF8(aName).get(), NS_ConvertUTF16toUTF8(tmp[i]).get());
    }
    return;
//  } else if (aValue.type() == BluetoothValue::TArrayOfuint8_t) {
//    LOGV("[D] %s, <%s, %d>", __FUNCTION__, NS_ConvertUTF16toUTF8(aName).get(), aValue.get_ArrayOfuint8_t()[0]);
  } else {
    LOGV("[D] %s, <%s, Unknown value type>", __FUNCTION__, NS_ConvertUTF16toUTF8(aName).get());
    return;
  }
}


void
BluetoothDevice::SetPropertyByValue(const BluetoothNamedValue& aValue)
{
  const nsString& name = aValue.name();
  const BluetoothValue& value = aValue.value();

  PrintProperty(name, value);

  if (name.EqualsLiteral("Name")) {
    mName = value.get_nsString();
  } else if (name.EqualsLiteral("Path")) {
    MOZ_ASSERT(value.get_nsString().Length() > 0);
    mPath = value.get_nsString();
  } else if (name.EqualsLiteral("Address")) {
    mAddress = value.get_nsString();
  } else if (name.EqualsLiteral("Class")) {
    mClass = value.get_uint32_t();
  } else if (name.EqualsLiteral("Icon")) {
    mIcon = value.get_nsString();
  } else if (name.EqualsLiteral("Connected")) {
    mConnected = value.get_bool();
  } else if (name.EqualsLiteral("Paired")) {
    mPaired = value.get_bool();
  } else if (name.EqualsLiteral("UUIDs")) {
    mUuids = value.get_ArrayOfnsString();
    nsresult rv;
    nsIScriptContext* sc = GetContextForEventHandlers(&rv);
    NS_ENSURE_SUCCESS_VOID(rv);

    if (NS_FAILED(nsTArrayToJSArray(sc->GetNativeContext(),
                                    mUuids,
                                    &mJsUuids))) {
      NS_WARNING("Cannot set JS UUIDs object!");
      return;
    }
    Root();
  } else if (name.EqualsLiteral("Services")) {
    mServices = value.get_ArrayOfnsString();
    nsresult rv;
    nsIScriptContext* sc = GetContextForEventHandlers(&rv);
    NS_ENSURE_SUCCESS_VOID(rv);

    if (NS_FAILED(nsTArrayToJSArray(sc->GetNativeContext(),
                                    mServices,
                                    &mJsServices))) {
      NS_WARNING("Cannot set JS Services object!");
      return;
    }
    Root();
  } else {
    nsCString warningMsg;
    warningMsg.AssignLiteral("Not handling device property: ");
    warningMsg.Append(NS_ConvertUTF16toUTF8(name));
    NS_WARNING(warningMsg.get());
  }
}

// static
already_AddRefed<BluetoothDevice>
BluetoothDevice::Create(nsPIDOMWindow* aWindow,
                        const nsAString& aAdapterPath,
                        const BluetoothValue& aValue)
{
  LOG("[D] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aWindow);

  nsRefPtr<BluetoothDevice> device =
    new BluetoothDevice(aWindow, aAdapterPath, aValue);
  return device.forget();
}

void
BluetoothDevice::Notify(const BluetoothSignal& aData)
{
  BT_LOG("[D] %s: %s", __FUNCTION__, NS_ConvertUTF16toUTF8(aData.name()).get());

  BluetoothValue v = aData.value();
  if (aData.name().EqualsLiteral("PropertyChanged")) {
    NS_ASSERTION(v.type() == BluetoothValue::TArrayOfBluetoothNamedValue,
                 "PropertyChanged: Invalid value type");
    const InfallibleTArray<BluetoothNamedValue>& arr =
      v.get_ArrayOfBluetoothNamedValue();

    NS_ASSERTION(arr.Length() == 1,
                 "Got more than one property in a change message!");
    SetPropertyByValue(arr[0]);
  } else {
#ifdef DEBUG
    nsCString warningMsg;
    warningMsg.AssignLiteral("Not handling device signal: ");
    warningMsg.Append(NS_ConvertUTF16toUTF8(aData.name()));
    NS_WARNING(warningMsg.get());
#endif
  }
}

NS_IMETHODIMP
BluetoothDevice::GetAddress(nsAString& aAddress)
{
  LOGV("[D] %s", __FUNCTION__);
  aAddress = mAddress;
  return NS_OK;
}

NS_IMETHODIMP
BluetoothDevice::GetName(nsAString& aName)
{
  LOGV("[D] %s", __FUNCTION__);
  aName = mName;
  return NS_OK;
}

NS_IMETHODIMP
BluetoothDevice::GetIcon(nsAString& aIcon)
{
  LOGV("[D] %s", __FUNCTION__);
  aIcon = mIcon;
  return NS_OK;
}

NS_IMETHODIMP
BluetoothDevice::GetDeviceClass(uint32_t* aClass)
{
  LOGV("[D] %s", __FUNCTION__);
  *aClass = mClass;
  return NS_OK;
}

NS_IMETHODIMP
BluetoothDevice::GetPaired(bool* aPaired)
{
  LOGV("[D] %s", __FUNCTION__);
  *aPaired = mPaired;
  return NS_OK;
}

NS_IMETHODIMP
BluetoothDevice::GetConnected(bool* aConnected)
{
  LOGV("[D] %s", __FUNCTION__);
  *aConnected = mConnected;
  return NS_OK;
}

NS_IMETHODIMP
BluetoothDevice::GetUuids(JSContext* aCx, jsval* aUuids)
{
  LOGV("[D] %s", __FUNCTION__);
  if (mJsUuids) {
    aUuids->setObject(*mJsUuids);
  } else {
    NS_WARNING("UUIDs not yet set!\n");
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

NS_IMETHODIMP
BluetoothDevice::GetServices(JSContext* aCx, jsval* aServices)
{
  LOGV("[D] %s", __FUNCTION__);
  if (mJsServices) {
    aServices->setObject(*mJsServices);
  } else {
    NS_WARNING("Services not yet set!\n");
  }
  return NS_OK;
}

NS_IMPL_EVENT_HANDLER(BluetoothDevice, propertychanged)
NS_IMPL_EVENT_HANDLER(BluetoothDevice, connected)
NS_IMPL_EVENT_HANDLER(BluetoothDevice, disconnected)
