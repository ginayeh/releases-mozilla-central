/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCRLManager.h"
#include "nsCRLInfo.h"

#include "nsCOMPtr.h"
#include "nsComponentManagerUtils.h"
#include "nsReadableUtils.h"
#include "nsNSSComponent.h"
#include "nsCOMPtr.h"
#include "nsICertificateDialogs.h"
#include "nsIMutableArray.h"
#include "nsIPrefService.h"
#include "nsIPrefBranch.h"
#include "nsNSSShutDown.h"
#include "nsThreadUtils.h"

#include "nsNSSCertHeader.h"

#include "nspr.h"
extern "C" {
#include "pk11func.h"
#include "certdb.h"
#include "cert.h"
#include "secerr.h"
#include "nssb64.h"
#include "secasn1.h"
#include "secder.h"
}
#include "ssl.h"
#include "ocsp.h"
#include "plbase64.h"

static NS_DEFINE_CID(kNSSComponentCID, NS_NSSCOMPONENT_CID);

NS_IMPL_ISUPPORTS1(nsCRLManager, nsICRLManager)

nsCRLManager::nsCRLManager()
{
}

nsCRLManager::~nsCRLManager()
{
}

NS_IMETHODIMP 
nsCRLManager::ImportCrl (uint8_t *aData, uint32_t aLength, nsIURI * aURI, uint32_t aType, bool doSilentDownload, const PRUnichar* crlKey)
{
  if (!NS_IsMainThread()) {
    NS_ERROR("nsCRLManager::ImportCrl called off the main thread");
    return NS_ERROR_NOT_SAME_THREAD;
  }
  
  nsNSSShutDownPreventionLock locker;
  nsresult rv;
  PRArenaPool *arena = NULL;
  CERTCertificate *caCert;
  SECItem derName = { siBuffer, NULL, 0 };
  SECItem derCrl;
  CERTSignedData sd;
  SECStatus sec_rv;
  CERTSignedCrl *crl;
  nsAutoCString url;
  nsCOMPtr<nsICRLInfo> crlData;
  bool importSuccessful;
  int32_t errorCode;
  nsString errorMessage;
  
  nsCOMPtr<nsINSSComponent> nssComponent(do_GetService(kNSSComponentCID, &rv));
  if (NS_FAILED(rv)) return rv;
	         
  aURI->GetSpec(url);
  arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
  if (!arena) {
    goto loser;
  }
  memset(&sd, 0, sizeof(sd));

  derCrl.data = (unsigned char*)aData;
  derCrl.len = aLength;
  sec_rv = CERT_KeyFromDERCrl(arena, &derCrl, &derName);
  if (sec_rv != SECSuccess) {
    goto loser;
  }

  caCert = CERT_FindCertByName(CERT_GetDefaultCertDB(), &derName);
  if (!caCert) {
    if (aType == SEC_KRL_TYPE){
      goto loser;
    }
  } else {
    sec_rv = SEC_ASN1DecodeItem(arena,
                            &sd, SEC_ASN1_GET(CERT_SignedDataTemplate), 
                            &derCrl);
    if (sec_rv != SECSuccess) {
      goto loser;
    }
    sec_rv = CERT_VerifySignedData(&sd, caCert, PR_Now(),
                               nullptr);
    if (sec_rv != SECSuccess) {
      goto loser;
    }
  }
  
  crl = SEC_NewCrl(CERT_GetDefaultCertDB(), const_cast<char*>(url.get()), &derCrl,
                   aType);
  
  if (!crl) {
    goto loser;
  }

  crlData = new nsCRLInfo(crl);
  SSL_ClearSessionCache();
  SEC_DestroyCrl(crl);
  
  importSuccessful = true;
  goto done;

loser:
  importSuccessful = false;
  errorCode = PR_GetError();
  switch (errorCode) {
    case SEC_ERROR_CRL_EXPIRED:
      nssComponent->GetPIPNSSBundleString("CrlImportFailureExpired", errorMessage);
      break;

	case SEC_ERROR_CRL_BAD_SIGNATURE:
      nssComponent->GetPIPNSSBundleString("CrlImportFailureBadSignature", errorMessage);
      break;

	case SEC_ERROR_CRL_INVALID:
      nssComponent->GetPIPNSSBundleString("CrlImportFailureInvalid", errorMessage);
      break;

	case SEC_ERROR_OLD_CRL:
      nssComponent->GetPIPNSSBundleString("CrlImportFailureOld", errorMessage);
      break;

	case SEC_ERROR_CRL_NOT_YET_VALID:
      nssComponent->GetPIPNSSBundleString("CrlImportFailureNotYetValid", errorMessage);
      break;

    default:
      nssComponent->GetPIPNSSBundleString("CrlImportFailureReasonUnknown", errorMessage);
      errorMessage.AppendInt(errorCode,16);
      break;
  }

done:
          
  if(!doSilentDownload){
    if (!importSuccessful){
      nsString message;
      nsString temp;
      nssComponent->GetPIPNSSBundleString("CrlImportFailure1x", message);
      message.Append(NS_LITERAL_STRING("\n").get());
      message.Append(errorMessage);
      nssComponent->GetPIPNSSBundleString("CrlImportFailure2", temp);
      message.Append(NS_LITERAL_STRING("\n").get());
      message.Append(temp);

      nsNSSComponent::ShowAlertWithConstructedString(message);
    } else {
      nsCOMPtr<nsICertificateDialogs> certDialogs;
      // Not being able to display the success dialog should not
      // be a fatal error, so don't return a failure code.
      {
        nsPSMUITracker tracker;
        if (tracker.isUIForbidden()) {
          rv = NS_ERROR_NOT_AVAILABLE;
        }
        else {
          rv = ::getNSSDialogs(getter_AddRefs(certDialogs),
            NS_GET_IID(nsICertificateDialogs), NS_CERTIFICATEDIALOGS_CONTRACTID);
        }
      }
      if (NS_SUCCEEDED(rv)) {
        nsCOMPtr<nsIInterfaceRequestor> cxt = new PipUIContext();
        certDialogs->CrlImportStatusDialog(cxt, crlData);
      }
    }
  } else {
    if(crlKey == nullptr){
      return NS_ERROR_FAILURE;
    }
    nsCOMPtr<nsIPrefService> prefSvc = do_GetService(NS_PREFSERVICE_CONTRACTID,&rv);
    nsCOMPtr<nsIPrefBranch> pref = do_GetService(NS_PREFSERVICE_CONTRACTID,&rv);
    if (NS_FAILED(rv)){
      return rv;
    }
    
    nsAutoCString updateErrCntPrefStr(CRL_AUTOUPDATE_ERRCNT_PREF);
    LossyAppendUTF16toASCII(crlKey, updateErrCntPrefStr);
    if(importSuccessful){
      PRUnichar *updateTime;
      nsAutoCString updateTimeStr;
      nsCString updateURL;
      int32_t timingTypePref;
      double dayCnt;
      char *dayCntStr;
      nsAutoCString updateTypePrefStr(CRL_AUTOUPDATE_TIMIINGTYPE_PREF);
      nsAutoCString updateTimePrefStr(CRL_AUTOUPDATE_TIME_PREF);
      nsAutoCString updateUrlPrefStr(CRL_AUTOUPDATE_URL_PREF);
      nsAutoCString updateDayCntPrefStr(CRL_AUTOUPDATE_DAYCNT_PREF);
      nsAutoCString updateFreqCntPrefStr(CRL_AUTOUPDATE_FREQCNT_PREF);
      LossyAppendUTF16toASCII(crlKey, updateTypePrefStr);
      LossyAppendUTF16toASCII(crlKey, updateTimePrefStr);
      LossyAppendUTF16toASCII(crlKey, updateUrlPrefStr);
      LossyAppendUTF16toASCII(crlKey, updateDayCntPrefStr);
      LossyAppendUTF16toASCII(crlKey, updateFreqCntPrefStr);

      pref->GetIntPref(updateTypePrefStr.get(),&timingTypePref);
      
      //Compute and update the next download instant
      if(timingTypePref == TYPE_AUTOUPDATE_TIME_BASED){
        pref->GetCharPref(updateDayCntPrefStr.get(),&dayCntStr);
      }else{
        pref->GetCharPref(updateFreqCntPrefStr.get(),&dayCntStr);
      }
      dayCnt = atof(dayCntStr);
      nsMemory::Free(dayCntStr);

      bool toBeRescheduled = false;
      if(NS_SUCCEEDED(ComputeNextAutoUpdateTime(crlData, timingTypePref, dayCnt, &updateTime))){
        updateTimeStr.AssignWithConversion(updateTime);
        pref->SetCharPref(updateTimePrefStr.get(),updateTimeStr.get());
        //Now, check if this update time is already in the past. This would
        //imply we have downloaded the same crl, or there is something wrong
        //with the next update date. We will not reschedule this crl in this
        //session anymore - or else, we land into a loop. It would anyway be
        //imported once the browser is restarted.
        if(int64_t(updateTime) > int64_t(PR_Now())){
          toBeRescheduled = true;
        }
        nsMemory::Free(updateTime);
      }
      
      //Update the url to download from, next time
      crlData->GetLastFetchURL(updateURL);
      pref->SetCharPref(updateUrlPrefStr.get(),updateURL.get());
      
      pref->SetIntPref(updateErrCntPrefStr.get(),0);
      
      if (toBeRescheduled) {
        nsAutoString hashKey(crlKey);
        nssComponent->RemoveCrlFromList(hashKey);
        nssComponent->DefineNextTimer();
      }

    } else{
      int32_t errCnt;
      nsAutoCString errMsg;
      nsAutoCString updateErrDetailPrefStr(CRL_AUTOUPDATE_ERRDETAIL_PREF);
      LossyAppendUTF16toASCII(crlKey, updateErrDetailPrefStr);
      errMsg.AssignWithConversion(errorMessage.get());
      rv = pref->GetIntPref(updateErrCntPrefStr.get(),&errCnt);
      if(NS_FAILED(rv))
        errCnt = 0;

      pref->SetIntPref(updateErrCntPrefStr.get(),errCnt+1);
      pref->SetCharPref(updateErrDetailPrefStr.get(),errMsg.get());
    }
    prefSvc->SavePrefFile(nullptr);
  }

  return rv;
}

NS_IMETHODIMP 
nsCRLManager::UpdateCRLFromURL( const PRUnichar *url, const PRUnichar* key, bool *res)
{
  nsresult rv;
  nsAutoString downloadUrl(url);
  nsAutoString dbKey(key);
  nsCOMPtr<nsINSSComponent> nssComponent(do_GetService(kNSSComponentCID, &rv));
  if(NS_FAILED(rv)){
    *res = false;
    return rv;
  }

  rv = nssComponent->DownloadCRLDirectly(downloadUrl, dbKey);
  if(NS_FAILED(rv)){
    *res = false;
  } else {
    *res = true;
  }
  return NS_OK;

}

NS_IMETHODIMP 
nsCRLManager::RescheduleCRLAutoUpdate(void)
{
  nsresult rv;
  nsCOMPtr<nsINSSComponent> nssComponent(do_GetService(kNSSComponentCID, &rv));
  if(NS_FAILED(rv)){
    return rv;
  }
  rv = nssComponent->DefineNextTimer();
  return rv;
}

/**
 * getCRLs
 *
 * Export a set of certs and keys from the database to a PKCS#12 file.
 */
NS_IMETHODIMP 
nsCRLManager::GetCrls(nsIArray ** aCrls)
{
  nsNSSShutDownPreventionLock locker;
  SECStatus sec_rv;
  CERTCrlHeadNode *head = nullptr;
  CERTCrlNode *node = nullptr;
  nsresult rv;
  nsCOMPtr<nsIMutableArray> crlsArray =
    do_CreateInstance(NS_ARRAY_CONTRACTID, &rv);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Get the list of certs //
  sec_rv = SEC_LookupCrls(CERT_GetDefaultCertDB(), &head, -1);
  if (sec_rv != SECSuccess) {
    return NS_ERROR_FAILURE;
  }

  if (head) {
    for (node=head->first; node != nullptr; node = node->next) {

      nsCOMPtr<nsICRLInfo> entry = new nsCRLInfo((node->crl));
      crlsArray->AppendElement(entry, false);
    }
    PORT_FreeArena(head->arena, false);
  }

  *aCrls = crlsArray;
  NS_IF_ADDREF(*aCrls);
  return NS_OK;
}

/**
 * deleteCrl
 *
 * Delete a Crl entry from the cert db.
 */
NS_IMETHODIMP 
nsCRLManager::DeleteCrl(uint32_t aCrlIndex)
{
  nsNSSShutDownPreventionLock locker;
  CERTSignedCrl *realCrl = nullptr;
  CERTCrlHeadNode *head = nullptr;
  CERTCrlNode *node = nullptr;
  SECStatus sec_rv;
  uint32_t i;

  // Get the list of certs //
  sec_rv = SEC_LookupCrls(CERT_GetDefaultCertDB(), &head, -1);
  if (sec_rv != SECSuccess) {
    return NS_ERROR_FAILURE;
  }

  if (head) {
    for (i = 0, node=head->first; node != nullptr; i++, node = node->next) {
      if (i != aCrlIndex) {
        continue;
      }
      realCrl = SEC_FindCrlByName(CERT_GetDefaultCertDB(), &(node->crl->crl.derName), node->type);
      SEC_DeletePermCRL(realCrl);
      SEC_DestroyCrl(realCrl);
      SSL_ClearSessionCache();
    }
    PORT_FreeArena(head->arena, false);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsCRLManager::ComputeNextAutoUpdateTime(nsICRLInfo *info, 
  uint32_t autoUpdateType, double dayCnt, PRUnichar **nextAutoUpdate)
{
  if (!info)
    return NS_ERROR_FAILURE;
  NS_ENSURE_ARG_POINTER(nextAutoUpdate);

  PRTime microsecInDayCnt;
  PRTime now = PR_Now();
  PRTime tempTime;
  int64_t diff = 0;
  int64_t secsInDay = 86400UL;
  int64_t temp;
  int64_t cycleCnt = 0;
  int64_t secsInDayCnt;
  double tmpData;
  
  LL_L2F(tmpData,secsInDay);
  LL_MUL(tmpData,dayCnt,tmpData);
  LL_F2L(secsInDayCnt,tmpData);
  LL_MUL(microsecInDayCnt, secsInDayCnt, PR_USEC_PER_SEC);
    
  PRTime lastUpdate;
  PRTime nextUpdate;
  
  nsresult rv;

  rv = info->GetLastUpdate(&lastUpdate);
  if (NS_FAILED(rv))
    return rv;

  rv = info->GetNextUpdate(&nextUpdate);
  if (NS_FAILED(rv))
    return rv;

  switch (autoUpdateType) {
  case TYPE_AUTOUPDATE_FREQ_BASED:
    LL_SUB(diff, now, lastUpdate);             //diff is the no of micro sec between now and last update
    LL_DIV(cycleCnt, diff, microsecInDayCnt);   //temp is the number of full cycles from lst update
    LL_MOD(temp, diff, microsecInDayCnt);
    if(temp != 0) {
      LL_ADD(cycleCnt,cycleCnt,1);            //no of complete cycles till next autoupdate instant
    }
    LL_MUL(temp,cycleCnt,microsecInDayCnt);    //micro secs from last update
    LL_ADD(tempTime, lastUpdate, temp);
    break;  
  case TYPE_AUTOUPDATE_TIME_BASED:
    LL_SUB(tempTime, nextUpdate, microsecInDayCnt);
    break;
  default:
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  //Now, a basic constraing is that the next auto update date can never be after
  //next update, if one is defined
  if(nextUpdate > 0) {
    if(tempTime > nextUpdate) {
      tempTime = nextUpdate;
    }
  }

  // Return value as string; no pref type for Int64/PRTime
  char *tempTimeStr = PR_smprintf("%lli", tempTime);
  *nextAutoUpdate = ToNewUnicode(nsDependentCString(tempTimeStr));
  PR_smprintf_free(tempTimeStr);

  return NS_OK;
}

