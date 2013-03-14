/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_BluetoothProfileManagerCollector_h
#define mozilla_dom_bluetooth_BluetoothProfileManagerCollector_h

#include "BluetoothCommon.h"
#include "mozilla/ipc/UnixSocket.h"

#include "nsClassHashtable.h"

using namespace mozilla::ipc;

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothProfileManager;

class BluetoothProfileManagerCollector
{
public:
  BluetoothProfileManagerCollector();
  ~BluetoothProfileManagerCollector();
  bool Find(uint16_t aProfileId, BluetoothProfileManager** aProfilePtr) const;
  void Add(uint16_t aProfileId, BluetoothProfileManager* aProfilePtr);
  void Delete(uint16_t aProfileId); 

  uint32_t Length() {
    return mBluetoothProfileManagerTable.Count();
  }

private:
  typedef nsClassHashtable<nsUint32HashKey, BluetoothProfileManager>
    BluetoothProfileManagerTable;

  BluetoothProfileManagerTable mBluetoothProfileManagerTable;
};

END_BLUETOOTH_NAMESPACE

#endif
