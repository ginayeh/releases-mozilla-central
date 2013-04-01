/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluetootha2dpmanager_h__
#define mozilla_dom_bluetooth_bluetootha2dpmanager_h__

#include "BluetoothCommon.h"
#include "BluetoothProfileManager.h"
#include "BluetoothRilListener.h"

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothReplyRunnable;

enum BluetoothA2dpState {
  SINK_DISCONNECTED = 0,
  SINK_CONNECTING = 1,
  SINK_CONNECTED = 2,
  SINK_PLAYING = 3
};
class BluetoothA2dpManager : public BluetoothProfileManager
{
public:
  ~BluetoothA2dpManager();
  static BluetoothA2dpManager* Get();
  bool Connect(const nsAString& aDeviceAddress);
  void Disconnect(const nsAString& aDeviceAddress);
  bool Listen();
  void GetConnectedSinkAddress(nsAString& aDeviceAddress);
  bool GetConnectionStatus();
  void ResetAudio();
  void NotifyMusicPlayStatus();
  void UpdateMetaData(const nsAString& aTitle, const nsAString& aArtist,
                      const nsAString& aAlbum, const nsAString& aMediaNumber,
                      const nsAString& aTotalMediaCount, const nsAString& aPlaytime,
                      BluetoothReplyRunnable* aRunnable);
  void HandleSinkPropertyChange(const nsAString& aDeviceObjectPath,
                                const nsAString& newState);
  void UpdateNotification(const nsAString& aDeviceObjectPath,
                          const uint16_t aEventid, const uint32_t aData, BluetoothReplyRunnable* aRunnable);
  void HandleCallStateChanged(uint16_t aCallState);
private:
  BluetoothA2dpManager();

  BluetoothA2dpState mCurrentSinkState;
  nsString mConnectedDeviceAddress;
  //AVRCP 1.3 fields
  nsString mTrackName;
  nsString mTrackNumber;
  nsString mArtist;
  nsString mAlbum;
  nsString mTotalMediaCount;
  nsString mPlaytime;
  uint32_t mDuration;
  uint32_t mPosition;
  uint32_t mPlayStatus;
  long mReportTime;
  //TODO:Add RIL listener for suspend/resume A2DP
  //For the reason HFP/A2DP usage switch, we need to force suspend A2DP
  nsAutoPtr<BluetoothRilListener> mListener;
};

END_BLUETOOTH_NAMESPACE

#endif
