/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"
#include "BluetoothAdapter.h"
#include "BluetoothDevice.h"
#include "BluetoothPropertyEvent.h"
#include "BluetoothReplyRunnable.h"
#include "BluetoothService.h"
#include "BluetoothUtils.h"
#include "GeneratedEvents.h"

#include "nsContentUtils.h"
#include "nsDOMClassInfo.h"
#include "nsDOMEvent.h"
#include "nsIDOMBluetoothDeviceAddressEvent.h"
#include "nsIDOMBluetoothDeviceEvent.h"
#include "nsIDOMDOMRequest.h"
#include "nsTArrayHelpers.h"
#include "nsThreadUtils.h"
#include "nsXPCOMCIDInternal.h"

#include "mozilla/dom/bluetooth/BluetoothTypes.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/LazyIdleThread.h"
#include "mozilla/Util.h"

using namespace mozilla;

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

DOMCI_DATA(BluetoothAdapter, BluetoothAdapter)

NS_IMPL_CYCLE_COLLECTION_CLASS(BluetoothAdapter)

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(BluetoothAdapter,
                                               nsDOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mJsUuids)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mJsDeviceAddresses)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(BluetoothAdapter, 
                                                  nsDOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_SCRIPT_OBJECTS
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(BluetoothAdapter, 
                                                nsDOMEventTargetHelper)
  tmp->Unroot();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(BluetoothAdapter)
  NS_INTERFACE_MAP_ENTRY(nsIDOMBluetoothAdapter)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(BluetoothAdapter)
NS_INTERFACE_MAP_END_INHERITING(nsDOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(BluetoothAdapter, nsDOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(BluetoothAdapter, nsDOMEventTargetHelper)

class GetPairedDevicesTask : public BluetoothReplyRunnable
{
public:
  GetPairedDevicesTask(BluetoothAdapter* aAdapterPtr,
                       nsIDOMDOMRequest* aReq) :
    BluetoothReplyRunnable(aReq),
    mAdapterPtr(aAdapterPtr)
  {
    MOZ_ASSERT(aReq && aAdapterPtr);
  }

  virtual bool ParseSuccessfulReply(jsval* aValue)
  {
    LOG("[A] GetPairedDevicesTask::ParseSuccessfulReply");
    *aValue = JSVAL_VOID;
    BluetoothValue& v = mReply->get_BluetoothReplySuccess().value();
    if (v.type() != BluetoothValue::TArrayOfBluetoothNamedValue) {
      NS_WARNING("Not a BluetoothNamedValue array!");
      return false;
    }

    const InfallibleTArray<BluetoothNamedValue>& reply =
      mReply->get_BluetoothReplySuccess().value().get_ArrayOfBluetoothNamedValue();
    nsTArray<nsRefPtr<BluetoothDevice> > devices;
    JSObject* JsDevices;
    for (uint32_t i = 0; i < reply.Length(); i++) {
      if (reply[i].value().type() != BluetoothValue::TArrayOfBluetoothNamedValue) {
        NS_WARNING("Not a BluetoothNamedValue array!");
        return false;
      }
      nsRefPtr<BluetoothDevice> d = BluetoothDevice::Create(mAdapterPtr->GetOwner(),
                                                            mAdapterPtr->GetPath(),
                                                            reply[i].value());
      devices.AppendElement(d);
    }

    nsresult rv;
    nsIScriptContext* sc = mAdapterPtr->GetContextForEventHandlers(&rv);
    if (!sc) {
      NS_WARNING("Cannot create script context!");
      return false;
    }

    rv = nsTArrayToJSArray(sc->GetNativeContext(), devices, &JsDevices);

    if (JsDevices) {
      aValue->setObject(*JsDevices);
    }
    else {
      NS_WARNING("Paird not yet set!");
      return false;
    }
    return true;
  }

  void
  ReleaseMembers()
  {
    BluetoothReplyRunnable::ReleaseMembers();
    mAdapterPtr = nullptr;
  }
private:
  nsRefPtr<BluetoothAdapter> mAdapterPtr;
};

static int kCreatePairedDeviceTimeout = 50000; // unit: msec

BluetoothAdapter::BluetoothAdapter(nsPIDOMWindow* aOwner, const BluetoothValue& aValue)
    : BluetoothPropertyContainer(BluetoothObjectType::TYPE_ADAPTER)
    , mEnabled(false)
    , mDiscoverable(false)
    , mDiscovering(false)
    , mPairable(false)
    , mPowered(false)
    , mJsUuids(nullptr)
    , mJsDeviceAddresses(nullptr)
    , mIsRooted(false)
{
  LOG("[A] %s", __FUNCTION__);
  BindToOwner(aOwner);
  const InfallibleTArray<BluetoothNamedValue>& values =
    aValue.get_ArrayOfBluetoothNamedValue();
  for (uint32_t i = 0; i < values.Length(); ++i) {
    SetPropertyByValue(values[i]);
  }
}

BluetoothAdapter::~BluetoothAdapter()
{
  LOG("[A] %s", __FUNCTION__);
  BluetoothService* bs = BluetoothService::Get();
  // We can be null on shutdown, where this might happen
  if (bs) {
    // XXXbent I don't see anything about LOCAL_AGENT_PATH or REMOTE_AGENT_PATH
    //         here. Probably a bug? Maybe use UnregisterAll.
    bs->UnregisterBluetoothSignalHandler(mPath, this);
  }
  Unroot();
}

void
BluetoothAdapter::Unroot()
{
  LOGV("[A] %s", __FUNCTION__);
  if (!mIsRooted) {
    return;
  }
  mJsUuids = nullptr;
  mJsDeviceAddresses = nullptr;
  NS_DROP_JS_OBJECTS(this, BluetoothAdapter);
  mIsRooted = false;
}

void
BluetoothAdapter::Root()
{
  LOGV("[A] %s", __FUNCTION__);
  if (mIsRooted) {
    return;
  }
  NS_HOLD_JS_OBJECTS(this, BluetoothAdapter);
  mIsRooted = true;
}

static void PrintProperty(const nsAString& aName, const BluetoothValue& aValue);

void
PrintProperty(const nsAString& aName, const BluetoothValue& aValue)
{
  if (aValue.type() == BluetoothValue::TnsString) {
    LOGV("[A] %s, <%s, %s>", __FUNCTION__, NS_ConvertUTF16toUTF8(aName).get(), NS_ConvertUTF16toUTF8(aValue.get_nsString()).get());
    return;
  } else if (aValue.type() == BluetoothValue::Tuint32_t) {
    LOGV("[A] %s, <%s, %d>", __FUNCTION__, NS_ConvertUTF16toUTF8(aName).get(), aValue.get_uint32_t());
    return;
  } else if (aValue.type() == BluetoothValue::Tbool) {
    LOGV("[A] %s, <%s, %d>", __FUNCTION__, NS_ConvertUTF16toUTF8(aName).get(), aValue.get_bool());
    return;
  } else if (aValue.type() == BluetoothValue::TArrayOfBluetoothNamedValue) {
    LOGV("[A] %s, <%s, Array of BluetoothNamedValue>", __FUNCTION__, NS_ConvertUTF16toUTF8(aName).get());
    return;
  } else if (aValue.type() == BluetoothValue::TArrayOfnsString) {
    InfallibleTArray<nsString> tmp = aValue.get_ArrayOfnsString();
    for (int i = 0; i < tmp.Length(); i++) {
      LOGV("[A] %s, <%s, %s>", __FUNCTION__, NS_ConvertUTF16toUTF8(aName).get(), NS_ConvertUTF16toUTF8(tmp[i]).get());
    }
    return;
  } else {
    LOGV("[A] %s, <%s, Unknown value type>", __FUNCTION__, NS_ConvertUTF16toUTF8(aName).get());
    return;
  }
}

void
BluetoothAdapter::SetPropertyByValue(const BluetoothNamedValue& aValue)
{
  const nsString& name = aValue.name();
  const BluetoothValue& value = aValue.value();

  PrintProperty(name, value);

  if (name.EqualsLiteral("Name")) {
    mName = value.get_nsString();
  } else if (name.EqualsLiteral("Address")) {
    mAddress = value.get_nsString();
  } else if (name.EqualsLiteral("Path")) {
    mPath = value.get_nsString();
  } else if (name.EqualsLiteral("Enabled")) {
    mEnabled = value.get_bool();
  } else if (name.EqualsLiteral("Discoverable")) {
    mDiscoverable = value.get_bool();
  } else if (name.EqualsLiteral("Discovering")) {
    mDiscovering = value.get_bool();
  } else if (name.EqualsLiteral("Pairable")) {
    mPairable = value.get_bool();
  } else if (name.EqualsLiteral("Powered")) {
    mPowered = value.get_bool();
  } else if (name.EqualsLiteral("PairableTimeout")) {
    mPairableTimeout = value.get_uint32_t();
  } else if (name.EqualsLiteral("DiscoverableTimeout")) {
    mDiscoverableTimeout = value.get_uint32_t();
  } else if (name.EqualsLiteral("Class")) {
    mClass = value.get_uint32_t();
  } else if (name.EqualsLiteral("UUIDs")) {
    mUuids = value.get_ArrayOfnsString();
    nsresult rv;
    nsIScriptContext* sc = GetContextForEventHandlers(&rv);
    if (sc) {
      rv =
        nsTArrayToJSArray(sc->GetNativeContext(), mUuids, &mJsUuids);
      if (NS_FAILED(rv)) {
        NS_WARNING("Cannot set JS UUIDs object!");
        return;
      }
      Root();
    } else {
      NS_WARNING("Could not get context!");
    }
  } else if (name.EqualsLiteral("Devices")) {
    mDeviceAddresses = value.get_ArrayOfnsString();
    nsresult rv;
    nsIScriptContext* sc = GetContextForEventHandlers(&rv);
    if (sc) {
      rv =
        nsTArrayToJSArray(sc->GetNativeContext(), mDeviceAddresses,
                          &mJsDeviceAddresses);
      if (NS_FAILED(rv)) {
        NS_WARNING("Cannot set JS Device Addresses object!");
        return;
      }
      Root();
    } else {
      NS_WARNING("Could not get context!");
    }
  } else {
#ifdef DEBUG
    nsCString warningMsg;
    warningMsg.AssignLiteral("Not handling adapter property: ");
    warningMsg.Append(NS_ConvertUTF16toUTF8(name));
    NS_WARNING(warningMsg.get());
#endif
  }
}

// static
already_AddRefed<BluetoothAdapter>
BluetoothAdapter::Create(nsPIDOMWindow* aOwner, const BluetoothValue& aValue)
{
  LOG("[A] %s", __FUNCTION__);
  BluetoothService* bs = BluetoothService::Get();
  if (!bs) {
    NS_WARNING("BluetoothService not available!");
    return nullptr;
  }

  nsRefPtr<BluetoothAdapter> adapter = new BluetoothAdapter(aOwner, aValue);

  bs->RegisterBluetoothSignalHandler(adapter->GetPath(), adapter);

  return adapter.forget();
}

void
BluetoothAdapter::Notify(const BluetoothSignal& aData)
{
  LOG("[A] %s", __FUNCTION__);
  InfallibleTArray<BluetoothNamedValue> arr;

  if (aData.name().EqualsLiteral("DeviceFound")) {
    LOG("[A] Receive event - DeviceFound");
    nsRefPtr<BluetoothDevice> device = BluetoothDevice::Create(GetOwner(), mPath, aData.value());
    nsCOMPtr<nsIDOMEvent> event;
    NS_NewDOMBluetoothDeviceEvent(getter_AddRefs(event), nullptr, nullptr);

    nsCOMPtr<nsIDOMBluetoothDeviceEvent> e = do_QueryInterface(event);
    e->InitBluetoothDeviceEvent(NS_LITERAL_STRING("devicefound"),
                                false, false, device);
    DispatchTrustedEvent(event);
  } else if (aData.name().EqualsLiteral("DeviceDisappeared")) {
    LOG("[A] Receive event - DeviceDisappeared");
    const nsAString& deviceAddress = aData.value().get_nsString();

    nsCOMPtr<nsIDOMEvent> event;
    NS_NewDOMBluetoothDeviceAddressEvent(getter_AddRefs(event), nullptr, nullptr);

    nsCOMPtr<nsIDOMBluetoothDeviceAddressEvent> e = do_QueryInterface(event);
    e->InitBluetoothDeviceAddressEvent(NS_LITERAL_STRING("devicedisappeared"),
                                       false, false, deviceAddress);
    DispatchTrustedEvent(e);
  } else if (aData.name().EqualsLiteral("DeviceCreated")) {
	  LOG("[A] Receive event - DeviceCreated");
    NS_ASSERTION(aData.value().type() == BluetoothValue::TArrayOfBluetoothNamedValue,
                 "DeviceCreated: Invalid value type");

    nsRefPtr<BluetoothDevice> device = BluetoothDevice::Create(GetOwner(),
                                                               GetPath(),
                                                               aData.value());
    nsCOMPtr<nsIDOMEvent> event;
    NS_NewDOMBluetoothDeviceEvent(getter_AddRefs(event), nullptr, nullptr);

    nsCOMPtr<nsIDOMBluetoothDeviceEvent> e = do_QueryInterface(event);
    e->InitBluetoothDeviceEvent(NS_LITERAL_STRING("devicecreated"),
                                false, false, device);
    DispatchTrustedEvent(e);
  } else if (aData.name().EqualsLiteral("PropertyChanged")) {
	  LOG("[A] Receive event - PropertyChanged");
    PrintProperty(aData.name(), aData.value());

    NS_ASSERTION(aData.value().type() == BluetoothValue::TArrayOfBluetoothNamedValue,
                 "PropertyChanged: Invalid value type");
    arr = aData.value().get_ArrayOfBluetoothNamedValue();

    NS_ASSERTION(arr.Length() == 1, "Got more than one property in a change message!");
    BluetoothNamedValue v = arr[0];

    SetPropertyByValue(v);
    nsRefPtr<BluetoothPropertyEvent> e = BluetoothPropertyEvent::Create(v.name());
    e->Dispatch(ToIDOMEventTarget(), NS_LITERAL_STRING("propertychanged"));
  } else {
#ifdef DEBUG
    nsCString warningMsg;
    warningMsg.AssignLiteral("Not handling adapter signal: ");
    warningMsg.Append(NS_ConvertUTF16toUTF8(aData.name()));
    NS_WARNING(warningMsg.get());
#endif
  }
}

nsresult
BluetoothAdapter::StartStopDiscovery(bool aStart, nsIDOMDOMRequest** aRequest)
{
	LOG("[A] %s", __FUNCTION__);
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

  nsRefPtr<BluetoothVoidReplyRunnable> results = new BluetoothVoidReplyRunnable(req);

  if (aStart) {
    rv = bs->StartDiscoveryInternal(mPath, results);
  } else {
    rv = bs->StopDiscoveryInternal(mPath, results);
  }
  if (NS_FAILED(rv)) {
    NS_WARNING("Starting discovery failed!");
    return NS_ERROR_FAILURE;
  }

  req.forget(aRequest);
  
  // mDiscovering is not set here, we'll get a Property update from our external
  // protocol to tell us that it's been set.
  
  return NS_OK;
}

NS_IMETHODIMP
BluetoothAdapter::StartDiscovery(nsIDOMDOMRequest** aRequest)
{
	LOGV("[A] %s", __FUNCTION__);
  return StartStopDiscovery(true, aRequest);
}

NS_IMETHODIMP
BluetoothAdapter::StopDiscovery(nsIDOMDOMRequest** aRequest)
{
	LOGV("[A] %s", __FUNCTION__);
  return StartStopDiscovery(false, aRequest);
}

NS_IMETHODIMP
BluetoothAdapter::GetEnabled(bool* aEnabled)
{
	LOGV("[A] %s", __FUNCTION__);
  *aEnabled = mEnabled;
  return NS_OK;
}

NS_IMETHODIMP
BluetoothAdapter::GetAddress(nsAString& aAddress)
{
	LOGV("[A] %s", __FUNCTION__);
  aAddress = mAddress;
  return NS_OK;
}

NS_IMETHODIMP
BluetoothAdapter::GetAdapterClass(uint32_t* aClass)
{
	LOGV("[A] %s", __FUNCTION__);
  *aClass = mClass;
  return NS_OK;
}

NS_IMETHODIMP
BluetoothAdapter::GetDiscovering(bool* aDiscovering)
{
	LOGV("[A] %s", __FUNCTION__);
  *aDiscovering = mDiscovering;
  return NS_OK;
}

NS_IMETHODIMP
BluetoothAdapter::GetName(nsAString& aName)
{
	LOGV("[A] %s", __FUNCTION__);
  aName = mName;
  return NS_OK;
}

NS_IMETHODIMP
BluetoothAdapter::GetDiscoverable(bool* aDiscoverable)
{
	LOGV("[A] %s", __FUNCTION__);
  *aDiscoverable = mDiscoverable;
  return NS_OK;
}

NS_IMETHODIMP
BluetoothAdapter::GetDiscoverableTimeout(uint32_t* aDiscoverableTimeout)
{
	LOGV("[A] %s", __FUNCTION__);
  *aDiscoverableTimeout = mDiscoverableTimeout;
  return NS_OK;
}

NS_IMETHODIMP
BluetoothAdapter::GetDevices(JSContext* aCx, jsval* aDevices)
{
	LOGV("[A] %s", __FUNCTION__);
  if (mJsDeviceAddresses) {
    aDevices->setObject(*mJsDeviceAddresses);
  }
  else {
    NS_WARNING("Devices not yet set!\n");
    return NS_ERROR_FAILURE;
  }    
  return NS_OK;
}

nsresult
BluetoothAdapter::GetUuids(JSContext* aCx, jsval* aValue)
{
	LOGV("[A] %s", __FUNCTION__);
  if (mJsUuids) {
    aValue->setObject(*mJsUuids);
  }
  else {
    NS_WARNING("UUIDs not yet set!\n");
    return NS_ERROR_FAILURE;
  }    
  return NS_OK;
}

NS_IMETHODIMP
BluetoothAdapter::SetName(const nsAString& aName,
                          nsIDOMDOMRequest** aRequest)
{
	LOGV("[A] %s", __FUNCTION__);
  if (mName.Equals(aName)) {
    return FirePropertyAlreadySet(GetOwner(), aRequest);
  }
  nsString name(aName);
  BluetoothValue value(name);
  BluetoothNamedValue property(NS_LITERAL_STRING("Name"), value);
  return SetProperty(GetOwner(), property, aRequest);
}
 
NS_IMETHODIMP
BluetoothAdapter::SetDiscoverable(const bool aDiscoverable,
                                  nsIDOMDOMRequest** aRequest)
{
	LOGV("[A] %s", __FUNCTION__);
  if (aDiscoverable == mDiscoverable) {
    return FirePropertyAlreadySet(GetOwner(), aRequest);
  }
  BluetoothValue value(aDiscoverable);
  BluetoothNamedValue property(NS_LITERAL_STRING("Discoverable"), value);
  return SetProperty(GetOwner(), property, aRequest);
}
 
NS_IMETHODIMP
BluetoothAdapter::SetDiscoverableTimeout(const uint32_t aDiscoverableTimeout,
                                         nsIDOMDOMRequest** aRequest)
{
	LOGV("[A] %s", __FUNCTION__);
  if (aDiscoverableTimeout == mDiscoverableTimeout) {
    return FirePropertyAlreadySet(GetOwner(), aRequest);
  }
  BluetoothValue value(aDiscoverableTimeout);
  BluetoothNamedValue property(NS_LITERAL_STRING("DiscoverableTimeout"), value);
  return SetProperty(GetOwner(), property, aRequest);
}

NS_IMETHODIMP
BluetoothAdapter::GetPairedDevices(nsIDOMDOMRequest** aRequest)
{
	LOG("[A] %s", __FUNCTION__);
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

  nsCOMPtr<nsIDOMDOMRequest> request;
  nsresult rv = rs->CreateRequest(GetOwner(), getter_AddRefs(request));
  if (NS_FAILED(rv)) {
    NS_WARNING("Can't create DOMRequest!");
    return NS_ERROR_FAILURE;
  }

  nsRefPtr<BluetoothReplyRunnable> results = new GetPairedDevicesTask(this, request);
  if (NS_FAILED(bs->GetPairedDevicePropertiesInternal(mDeviceAddresses, results))) {
    return NS_ERROR_FAILURE;
  }
  request.forget(aRequest);
  return NS_OK;
}

nsresult
BluetoothAdapter::PairUnpair(bool aPair,
                             nsIDOMBluetoothDevice* aDevice,
                             nsIDOMDOMRequest** aRequest)
{
	LOG("[A] %s", __FUNCTION__);
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

  nsRefPtr<BluetoothVoidReplyRunnable> results = new BluetoothVoidReplyRunnable(req);

  nsString addr;
  aDevice->GetAddress(addr);

  if (aPair) {
    rv = bs->CreatePairedDeviceInternal(mPath,
                                        addr,
                                        kCreatePairedDeviceTimeout,
                                        results);
  } else {
    rv = bs->RemoveDeviceInternal(mPath, addr, results);
  }

  if (NS_FAILED(rv)) {
    NS_WARNING("Pair/Unpair failed!");
    return NS_ERROR_FAILURE;
  }

  req.forget(aRequest);

  return NS_OK;
}

nsresult
BluetoothAdapter::Pair(nsIDOMBluetoothDevice* aDevice, nsIDOMDOMRequest** aRequest)
{
	LOGV("[A] %s", __FUNCTION__);
  return PairUnpair(true, aDevice, aRequest);
}

nsresult
BluetoothAdapter::Unpair(nsIDOMBluetoothDevice* aDevice, nsIDOMDOMRequest** aRequest)
{
	LOGV("[A] %s", __FUNCTION__);
  return PairUnpair(false, aDevice, aRequest);
}

nsresult
BluetoothAdapter::SetPinCode(const nsAString& aDeviceAddress,
                             const nsAString& aPinCode,
                             nsIDOMDOMRequest** aRequest)
{
  LOG("[A] %s", __FUNCTION__);
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

  nsRefPtr<BluetoothVoidReplyRunnable> results = new BluetoothVoidReplyRunnable(req);

  bool result = bs->SetPinCodeInternal(aDeviceAddress, aPinCode, results);
  if(!result) {
    NS_WARNING("SetPinCode failed!");
    return NS_ERROR_FAILURE;
  }

  req.forget(aRequest);
  return NS_OK;  
}

nsresult
BluetoothAdapter::SetPasskey(const nsAString& aDeviceAddress, uint32_t aPasskey,
                             nsIDOMDOMRequest** aRequest)
{
  LOG("[A] %s", __FUNCTION__);
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

  nsRefPtr<BluetoothVoidReplyRunnable> results = new BluetoothVoidReplyRunnable(req);

  bool result = bs->SetPasskeyInternal(aDeviceAddress, aPasskey, results);
  if(!result) {
    NS_WARNING("SetPasskeyInternal failed!");
    return NS_ERROR_FAILURE;
  }

  req.forget(aRequest);
  return NS_OK;  
}

nsresult
BluetoothAdapter::SetPairingConfirmation(const nsAString& aDeviceAddress,
                                         bool aConfirmation,
                                         nsIDOMDOMRequest** aRequest)
{
  LOG("[A] %s", __FUNCTION__);
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

  nsRefPtr<BluetoothVoidReplyRunnable> results = new BluetoothVoidReplyRunnable(req);

  bool result = bs->SetPairingConfirmationInternal(aDeviceAddress, aConfirmation, results);
  if(!result) {
    NS_WARNING("SetPairingConfirmation failed!");
    return NS_ERROR_FAILURE;
  }

  req.forget(aRequest);
  return NS_OK;  
}

nsresult
BluetoothAdapter::SetAuthorization(const nsAString& aDeviceAddress, bool aAllow,
                                   nsIDOMDOMRequest** aRequest)
{
  LOG("[A] %s", __FUNCTION__);
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

  nsRefPtr<BluetoothVoidReplyRunnable> results = new BluetoothVoidReplyRunnable(req);

  bool result = bs->SetAuthorizationInternal(aDeviceAddress, aAllow, results);
  if(!result) {
    NS_WARNING("SetAuthorization failed!");
    return NS_ERROR_FAILURE;
  }

  req.forget(aRequest);
  return NS_OK;  
}

NS_IMETHODIMP
BluetoothAdapter::Connect(const nsAString& aDeviceAddress,
                          uint16_t aProfileId,
                          nsIDOMDOMRequest** aRequest)
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

  nsRefPtr<BluetoothVoidReplyRunnable> results =
    new BluetoothVoidReplyRunnable(req);
  bs->Connect(aDeviceAddress, mPath, aProfileId, results);

  req.forget(aRequest);
  return NS_OK;
}

NS_IMETHODIMP
BluetoothAdapter::Disconnect(uint16_t aProfileId,
                             nsIDOMDOMRequest** aRequest)
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

  nsRefPtr<BluetoothVoidReplyRunnable> result =
    new BluetoothVoidReplyRunnable(req);
  bs->Disconnect(aProfileId, result);

  req.forget(aRequest);
  return NS_OK;
}

NS_IMETHODIMP
BluetoothAdapter::SendFile(const nsAString& aDeviceAddress,
                           nsIDOMBlob* aBlob,
                           nsIDOMDOMRequest** aRequest)
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

  BlobChild* actor =
      mozilla::dom::ContentChild::GetSingleton()->GetOrCreateActorForBlob(aBlob);

  if (!actor) {
    NS_WARNING("Can't create actor");
    return NS_ERROR_FAILURE;
  }

  nsRefPtr<BluetoothVoidReplyRunnable> result = new BluetoothVoidReplyRunnable(req);
  bs->SendFile(aDeviceAddress, nullptr, actor, result);
  req.forget(aRequest);

  return NS_OK;
}

NS_IMETHODIMP
BluetoothAdapter::StopSendingFile(const nsAString& aDeviceAddress,
                                  nsIDOMDOMRequest** aRequest)
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
  bs->StopSendingFile(aDeviceAddress, result);
  req.forget(aRequest);

  return NS_OK;
}

nsresult
BluetoothAdapter::ConfirmReceivingFile(const nsAString& aDeviceAddress,
                                       bool aConfirmation,
                                       nsIDOMDOMRequest** aRequest)
{
  BluetoothService* bs = BluetoothService::Get();
  if (!bs) {
    NS_WARNING("BluetoothService not available!");
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIDOMRequestService> rs =
    do_GetService("@mozilla.org/dom/dom-request-service;1");

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

  nsRefPtr<BluetoothVoidReplyRunnable> result =
    new BluetoothVoidReplyRunnable(req);
  bs->ConfirmReceivingFile(aDeviceAddress, aConfirmation, result);

  req.forget(aRequest);

  return NS_OK;
}

NS_IMPL_EVENT_HANDLER(BluetoothAdapter, propertychanged)
NS_IMPL_EVENT_HANDLER(BluetoothAdapter, devicefound)
NS_IMPL_EVENT_HANDLER(BluetoothAdapter, devicedisappeared)
NS_IMPL_EVENT_HANDLER(BluetoothAdapter, devicecreated)
NS_IMPL_EVENT_HANDLER(BluetoothAdapter, requestconfirmation)
NS_IMPL_EVENT_HANDLER(BluetoothAdapter, requestpincode)
NS_IMPL_EVENT_HANDLER(BluetoothAdapter, requestpasskey)
NS_IMPL_EVENT_HANDLER(BluetoothAdapter, authorize)
NS_IMPL_EVENT_HANDLER(BluetoothAdapter, cancel)
