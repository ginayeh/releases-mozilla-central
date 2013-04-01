/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BluetoothA2dpManager.h"
#include "BluetoothReplyRunnable.h"
#include "BluetoothService.h"
#include "gonk/AudioSystem.h"
#include "nsThreadUtils.h"
#include "BluetoothUtils.h"
#include <utils/String8.h>
#include <android/log.h>

#include "nsIAudioManager.h"
#include "nsIObserverService.h" 
#include "mozilla/dom/bluetooth/BluetoothTypes.h"

#define BLUETOOTH_A2DP_STATUS_CHANGED "bluetooth-a2dp-status-changed"

USING_BLUETOOTH_NAMESPACE

static const int STATUS_STOPPED = 0x00;
static const int STATUS_PLAYING = 0x01;
static const int STATUS_PAUSED = 0x02;
static const int STATUS_FWD_SEEK = 0x03;
static const int STATUS_REV_SEEK = 0x04;
static const int STATUS_ERROR = 0xFF;
static const int EVENT_PLAYSTATUS_CHANGED = 0x1;
static const int EVENT_TRACK_CHANGED = 0x2;
namespace {
  static nsAutoPtr<BluetoothA2dpManager> gBluetoothA2dpManager;
} // anonymous namespace

BluetoothA2dpManager::BluetoothA2dpManager()
{
  mConnectedDeviceAddress.Truncate();
}

BluetoothA2dpManager::~BluetoothA2dpManager()
{

}

/*static*/
BluetoothA2dpManager*
BluetoothA2dpManager::Get()
{
  MOZ_ASSERT(NS_IsMainThread());

  // If we already exist, exit early
  if (gBluetoothA2dpManager) {
    BT_LOG("return existing BluetoothA2dpManager");
    return gBluetoothA2dpManager;
  }

  gBluetoothA2dpManager = new BluetoothA2dpManager();
  BT_LOG("A new BluetoothA2dpManager");
  return gBluetoothA2dpManager;
};

static BluetoothA2dpState
ConvertSinkStringToState(const nsAString& aNewState)
{
  if (aNewState.EqualsLiteral("disonnected"))
    return BluetoothA2dpState::SINK_DISCONNECTED;
  if (aNewState.EqualsLiteral("connecting"))
    return BluetoothA2dpState::SINK_CONNECTING;
  if (aNewState.EqualsLiteral("connected"))
    return BluetoothA2dpState::SINK_CONNECTED;
  if (aNewState.EqualsLiteral("playing"))
    return BluetoothA2dpState::SINK_PLAYING;
    return BluetoothA2dpState::SINK_DISCONNECTED;
}

static void
SetParameter(const nsAString& aParameter)
{
  android::String8 cmd;
  cmd.appendFormat(NS_ConvertUTF16toUTF8(aParameter).get());
  android::AudioSystem::setParameters(0, cmd);
}

/*static void
SetupA2dpDevice(const nsAString& aDeviceAddress)
{
  SetParameter(NS_LITERAL_STRING("bluetooth_enabled=true"));
  SetParameter(NS_LITERAL_STRING("A2dpSuspended=false"));

  // Make a2dp device available
  android::AudioSystem::
    setDeviceConnectionState(AUDIO_DEVICE_OUT_BLUETOOTH_A2DP,
                             AUDIO_POLICY_DEVICE_STATE_AVAILABLE,
                             NS_ConvertUTF16toUTF8(aDeviceAddress).get());
}*/

void
NotifyAudioManager(const nsAString& aAddress)
{
  BT_LOG("[A2DP] %s", __FUNCTION__);
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIObserverService> obs =
    do_GetService("@mozilla.org/observer-service;1");
  NS_ENSURE_TRUE_VOID(obs);

  if (aAddress.IsEmpty()) {
    if (NS_FAILED(obs->NotifyObservers(nullptr, BLUETOOTH_A2DP_STATUS_CHANGED, nullptr))) {
      NS_WARNING("Failed to notify bluetooth-a2dp-status-changed observsers!");
      return;
    }
  } else {
    if (NS_FAILED(obs->NotifyObservers(nullptr, BLUETOOTH_A2DP_STATUS_CHANGED, aAddress.BeginReading()))) {
      NS_WARNING("Failed to notify bluetooth-a2dp-status-changed observsers!");
      return;
    }
  }
} 

/*static void
TeardownA2dpDevice(const nsAString& aDeviceAddress)
{
  SetParameter(NS_LITERAL_STRING("bluetooth_enabled=false"));
  SetParameter(NS_LITERAL_STRING("A2dpSuspended=true"));

  // Make a2dp device unavailable
  android::AudioSystem::
    setDeviceConnectionState(AUDIO_DEVICE_OUT_BLUETOOTH_A2DP,
                             AUDIO_POLICY_DEVICE_STATE_UNAVAILABLE,
                             NS_ConvertUTF16toUTF8(aDeviceAddress).get());
}*/

static void
RouteA2dpAudioPath()
{
  SetParameter(NS_LITERAL_STRING("bluetooth_enabled=true"));
  SetParameter(NS_LITERAL_STRING("A2dpSuspended=false"));
  android::AudioSystem::setForceUse((audio_policy_force_use_t)1,
      (audio_policy_forced_cfg_t)0);
}

/* HandleSinkPropertyChange stores current A2DP state
 * Possible values: "disconnected", "connecting","connected", "playing"
 * 1. "disconnected" -> "connecting"
 *  Either an incoming or outgoing connection
 *  attempt ongoing.
 * 2. "connecting" -> "disconnected"
 * Connection attempt failed
 * 3. "connecting" -> "connected"
 *     Successfully connected
 * 4. "connected" -> "playing"
 *     SCO audio connection successfully opened
 * 5. "playing" -> "connected"
 *     SCO audio connection closed
 * 6. "connected" -> "disconnected"
 * 7. "playing" -> "disconnected"
 *     Disconnected from the remote device
 */
void
BluetoothA2dpManager::HandleSinkPropertyChange(const nsAString& aDeviceObjectPath,
                         const nsAString& aNewState)
{

  if (aNewState.EqualsLiteral("connected")) {
    BT_LOG("A2DP connected!! Route path to a2dp");
    BT_LOG("Currnet device: %s",NS_ConvertUTF16toUTF8(mConnectedDeviceAddress).get());
    RouteA2dpAudioPath();
  }
  mCurrentSinkState = ConvertSinkStringToState(aNewState);
  //TODO: Need to check Sink state and do more stuffs
}

bool
BluetoothA2dpManager::Connect(const nsAString& aDeviceAddress)
{
  MOZ_ASSERT(NS_IsMainThread());

  if ((mConnectedDeviceAddress != aDeviceAddress) &&
      (!mConnectedDeviceAddress.IsEmpty())) {
    BT_LOG("[A2DP] Connection already exists");
    return false;
  }

  BluetoothService* bs = BluetoothService::Get();
  NS_ENSURE_TRUE(bs, false);

  if (!bs->ConnectSink(aDeviceAddress)) {
    BT_LOG("[A2DP] Connect failed!");
    return false;
  }

//  SetupA2dpDevice(aDeviceAddress);
  NotifyAudioManager(aDeviceAddress); 
  BT_LOG("[A2DP] Connect successfully!");

  mConnectedDeviceAddress = aDeviceAddress;
  BT_LOG("Connected Device address:%s", NS_ConvertUTF16toUTF8(mConnectedDeviceAddress).get() );
  return true;
}

void
BluetoothA2dpManager::Disconnect(const nsAString& aDeviceAddress)
{
  MOZ_ASSERT(NS_IsMainThread());

  if (mConnectedDeviceAddress.IsEmpty()) {
    return;
  }

  BluetoothService* bs = BluetoothService::Get();
  if (!bs->DisconnectSink(aDeviceAddress)) {
    BT_LOG("[A2DP] Disconnect failed!");
    return;
  }

//  TeardownA2dpDevice(aDeviceAddress);
  nsString address = NS_LITERAL_STRING("");
  NotifyAudioManager(address); 
  BT_LOG("[A2DP] Disconnect successfully!");

  mConnectedDeviceAddress.Truncate();
}

void
BluetoothA2dpManager::NotifyMusicPlayStatus()
{

  BT_LOG("%s", __FUNCTION__);
  nsAutoString type, name;
  InfallibleTArray<BluetoothNamedValue> parameters;
  type.AssignLiteral("bluetooth-avrcp-playstatus");
  
  if (!BroadcastSystemMessage(type, parameters)) 
    BT_LOG("fail to send system message");
}

void
BluetoothA2dpManager::UpdateMetaData(const nsAString& aTitle, const nsAString& aArtist,
                                     const nsAString& aAlbum,
                                     const nsAString& aMediaNumber,
                                     const nsAString& aTotalMediaCount,
                                     const nsAString& aPlaytime,
                                     BluetoothReplyRunnable* aRunnable)
{
}

void
BluetoothA2dpManager::GetConnectedSinkAddress(nsAString& aDeviceAddress)
{
  BT_LOG("mConnectedDeviceAddress: %s", NS_ConvertUTF16toUTF8(mConnectedDeviceAddress).get());
  aDeviceAddress = mConnectedDeviceAddress;
}

bool
BluetoothA2dpManager::GetConnectionStatus()
{
  if (mCurrentSinkState == BluetoothA2dpState::SINK_CONNECTED)
    return true;

  return false;
}

bool
BluetoothA2dpManager::Listen()
{
  BT_LOG("[A2DP] Listen");
  return true;
}

