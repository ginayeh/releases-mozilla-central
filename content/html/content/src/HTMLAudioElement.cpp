/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/HTMLAudioElement.h"
#include "mozilla/dom/HTMLAudioElementBinding.h"
#include "nsError.h"
#include "nsIDOMHTMLAudioElement.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsIDocument.h"
#include "jsfriendapi.h"
#include "nsContentUtils.h"
#include "nsJSUtils.h"
#include "AudioSampleFormat.h"
#include "AudioChannelCommon.h"
#include <algorithm>
#include "mozilla/Preferences.h"
#include "mozilla/dom/EnableWebAudioCheck.h"

static bool
IsAudioAPIEnabled()
{
  return mozilla::Preferences::GetBool("media.audio_data.enabled", true);
}

NS_IMPL_NS_NEW_HTML_ELEMENT(Audio)

namespace mozilla {
namespace dom {

NS_IMPL_ADDREF_INHERITED(HTMLAudioElement, HTMLMediaElement)
NS_IMPL_RELEASE_INHERITED(HTMLAudioElement, HTMLMediaElement)

NS_INTERFACE_TABLE_HEAD(HTMLAudioElement)
NS_HTML_CONTENT_INTERFACE_TABLE2(HTMLAudioElement, nsIDOMHTMLMediaElement,
                                 nsIDOMHTMLAudioElement)
NS_HTML_CONTENT_INTERFACE_TABLE_TO_MAP_SEGUE(HTMLAudioElement,
                                             HTMLMediaElement)
NS_HTML_CONTENT_INTERFACE_MAP_END

NS_IMPL_ELEMENT_CLONE(HTMLAudioElement)


HTMLAudioElement::HTMLAudioElement(already_AddRefed<nsINodeInfo> aNodeInfo)
  : HTMLMediaElement(aNodeInfo)
{
  SetIsDOMBinding();
}

HTMLAudioElement::~HTMLAudioElement()
{
}


already_AddRefed<HTMLAudioElement>
HTMLAudioElement::Audio(const GlobalObject& aGlobal, ErrorResult& aRv)
{
  nsCOMPtr<nsPIDOMWindow> win = do_QueryInterface(aGlobal.Get());
  nsIDocument* doc;
  if (!win || !(doc = win->GetExtantDoc())) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  nsCOMPtr<nsINodeInfo> nodeInfo =
    doc->NodeInfoManager()->GetNodeInfo(nsGkAtoms::audio, nullptr,
                                        kNameSpaceID_XHTML,
                                        nsIDOMNode::ELEMENT_NODE);

  nsRefPtr<HTMLAudioElement> audio = new HTMLAudioElement(nodeInfo.forget());
  audio->SetHTMLAttr(nsGkAtoms::preload, NS_LITERAL_STRING("auto"), aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  return audio.forget();
}

already_AddRefed<HTMLAudioElement>
HTMLAudioElement::Audio(const GlobalObject& aGlobal, const nsAString& aSrc, ErrorResult& aRv)
{
  nsRefPtr<HTMLAudioElement> audio = Audio(aGlobal, aRv);
  if (audio) {
    aRv = audio->SetSrc(aSrc);
  }

  return audio.forget();
}

void
HTMLAudioElement::MozSetup(uint32_t aChannels, uint32_t aRate, ErrorResult& aRv)
{
  if (!IsAudioAPIEnabled()) {
    aRv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
    return;
  }

  if (dom::EnableWebAudioCheck::PrefEnabled()) {
    OwnerDoc()->WarnOnceAbout(nsIDocument::eMozAudioData);
  }

  // If there is already a src provided, don't setup another stream
  if (mDecoder) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  // MozWriteAudio divides by mChannels, so validate now.
  if (0 == aChannels) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  if (mAudioStream) {
    mAudioStream->Shutdown();
  }

  mAudioStream = AudioStream::AllocateStream();
  aRv = mAudioStream->Init(aChannels, aRate, mAudioChannelType);
  if (aRv.Failed()) {
    mAudioStream->Shutdown();
    mAudioStream = nullptr;
    return;
  }

  MetadataLoaded(aChannels, aRate, true, false, nullptr);
  mAudioStream->SetVolume(mVolume);
}

NS_IMETHODIMP
HTMLAudioElement::MozSetup(uint32_t aChannels, uint32_t aRate)
{
  ErrorResult rv;
  MozSetup(aChannels, aRate, rv);
  return rv.ErrorCode();
}

uint32_t
HTMLAudioElement::MozWriteAudio(JSContext* aCx, JS::Value aData, ErrorResult& aRv)
{
  if (!IsAudioAPIEnabled()) {
    aRv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
    return 0;
  }

  if (!mAudioStream) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return 0;
  }

  if (!aData.isObject()) {
    aRv.Throw(NS_ERROR_DOM_TYPE_MISMATCH_ERR);
    return 0;
  }

  JSObject* darray = &aData.toObject();
  JS::AutoObjectRooter tvr(aCx);
  JSObject* tsrc = nullptr;

  // Allow either Float32Array or plain JS Array
  if (JS_IsFloat32Array(darray)) {
    tsrc = darray;
  } else if (JS_IsArrayObject(aCx, darray)) {
    JSObject* nobj = JS_NewFloat32ArrayFromArray(aCx, darray);
    if (!nobj) {
      aRv.Throw(NS_ERROR_DOM_TYPE_MISMATCH_ERR);
      return 0;
    }
    tsrc = nobj;
  } else {
    aRv.Throw(NS_ERROR_DOM_TYPE_MISMATCH_ERR);
    return 0;
  }
  tvr.setObject(tsrc);

  uint32_t dataLength = JS_GetTypedArrayLength(tsrc);

  // Make sure that we are going to write the correct amount of data based
  // on number of channels.
  if (dataLength % mChannels != 0) {
    aRv.Throw(NS_ERROR_DOM_INDEX_SIZE_ERR);
    return 0;
  }

  // Don't write more than can be written without blocking.
  uint32_t writeLen = std::min(mAudioStream->Available(), dataLength / mChannels);

  float* frames = JS_GetFloat32ArrayData(tsrc);
  // Convert the samples back to integers as we are using fixed point audio in
  // the AudioStream.
  // This could be optimized to avoid allocation and memcpy when
  // AudioDataValue is 'float', but it's not worth it for this deprecated API.
  nsAutoArrayPtr<AudioDataValue> audioData(new AudioDataValue[writeLen * mChannels]);
  ConvertAudioSamples(frames, audioData.get(), writeLen * mChannels);
  aRv = mAudioStream->Write(audioData.get(), writeLen);
  if (aRv.Failed()) {
    return 0;
  }
  mAudioStream->Start();

  // Return the actual amount written.
  return writeLen * mChannels;
}

NS_IMETHODIMP
HTMLAudioElement::MozWriteAudio(const JS::Value& aData, JSContext* aCx, uint32_t* aRetVal)
{
  ErrorResult rv;
  *aRetVal = MozWriteAudio(aCx, aData, rv);
  return rv.ErrorCode();
}

uint64_t
HTMLAudioElement::MozCurrentSampleOffset(ErrorResult& aRv)
{
  if (!IsAudioAPIEnabled()) {
    aRv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
    return 0;
  }

  if (!mAudioStream) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return 0;
  }

  int64_t position = mAudioStream->GetPositionInFrames();
  if (position < 0) {
    return 0;
  }

  return position * mChannels;
}

NS_IMETHODIMP
HTMLAudioElement::MozCurrentSampleOffset(uint64_t *aRetVal)
{
  ErrorResult rv;
  *aRetVal = MozCurrentSampleOffset(rv);
  return rv.ErrorCode();
}

nsresult HTMLAudioElement::SetAcceptHeader(nsIHttpChannel* aChannel)
{
    nsAutoCString value(
#ifdef MOZ_WEBM
      "audio/webm,"
#endif
#ifdef MOZ_OGG
      "audio/ogg,"
#endif
#ifdef MOZ_WAVE
      "audio/wav,"
#endif
      "audio/*;q=0.9,"
#ifdef MOZ_OGG
      "application/ogg;q=0.7,"
#endif
      "video/*;q=0.6,*/*;q=0.5");

    return aChannel->SetRequestHeader(NS_LITERAL_CSTRING("Accept"),
                                      value,
                                      false);
}

JSObject*
HTMLAudioElement::WrapNode(JSContext* aCx, JSObject* aScope)
{
  return HTMLAudioElementBinding::Wrap(aCx, aScope, this);
}

} // namespace dom
} // namespace mozilla
