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

#include "nsContentUtils.h"
#include "nsDOMClassInfo.h"
#include "nsDOMEvent.h"
#include "nsIDOMDOMRequest.h"
#include "nsIPermissionManager.h"
#include "nsThreadUtils.h"
#include "nsXPCOMCIDInternal.h"
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

#undef LOGV
#if defined(MOZ_WIDGET_GONK)
#include <android/log.h>
#define LOGV(args...)  __android_log_print(ANDROID_LOG_INFO, "GonkDBusV", args);
#else
#define BTDEBUG true
#define LOGV(args...) if (BTDEBUG) printf(args);
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
    nsCOMPtr<nsIDOMBluetoothAdapter> adapter;
    *aValue = JSVAL_VOID;

    const InfallibleTArray<BluetoothNamedValue>& v =
      mReply->get_BluetoothReplySuccess().value().get_ArrayOfBluetoothNamedValue();
    adapter = BluetoothAdapter::Create(mManagerPtr->GetOwner(), v);

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
    bool result = NS_SUCCEEDED(rv);
    if (!result) {
      NS_WARNING("Cannot create native object!");
      SetError(NS_LITERAL_STRING("BluetoothNativeObjectError"));
    }

    return result;
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

class ToggleBtResultTask : public nsRunnable
{
public:
  ToggleBtResultTask(BluetoothManager* aManager, bool aEnabled)
    : mManagerPtr(aManager),
      mEnabled(aEnabled)
  {
  }

  NS_IMETHOD Run()
  {
    LOG("[M] ToggleBtResultTask::Run");
    MOZ_ASSERT(NS_IsMainThread());

    mManagerPtr->FireEnabledDisabledEvent(mEnabled);

    // mManagerPtr must be null before returning to prevent the background
    // thread from racing to release it during the destruction of this runnable.
    mManagerPtr = nullptr;

    return NS_OK;
  }

private:
  nsRefPtr<BluetoothManager> mManagerPtr;
  bool mEnabled;
};

nsresult
BluetoothManager::FireEnabledDisabledEvent(bool aEnabled)
{
  LOG("[M] %s", __FUNCTION__);
  nsString eventName;

  if (aEnabled) {
    eventName.AssignLiteral("enabled");
  } else {
    eventName.AssignLiteral("disabled");
  }

  nsRefPtr<nsDOMEvent> event = new nsDOMEvent(nullptr, nullptr);
  nsresult rv = event->InitEvent(eventName, false, false);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = event->SetTrusted(true);
  NS_ENSURE_SUCCESS(rv, rv);

  bool dummy;
  rv = DispatchEvent(event, &dummy);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

BluetoothManager::BluetoothManager(nsPIDOMWindow *aWindow)
: BluetoothPropertyContainer(BluetoothObjectType::TYPE_MANAGER)
{
  LOG("[M] %s", __FUNCTION__);
  MOZ_ASSERT(aWindow);

  BindToOwner(aWindow);
  mPath.AssignLiteral("/");
}

BluetoothManager::~BluetoothManager()
{
  LOG("[M] %s", __FUNCTION__);
  BluetoothService* bs = BluetoothService::Get();
  if (bs) {
    bs->UnregisterManager(this);
  }
}

static void PrintProperty(const nsAString& aName, const BluetoothValue& aValue);

void
PrintProperty(const nsAString& aName, const BluetoothValue& aValue)
{
  if (aValue.type() == BluetoothValue::TnsString) {
    LOGV("[M] %s, <%s, %s>", __FUNCTION__, NS_ConvertUTF16toUTF8(aName).get(), NS_ConvertUTF16toUTF8(aValue.get_nsString()).get());
    return;
  } else if (aValue.type() == BluetoothValue::Tuint32_t) {
    LOGV("[M] %s, <%s, %d>", __FUNCTION__, NS_ConvertUTF16toUTF8(aName).get(), aValue.get_uint32_t());
    return;
  } else if (aValue.type() == BluetoothValue::Tbool) {
    LOGV("[M] %s, <%s, %d>", __FUNCTION__, NS_ConvertUTF16toUTF8(aName).get(), aValue.get_bool());
    return;
  } else if (aValue.type() == BluetoothValue::TArrayOfBluetoothNamedValue) {
    LOGV("[M] %s, <%s, Array of BluetoothNamedValue>", __FUNCTION__, NS_ConvertUTF16toUTF8(aName).get());
//    PrintProperty(aName, aValue);
    return;
  } else if (aValue.type() == BluetoothValue::TArrayOfnsString) {
    nsTArray<nsString> tmp = aValue.get_ArrayOfnsString();
    for (int i = 0; i < tmp.Length(); i++) {
      LOGV("[M] %s, <%s, %s>", __FUNCTION__, NS_ConvertUTF16toUTF8(aName).get(), NS_ConvertUTF16toUTF8(tmp[i]).get());
    }
    return;
//  } else if (aValue.type() == BluetoothValue::TArrayOfuint8_t) {
//    LOGV("[M] %s, <%s, %d>", __FUNCTION__, NS_ConvertUTF16toUTF8(aName).get(), aValue.get_ArrayOfuint8_t()[0]);
  } else {
    LOGV("[M] %s, <%s, Unknown value type>", __FUNCTION__, NS_ConvertUTF16toUTF8(aName).get());
    return;
  }
}

void
BluetoothManager::SetPropertyByValue(const BluetoothNamedValue& aValue)
{
  PrintProperty(aValue.name(), aValue.value());
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
  LOGV("[M] %s", __FUNCTION__);
  BluetoothService* bs = BluetoothService::Get();
  if (!bs) {
    NS_WARNING("BluetoothService not available!");
    return NS_ERROR_FAILURE;
  }

  *aEnabled = bs->IsEnabled();
  return NS_OK;
}

NS_IMETHODIMP
BluetoothManager::GetDefaultAdapter(nsIDOMDOMRequest** aAdapter)
{
  LOG("[M] %s", __FUNCTION__);
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

  nsRefPtr<BluetoothReplyRunnable> results = new GetAdapterTask(this, request);

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
  LOG("[M] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aWindow);

  BluetoothService* bs = BluetoothService::Get();
  if (!bs) {
    NS_WARNING("BluetoothService not available!");
    return nullptr;
  }

  nsRefPtr<BluetoothManager> manager = new BluetoothManager(aWindow);

  bs->RegisterManager(manager);

  return manager.forget();
}

nsresult
NS_NewBluetoothManager(nsPIDOMWindow* aWindow,
                       nsIDOMBluetoothManager** aBluetoothManager)
{
  LOG("[M] %s", __FUNCTION__);
  NS_ASSERTION(aWindow, "Null pointer!");

  nsPIDOMWindow* innerWindow = aWindow->IsInnerWindow() ?
    aWindow :
    aWindow->GetCurrentInnerWindow();

  // Need the document for security check.
  nsCOMPtr<nsIDocument> document = innerWindow->GetExtantDoc();
  NS_ENSURE_TRUE(document, NS_NOINTERFACE);

  nsCOMPtr<nsIPrincipal> principal = document->NodePrincipal();
  NS_ENSURE_TRUE(principal, NS_ERROR_UNEXPECTED);

  nsCOMPtr<nsIPermissionManager> permMgr =
    do_GetService(NS_PERMISSIONMANAGER_CONTRACTID);
  NS_ENSURE_TRUE(permMgr, NS_ERROR_UNEXPECTED);

  uint32_t permission;
  nsresult rv =
    permMgr->TestPermissionFromPrincipal(principal, "mozBluetooth",
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
  LOG("[M] %s", __FUNCTION__);
  if (aData.name().EqualsLiteral("AdapterAdded")) {
    LOG("[M] Receive event AdapterAdded");
    nsRefPtr<nsDOMEvent> event = new nsDOMEvent(nullptr, nullptr);
    nsresult rv = event->InitEvent(NS_LITERAL_STRING("adapteradded"), false, false);

    if (NS_FAILED(rv)) {
      NS_WARNING("Failed to init the adapteradded event!!!");
      return;
    }

    event->SetTrusted(true);
    bool dummy;
    DispatchEvent(event, &dummy);
  } else {
#ifdef DEBUG
    nsCString warningMsg;
    warningMsg.AssignLiteral("Not handling manager signal: ");
    warningMsg.Append(NS_ConvertUTF16toUTF8(aData.name()));
    NS_WARNING(warningMsg.get());
#endif
  }
}

NS_IMPL_EVENT_HANDLER(BluetoothManager, enabled)
NS_IMPL_EVENT_HANDLER(BluetoothManager, disabled)
NS_IMPL_EVENT_HANDLER(BluetoothManager, adapteradded)
