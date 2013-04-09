/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/basictypes.h"
#include "BluetoothManager.h"
#include "BluetoothCommon.h"
#include "BluetoothAdapter.h"
#include "BluetoothService.h"
#include "BluetoothReplyRunnable.h"

#include "DOMRequest.h"
#include "nsContentUtils.h"
#include "nsDOMClassInfo.h"
#include "nsIPermissionManager.h"
#include "nsThreadUtils.h"
#include "mozilla/Util.h"
#include "mozilla/dom/bluetooth/BluetoothTypes.h"

#undef LOG
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "GonkDBus", args);
#else
#define BTDEBUG true
#define LOG(args...) if (BTDEBUG) printf(args);
#endif

using namespace mozilla;

USING_BLUETOOTH_NAMESPACE

DOMCI_DATA(BluetoothManager, BluetoothManager)

NS_IMPL_CYCLE_COLLECTION_CLASS(BluetoothManager)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(BluetoothManager,
                                                  nsDOMEventTargetHelper)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(BluetoothManager,
                                                nsDOMEventTargetHelper)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(BluetoothManager)
  NS_INTERFACE_MAP_ENTRY(nsIDOMBluetoothManager)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(BluetoothManager)
NS_INTERFACE_MAP_END_INHERITING(nsDOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(BluetoothManager, nsDOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(BluetoothManager, nsDOMEventTargetHelper)

class GetAdapterTask : public BluetoothReplyRunnable
{
public:
  GetAdapterTask(BluetoothManager* aManager,
                 nsIDOMDOMRequest* aReq) :
    BluetoothReplyRunnable(aReq),
    mManagerPtr(aManager)
  {
  }

  bool
  ParseSuccessfulReply(jsval* aValue)
  {
    LOG("[M] GetAdapterTask::ParseSuccessfulReply");
    *aValue = JSVAL_VOID;

    const BluetoothValue& v = mReply->get_BluetoothReplySuccess().value();

    MOZ_ASSERT(v.type() == BluetoothValue::TArrayOfBluetoothNamedValue);
    const InfallibleTArray<BluetoothNamedValue>& values =
      v.get_ArrayOfBluetoothNamedValue();
    nsCOMPtr<nsIDOMBluetoothAdapter> adapter;
    adapter = BluetoothAdapter::Create(mManagerPtr->GetOwner(), values);

    nsresult rv;
    nsIScriptContext* sc = mManagerPtr->GetContextForEventHandlers(&rv);
    if (!sc) {
      NS_WARNING("Cannot create script context!");
      SetError(NS_LITERAL_STRING("BluetoothScriptContextError"));
      return false;
    }

    rv = nsContentUtils::WrapNative(sc->GetNativeContext(),
                                    sc->GetNativeGlobal(),
                                    adapter,
                                    aValue);
    if (NS_FAILED(rv)) {
      NS_WARNING("Cannot create native object!");
      SetError(NS_LITERAL_STRING("BluetoothNativeObjectError"));
      return false;
    }

    return true;
  }

  void
  ReleaseMembers()
  {
    BluetoothReplyRunnable::ReleaseMembers();
    mManagerPtr = nullptr;
  }
  
private:
  nsRefPtr<BluetoothManager> mManagerPtr;
};

BluetoothManager::BluetoothManager(nsPIDOMWindow *aWindow)
  : BluetoothPropertyContainer(BluetoothObjectType::TYPE_MANAGER)
{
  LOG("[M] %s", __FUNCTION__);
  MOZ_ASSERT(aWindow);

  BindToOwner(aWindow);
  mPath.AssignLiteral("/");

  BluetoothService* bs = BluetoothService::Get();
  NS_ENSURE_TRUE_VOID(bs);
  bs->RegisterBluetoothSignalHandler(mPath, this);
}

BluetoothManager::~BluetoothManager()
{ 
  LOG("[M] %s", __FUNCTION__);
  BluetoothService* bs = BluetoothService::Get();
  NS_ENSURE_TRUE_VOID(bs);
  bs->UnregisterBluetoothSignalHandler(mPath, this);
}

void
BluetoothManager::SetPropertyByValue(const BluetoothNamedValue& aValue)
{
  LOG("[M] %s", __FUNCTION__);

  const nsString& name2 = aValue.name();
  const BluetoothValue& value = aValue.value();
  if (value.type() == BluetoothValue::TnsString) {
    LOG("[M] %s, <%s, %s>", __FUNCTION__, NS_ConvertUTF16toUTF8(name2).get(),
        NS_ConvertUTF16toUTF8(value.get_nsString()).get());
  } else if (value.type() == BluetoothValue::Tuint32_t) {
    LOG("[M] %s, <%s, %d>", __FUNCTION__, NS_ConvertUTF16toUTF8(name2).get(),
        value.get_uint32_t());
  } else if (value.type() == BluetoothValue::Tbool) {
    LOG("[M] %s, <%s, %d>", __FUNCTION__, NS_ConvertUTF16toUTF8(name2).get(),
        value.get_bool());
  } else if (value.type() == BluetoothValue::TArrayOfBluetoothNamedValue) {
    LOG("[M] %s, <%s, Array of BluetoothNamedValue>", __FUNCTION__,
        NS_ConvertUTF16toUTF8(name2).get());
  } else if (value.type() == BluetoothValue::TArrayOfnsString) {
    nsTArray<nsString> tmp = value.get_ArrayOfnsString();
    for (int i = 0; i < tmp.Length(); i++) {
      LOG("[M] %s, <%s, %s>", __FUNCTION__, NS_ConvertUTF16toUTF8(name2).get(),
          NS_ConvertUTF16toUTF8(tmp[i]).get());
    }
  } else {
    LOG("[M] %s, <%s, Unknown value type>", __FUNCTION__,
        NS_ConvertUTF16toUTF8(name2).get());
  }

#ifdef DEBUG
    const nsString& name = aValue.name();
    nsCString warningMsg;
    warningMsg.AssignLiteral("Not handling manager property: ");
    warningMsg.Append(NS_ConvertUTF16toUTF8(name));
    NS_WARNING(warningMsg.get());
#endif
}

NS_IMETHODIMP
BluetoothManager::GetEnabled(bool* aEnabled)
{
  BluetoothService* bs = BluetoothService::Get();
  NS_ENSURE_TRUE(bs, NS_ERROR_FAILURE);

  *aEnabled = bs->IsEnabled();
  return NS_OK;
}

NS_IMETHODIMP
BluetoothManager::GetDefaultAdapter(nsIDOMDOMRequest** aAdapter)
{
  LOG("[M] %s", __FUNCTION__);
  nsCOMPtr<nsIDOMRequestService> rs =
    do_GetService(DOMREQUEST_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(rs, NS_ERROR_FAILURE);

  nsCOMPtr<nsIDOMDOMRequest> request;
  nsresult rv = rs->CreateRequest(GetOwner(), getter_AddRefs(request));
  NS_ENSURE_SUCCESS(rv, rv);

  nsRefPtr<BluetoothReplyRunnable> results = new GetAdapterTask(this, request);

  BluetoothService* bs = BluetoothService::Get();
  NS_ENSURE_TRUE(bs, NS_ERROR_FAILURE);
  if (NS_FAILED(bs->GetDefaultAdapterPathInternal(results))) {
    return NS_ERROR_FAILURE;
  }

  request.forget(aAdapter);
  return NS_OK;
}

// static
already_AddRefed<BluetoothManager>
BluetoothManager::Create(nsPIDOMWindow* aWindow)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aWindow);

  nsRefPtr<BluetoothManager> manager = new BluetoothManager(aWindow);
  return manager.forget();
}

nsresult
NS_NewBluetoothManager(nsPIDOMWindow* aWindow,
                       nsIDOMBluetoothManager** aBluetoothManager)
{
  NS_ASSERTION(aWindow, "Null pointer!");

  nsCOMPtr<nsIPermissionManager> permMgr =
    do_GetService(NS_PERMISSIONMANAGER_CONTRACTID);
  NS_ENSURE_TRUE(permMgr, NS_ERROR_UNEXPECTED);

  uint32_t permission;
  nsresult rv =
    permMgr->TestPermissionFromWindow(aWindow, "bluetooth",
                                      &permission);
  NS_ENSURE_SUCCESS(rv, rv);

  nsRefPtr<BluetoothManager> bluetoothManager;

  if (permission == nsIPermissionManager::ALLOW_ACTION) {
    bluetoothManager = BluetoothManager::Create(aWindow);
  }

  bluetoothManager.forget(aBluetoothManager);
  return NS_OK;
}

void
BluetoothManager::Notify(const BluetoothSignal& aData)
{
  LOG("[M] %s - '%s'", __FUNCTION__, NS_ConvertUTF16toUTF8(aData.name()).get());

  if (aData.name().EqualsLiteral("AdapterAdded")) {
    DispatchTrustedEvent(NS_LITERAL_STRING("adapteradded"));
  } else if (aData.name().EqualsLiteral("Enabled")) {
    DispatchTrustedEvent(NS_LITERAL_STRING("enabled"));
  } else if (aData.name().EqualsLiteral("Disabled")) {
    DispatchTrustedEvent(NS_LITERAL_STRING("disabled"));
  } else {
#ifdef DEBUG
    nsCString warningMsg;
    warningMsg.AssignLiteral("Not handling manager signal: ");
    warningMsg.Append(NS_ConvertUTF16toUTF8(aData.name()));
    NS_WARNING(warningMsg.get());
#endif
  }
}

NS_IMETHODIMP
BluetoothManager::IsConnected(uint16_t aProfileId, bool* aConnected)
{
  LOG("[M] %s(%d)", __FUNCTION__, aProfileId);
  BluetoothService* bs = BluetoothService::Get();
  if (!bs) {
    NS_WARNING("BluetoothService not available!");
    return NS_ERROR_FAILURE;
  }

  *aConnected = bs->IsConnected(aProfileId);
  return NS_OK;
}

NS_IMPL_EVENT_HANDLER(BluetoothManager, enabled)
NS_IMPL_EVENT_HANDLER(BluetoothManager, disabled)
NS_IMPL_EVENT_HANDLER(BluetoothManager, adapteradded)

