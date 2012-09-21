/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"
#include "BluetoothDevice.h"
#include "BluetoothHfpManager.h"
#include "BluetoothPropertyEvent.h"
#include "BluetoothReplyRunnable.h"
#include "BluetoothService.h"
#include "BluetoothUtils.h"
#include "BluetoothServiceUuid.h"

#include "nsIDOMDOMRequest.h"
#include "nsDOMClassInfo.h"
#include "nsContentUtils.h"
#include "mozilla/dom/bluetooth/BluetoothTypes.h"

#undef LOG
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...) __android_log_print(ANDROID_LOG_INFO, "GonkDBus", args);
#else
#define BTDEBUG true
#define LOG(args...) if (BTDEBUG) printf(args);
#endif

USING_BLUETOOTH_NAMESPACE

DOMCI_DATA(BluetoothDevice, BluetoothDevice)

NS_IMPL_CYCLE_COLLECTION_CLASS(BluetoothDevice)

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

BluetoothDevice::BluetoothDevice(nsPIDOMWindow* aOwner,
                                 const nsAString& aAdapterPath,
                                 const BluetoothValue& aValue) :
  BluetoothPropertyContainer(BluetoothObjectType::TYPE_DEVICE),
  mJsUuids(nullptr),
  mJsServices(nullptr),
  mAdapterPath(aAdapterPath),
  mIsRooted(false)
{
  BindToOwner(aOwner);
  const InfallibleTArray<BluetoothNamedValue>& values =
    aValue.get_ArrayOfBluetoothNamedValue();
  for (uint32_t i = 0; i < values.Length(); ++i) {
    SetPropertyByValue(values[i]);
  }
}

BluetoothDevice::~BluetoothDevice()
{
  BluetoothService* bs = BluetoothService::Get();
  // bs can be null on shutdown, where destruction might happen.
  if (bs) {
    bs->UnregisterBluetoothSignalHandler(mPath, this);
  }
  Unroot();
}

void
BluetoothDevice::Root()
{
  if (!mIsRooted) {
    NS_HOLD_JS_OBJECTS(this, BluetoothDevice);
    mIsRooted = true;
  }
}

void
BluetoothDevice::Unroot()
{
  if (mIsRooted) {
    NS_DROP_JS_OBJECTS(this, BluetoothDevice);
    mIsRooted = false;
  }
}

void
BluetoothDevice::SetPropertyByValue(const BluetoothNamedValue& aValue)
{
  const nsString& name = aValue.name();
  const BluetoothValue& value = aValue.value();
  if (name.EqualsLiteral("Name")) {
    mName = value.get_nsString();
  } else if (name.EqualsLiteral("Address")) {
    mAddress = value.get_nsString();
    BluetoothService* bs = BluetoothService::Get();
    if (!bs) {
      NS_WARNING("BluetoothService not available!");
      return;
    }

    // We can't actually set up our path until we know our address
    bs->GetDevicePath(mAdapterPath, mAddress, mPath);
  } else if (name.EqualsLiteral("Address")) {
    mAddress = value.get_nsString();
  } else if (name.EqualsLiteral("Class")) {
    mClass = value.get_uint32_t();
  } else if (name.EqualsLiteral("Icon")) {
    mIcon = value.get_nsString();
  } else if (name.EqualsLiteral("Connected")) {
#ifdef MOZ_WIDGET_GONK
    // Connected is an 2-byte array
    // arr[0]: boolean value, true means connected, false means disconnected
    // arr[1]: disconnection reason
    InfallibleTArray<uint8_t> arr = value.get_ArrayOfuint8_t();
    mConnected = (arr[0] == 1);
#else
    mConnected = value.get_bool();
#endif
  } else if (name.EqualsLiteral("Paired")) {
    mPaired = value.get_bool();
  } else if (name.EqualsLiteral("UUIDs")) {
    mUuids = value.get_ArrayOfnsString();
    nsresult rv;
    nsIScriptContext* sc = GetContextForEventHandlers(&rv);
    if (sc) {
      rv =
        StringArrayToJSArray(sc->GetNativeContext(),
                             sc->GetNativeGlobal(), mUuids, &mJsUuids);
      if (NS_FAILED(rv)) {
        NS_WARNING("Cannot set JS UUIDs object!");
        return;
      }
      Root();
    } else {
      NS_WARNING("Could not get context!");
    }
  } else if (name.EqualsLiteral("Services")) {
    mServices = value.get_ArrayOfnsString();
    nsresult rv;
    nsIScriptContext* sc = GetContextForEventHandlers(&rv);
    if (sc) {
      rv =
        StringArrayToJSArray(sc->GetNativeContext(),
                             sc->GetNativeGlobal(), mServices, &mJsServices);
      if (NS_FAILED(rv)) {
        NS_WARNING("Cannot set JS Services object!");
        return;
      }
      Root();
    } else {
      NS_WARNING("Could not get context!");
    }
#ifdef DEBUG
  } else {
    nsCString warningMsg;
    warningMsg.AssignLiteral("Not handling device property: ");
    warningMsg.Append(NS_ConvertUTF16toUTF8(name));
    NS_WARNING(warningMsg.get());
#endif
  }
}

// static
already_AddRefed<BluetoothDevice>
BluetoothDevice::Create(nsPIDOMWindow* aOwner,
                        const nsAString& aAdapterPath,
                        const BluetoothValue& aValue)
{
  // Make sure we at least have a service
  BluetoothService* bs = BluetoothService::Get();
  if (!bs) {
    NS_WARNING("BluetoothService not available!");
    return nullptr;
  }

  nsRefPtr<BluetoothDevice> device = new BluetoothDevice(aOwner, aAdapterPath,
                                                         aValue);

  bs->RegisterBluetoothSignalHandler(device->mPath, device);

  return device.forget();
}

void
BluetoothDevice::Notify(const BluetoothSignal& aData)
{
  if (aData.name().EqualsLiteral("PropertyChanged")) {
    NS_ASSERTION(aData.value().type() == BluetoothValue::TArrayOfBluetoothNamedValue,
                 "PropertyChanged: Invalid value type");
    InfallibleTArray<BluetoothNamedValue> arr = aData.value().get_ArrayOfBluetoothNamedValue();

    NS_ASSERTION(arr.Length() == 1, "Got more than one property in a change message!");
    BluetoothNamedValue v = arr[0];
    nsString name = v.name();

    SetPropertyByValue(v);
    if (name.EqualsLiteral("Connected")) {
      nsRefPtr<nsDOMEvent> event = new nsDOMEvent(nullptr, nullptr);
      nsresult rv;
      if (mConnected) {
        rv = event->InitEvent(NS_LITERAL_STRING("connected"), false, false);
      } else {
        rv = event->InitEvent(NS_LITERAL_STRING("disconnected"), false, false);
      }
      if (NS_FAILED(rv)) {
        NS_WARNING("Failed to init the connected/disconnected event!!!");
        return;
      }

      event->SetTrusted(true);
      bool dummy;
      DispatchEvent(event, &dummy);
    } else {
      nsRefPtr<BluetoothPropertyEvent> e = BluetoothPropertyEvent::Create(name);
      e->Dispatch(ToIDOMEventTarget(), NS_LITERAL_STRING("propertychanged"));
    }
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
  aAddress = mAddress;
  return NS_OK;
}

NS_IMETHODIMP
BluetoothDevice::GetName(nsAString& aName)
{
  aName = mName;
  return NS_OK;
}

NS_IMETHODIMP
BluetoothDevice::GetIcon(nsAString& aIcon)
{
  aIcon = mIcon;
  return NS_OK;
}

NS_IMETHODIMP
BluetoothDevice::GetDeviceClass(uint32_t* aClass)
{
  *aClass = mClass;
  return NS_OK;
}

NS_IMETHODIMP
BluetoothDevice::GetPaired(bool* aPaired)
{
  *aPaired = mPaired;
  return NS_OK;
}

NS_IMETHODIMP
BluetoothDevice::GetConnected(bool* aConnected)
{
  *aConnected = mConnected;
  return NS_OK;
}

NS_IMETHODIMP
BluetoothDevice::GetUuids(JSContext* aCx, jsval* aUuids)
{
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
  if (mJsServices) {
    aServices->setObject(*mJsServices);
  } else {
    NS_WARNING("Services not yet set!\n");
  }
  return NS_OK;
}

NS_IMETHODIMP
BluetoothDevice::ConnectHeadset(nsIDOMDOMRequest** aRequest)
{
  BluetoothService* bs = BluetoothService::Get();
  if (!bs) {
    NS_WARNING("BluetoothService not available!");
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIDOMRequestService> rs = do_GetService("@mozilla.org/dom/dom-request-service;1");
  if (!rs) {
    NS_WARNING("No DOMRequest Service!");
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIDOMDOMRequest> req;
  nsresult rv = rs->CreateRequest(GetOwner(), getter_AddRefs(req));
  if (NS_FAILED(rv)) {
    NS_WARNING("Can't create DOMRequest!");
    return NS_ERROR_FAILURE;
  }

  nsRefPtr<BluetoothVoidReplyRunnable> result = new BluetoothVoidReplyRunnable(req);
  bs->ConnectHeadset(mPath, result);
  req.forget(aRequest);

  return NS_OK;
}

NS_IMETHODIMP
BluetoothDevice::DisconnectHeadset(nsIDOMDOMRequest** aRequest)
{  
  BluetoothService* bs = BluetoothService::Get();
  if (!bs) {
    NS_WARNING("BluetoothService not available!");
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIDOMRequestService> rs = do_GetService("@mozilla.org/dom/dom-request-service;1");
  if (!rs) {
    NS_WARNING("No DOMRequest Service!");
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIDOMDOMRequest> req;
  nsresult rv = rs->CreateRequest(GetOwner(), getter_AddRefs(req));
  if (NS_FAILED(rv)) {
    NS_WARNING("Can't create DOMRequest!");
    return NS_ERROR_FAILURE;
  }

  nsRefPtr<BluetoothVoidReplyRunnable> result = new BluetoothVoidReplyRunnable(req);
  bs->DisconnectHeadset(mPath, result);
  req.forget(aRequest);

  return NS_OK;
}

NS_IMPL_EVENT_HANDLER(BluetoothDevice, propertychanged)
NS_IMPL_EVENT_HANDLER(BluetoothDevice, connected)
NS_IMPL_EVENT_HANDLER(BluetoothDevice, disconnected)
