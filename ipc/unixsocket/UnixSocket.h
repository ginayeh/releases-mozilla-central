/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ipc_UnixSocket_h
#define mozilla_ipc_UnixSocket_h

#include <stdlib.h>
#include "nsString.h"
#include "nsAutoPtr.h"
#include "mozilla/RefPtr.h"

namespace mozilla {
namespace ipc {

struct UnixSocketRawData
{
  static const size_t MAX_DATA_SIZE = 1024;
  uint8_t mData[MAX_DATA_SIZE];

  // Number of octets in mData.
  size_t mSize;
  size_t mCurrentWriteOffset;

  UnixSocketRawData() :
    mSize(0),
    mCurrentWriteOffset(0)
  {
  }

  UnixSocketRawData(const uint8_t* aData, int aSize) :
    mSize(aSize),
    mCurrentWriteOffset(0)
  {
    memcpy(mData, aData, aSize);
  }

};

class UnixSocketImpl;

class UnixSocketConnector
{
public:
  UnixSocketConnector()
  {

  }
  virtual ~UnixSocketConnector()
  {

  }
  virtual int Create() = 0;
  virtual bool Connect(int aFd, const char* aAddress) = 0;
protected:
  bool Prepare(int aFd);
};

class UnixSocketConsumer : public RefCounted<UnixSocketConsumer>
{
public:
  UnixSocketConsumer()
  {}
  virtual ~UnixSocketConsumer()
  {
  }
  virtual void ReceiveSocketData(UnixSocketRawData* aMessage) = 0;
  bool SendSocketData(UnixSocketRawData* aMessage);
  bool SendSocketData(const nsAString& aMessage);
  bool SendSocketData(const nsACString& aMessage);
  bool ConnectSocket(UnixSocketConnector& aConnector, const char* aAddress);
  void CloseSocket();
  bool IsSocketOpen();
private:
  UnixSocketImpl* mImpl;
};

} // namespace ipc
} // namepsace mozilla

#endif // mozilla_ipc_Socket_h
