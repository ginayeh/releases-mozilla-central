/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * The contents of this file are subject to the Netscape Public
 * License Version 1.1 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of
 * the License at http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation. All
 * Rights Reserved.
 *
 * Contributor(s): 
 *   Henrik Gemal <gemal@gemal.dk>
 *   Darin Fisher <darin@netscape.com>
 */

#include "nsAboutCache.h"
#include "nsIIOService.h"
#include "nsIServiceManager.h"
#include "nsIInputStream.h"
#include "nsIStorageStream.h"
#include "nsISimpleEnumerator.h"
#include "nsXPIDLString.h"
#include "nsIURI.h"
#include "nsCOMPtr.h"
#include "nsNetUtil.h"
#include "prtime.h"

#ifdef MOZ_NEW_CACHE
#include "nsICacheService.h"
#else
#include "nsICachedNetData.h"
#include "nsINetDataCacheRecord.h"
#endif


#ifdef MOZ_NEW_CACHE
NS_IMPL_ISUPPORTS2(nsAboutCache, nsIAboutModule, nsICacheVisitor)
#else
NS_IMPL_ISUPPORTS1(nsAboutCache, nsIAboutModule)
#endif

NS_IMETHODIMP
nsAboutCache::NewChannel(nsIURI *aURI, nsIChannel **result)
{
    nsresult rv;
    PRUint32 bytesWritten;

    *result = nsnull;
/*
    nsXPIDLCString path;
    rv = aURI->GetPath(getter_Copies(path));
    if (NS_FAILED(rv)) return rv;

    PRBool clear = PR_FALSE;
    PRBool leaks = PR_FALSE;

    nsCAutoString p(path);
    PRInt32 pos = p.Find("?");
    if (pos > 0) {
        nsCAutoString param;
        (void)p.Mid(param, pos+1, -1);
        if (param.Equals("new"))
            statType = nsTraceRefcnt::NEW_STATS;
        else if (param.Equals("clear"))
            clear = PR_TRUE;
        else if (param.Equals("leaks"))
            leaks = PR_TRUE;
    }
*/
    // Get the cache manager service
#ifdef MOZ_NEW_CACHE
    NS_WITH_SERVICE(nsICacheService, cacheService,
                    NS_CACHESERVICE_CONTRACTID, &rv);
#else
    NS_WITH_SERVICE(nsINetDataCacheManager, cacheManager,
                    NS_NETWORK_CACHE_MANAGER_CONTRACTID, &rv);
#endif
    if (NS_FAILED(rv)) return rv;

    nsCOMPtr<nsIStorageStream> storageStream;
    nsCOMPtr<nsIOutputStream> outputStream;
    nsCString buffer;

    // Init: (block size, maximum length)
    rv = NS_NewStorageStream(256, (PRUint32)-1, getter_AddRefs(storageStream));
    if (NS_FAILED(rv)) return rv;

    rv = storageStream->GetOutputStream(0, getter_AddRefs(outputStream));
    if (NS_FAILED(rv)) return rv;

    mBuffer.Assign("<html>\n<head>\n<title>Information about the Cache Manager</title>\n</head>\n<body>\n");
    outputStream->Write(mBuffer, mBuffer.Length(), &bytesWritten);


#ifdef MOZ_NEW_CACHE
    mStream = outputStream;
    rv = cacheService->VisitEntries(this);
    if (NS_FAILED(rv)) return rv;
#else
    nsCOMPtr<nsISimpleEnumerator> moduleIterator;
    nsCOMPtr<nsISimpleEnumerator> entryIterator;

    rv = cacheManager->NewCacheModuleIterator(getter_AddRefs(moduleIterator));
    if (NS_FAILED(rv)) return rv;

    nsCOMPtr<nsISupports> item;
    nsCOMPtr<nsINetDataCache> cache;
    nsCOMPtr<nsINetDataCacheRecord> cacheRecord;

    do {
      PRInt32 cacheEntryCount = 0;
      PRInt32 cacheSize = 0;
      PRBool bMoreModules = PR_FALSE;

      rv = moduleIterator->HasMoreElements(&bMoreModules);
      if (!bMoreModules) break;

      rv = moduleIterator->GetNext(getter_AddRefs(item));
      if (NS_FAILED(rv)) break;

      cache = do_QueryInterface(item);
      rv = cache->NewCacheEntryIterator(getter_AddRefs(entryIterator));
      if (NS_FAILED(rv)) continue;

      DumpCacheInfo(outputStream, cache);

      DumpCacheEntries(outputStream, cacheManager, cache);
/*
      do {
        char *key;
        PRUint32 length;
        PRBool bMoreEntries = PR_FALSE;

        rv = entryIterator->HasMoreElements(&bMoreEntries);
        if (!bMoreEntries) break;

        rv = entryIterator->GetNext(getter_AddRefs(item));
        if (NS_FAILED(rv)) break;

        cacheRecord = do_QueryInterface(item);

        cacheEntryCount += 1;
        cacheRecord->GetStoredContentLength(&length);
        cacheSize += length;

        buffer.Truncate(0);
        buffer.AppendInt(cacheEntryCount);
        buffer.Append(".\t");
        buffer.AppendInt(length);
        buffer.Append(" bytes\t");
        outputStream->Write(buffer, buffer.Length(), &bytesWritten);

        rv = cacheRecord->GetKey(&length, &key);
        if (NS_FAILED(rv)) break;

        outputStream->Write(key, strlen(key), &bytesWritten);
        outputStream->Write("\n", 1, &bytesWritten);

        nsMemory::Free(key);

      } while(1);

      buffer.Truncate(0);
      buffer.Append("\nTotal Bytes in Cache: ");
      buffer.AppendInt(cacheSize);
      buffer.Append("\n\n");
      outputStream->Write(buffer, buffer.Length(), &bytesWritten);
*/
    } while(1);
#endif

    mBuffer.Assign("</body>\n</html>\n");
    outputStream->Write(mBuffer, mBuffer.Length(), &bytesWritten);
        
    nsCOMPtr<nsIInputStream> inStr;
    PRUint32 size;

    rv = storageStream->GetLength(&size);
    if (NS_FAILED(rv)) return rv;

    rv = storageStream->NewInputStream(0, getter_AddRefs(inStr));
    if (NS_FAILED(rv)) return rv;

    nsIChannel* channel;
    rv = NS_NewInputStreamChannel(&channel, aURI, inStr, "text/html", size);
    if (NS_FAILED(rv)) return rv;

    *result = channel;
    return rv;
}

#ifdef MOZ_NEW_CACHE

NS_IMETHODIMP
nsAboutCache::VisitDevice(const char *deviceID,
                          nsICacheDeviceInfo *deviceInfo,
                          PRBool *visitEntries)
{
    PRUint32 bytesWritten, value;
    nsXPIDLCString str;

    // Write out the Cache Name
    deviceInfo->GetDescription(getter_Copies(str));

    mBuffer.Assign("<h2>");
    mBuffer.Append(str);
    mBuffer.Append("</h2><br>\n");

    // Write out cache info
    
    mBuffer.Append("<table>\n<tr><td><b>Usage report:</b></td>\n");
    deviceInfo->GetUsageReport(getter_Copies(str));
    mBuffer.Append("<td>");
    mBuffer.Append(str);
    mBuffer.Append("</td>\n</tr>\n");

    mBuffer.Append("\n<tr><td><b>Number of entries:</b></td>\n");
    value = 0;
    deviceInfo->GetEntryCount(&value);
    mBuffer.Append("<td>");
    mBuffer.AppendInt(value);
    mBuffer.Append("</td>\n</tr>\n");

    mBuffer.Append("\n<tr><td><b>Maximum storage size:</b></td>\n");
    value = 0;
    deviceInfo->GetMaximumSize(&value);
    mBuffer.Append("<td>");
    mBuffer.AppendInt(value);
    mBuffer.Append("</td>\n</tr>\n");

    mBuffer.Append("\n<tr><td><b>Storage in use:</b></td>\n");
    mBuffer.Append("<td>");
    value = 0;
    deviceInfo->GetTotalSize(&value);
    mBuffer.AppendInt(value);
    mBuffer.Append(" Bytes</td>\n</tr>\n");

    mBuffer.Append("</table>\n<hr>\n");

    mStream->Write(mBuffer, mBuffer.Length(), &bytesWritten);

    *visitEntries = PR_TRUE;
    return NS_OK;
}

NS_IMETHODIMP
nsAboutCache::VisitEntry(const char *deviceID,
                         const char *clientID,
                         nsICacheEntryInfo *entryInfo,
                         PRBool *visitNext)
{
    PRUint32 bytesWritten;
    nsXPIDLCString str;

    entryInfo->GetKey(getter_Copies(str));

    // Generate a about:cache-entry URL for this entry...
    nsCAutoString url;
    url += NS_LITERAL_CSTRING("about:cache-entry?client=");
    url += clientID;
    url += NS_LITERAL_CSTRING("&key=");
    url += str; // key

    // Entry start...
    mBuffer.Assign("<p>\n");

    // URI
    mBuffer.Append("<tt>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;"
                     "&nbsp;&nbsp;&nbsp;&nbsp;Key: </tt>");
    mBuffer.Append("<a href=\"");
    mBuffer.Append(url);
    mBuffer.Append("\">");
    mBuffer.Append(str);
    mBuffer.Append("</a><br>\n");

    // Client
    mBuffer.Append("<tt>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;"
                     "&nbsp;Client: </tt>");
    entryInfo->GetClientID(getter_Copies(str));
    mBuffer.Append(str);
    mBuffer.Append("<br>\n");

    // Content length
    PRUint32 length = 0;
    entryInfo->GetDataSize(&length);

    mBuffer.Append("<tt>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Data size: </tt>");
    mBuffer.AppendInt(length);
    mBuffer.Append(" Bytes<br>\n");

    // Number of accesses
    PRInt32 fetchCount = 0;
    entryInfo->GetFetchCount(&fetchCount);

    mBuffer.Append("<tt>&nbsp;&nbsp;&nbsp;Fetch count: </tt>");
    mBuffer.AppendInt(fetchCount);
    mBuffer.Append("<br>\n");

    // Last modified time
    char buf[255];
    PRExplodedTime et;
    PRTime t;

    mBuffer.Append("<tt>&nbsp;&nbsp;Last Fetched: </tt>");
    entryInfo->GetLastFetched(&t);
    if (LL_NE(t, LL_ZERO)) {
        PR_ExplodeTime(t, PR_LocalTimeParameters, &et);
        PR_FormatTime(buf, sizeof(buf), "%c", &et);
        mBuffer.Append(buf);
    } else
        mBuffer.Append("No last fetched time");
    mBuffer.Append("<br>");

    mBuffer.Append("<tt>&nbsp;Last Modified: </tt>");
    entryInfo->GetLastModified(&t);
    if (LL_NE(t, LL_ZERO)) {
        PR_ExplodeTime(t, PR_LocalTimeParameters, &et);
        PR_FormatTime(buf, sizeof(buf), "%c", &et);
        mBuffer.Append(buf);
    } else
        mBuffer.Append("No last modified time");
    mBuffer.Append("<br>");

    mBuffer.Append("<tt>Last Validated: </tt>");
    entryInfo->GetLastFetched(&t);
    if (LL_NE(t, LL_ZERO)) {
        PR_ExplodeTime(t, PR_LocalTimeParameters, &et);
        PR_FormatTime(buf, sizeof(buf), "%c", &et);
        mBuffer.Append(buf);
    } else
        mBuffer.Append("No last validated time");
    mBuffer.Append("<br>");

    // Expires time
    mBuffer.Append("<tt>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;"
                 "Expires: </tt>");
    entryInfo->GetExpirationTime(&t);
    if (LL_NE(t, LL_ZERO)) {
        PR_ExplodeTime(t, PR_LocalTimeParameters, &et);
        PR_FormatTime(buf, sizeof(buf), "%c", &et);
        mBuffer.Append(buf);
    } else {
        mBuffer.Append("No expiration time");
    }
    mBuffer.Append("<br>");

    // Stream based
    mBuffer.Append("<tt>&nbsp;&nbsp;Stream based: </tt>");
    PRBool b = PR_TRUE;
    entryInfo->GetStreamBased(&b);
    mBuffer.Append(b ? "TRUE" : "FALSE");
    mBuffer.Append("<br>\n");

    /*
    // Flags
    PRBool flag = PR_FALSE, foundFlag = PR_FALSE;
    mBuffer.Append("<tt>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;"
                 "Flags: </tt><b>");

    flag = PR_FALSE;
    entry->GetPartialFlag(&flag);
    if (flag) {
        mBuffer.Append("PARTIAL ");
        foundFlag = PR_TRUE;
    }
    flag = PR_FALSE;
    entry->GetUpdateInProgress(&flag);
    if (flag) {
        mBuffer.Append("UPDATE_IN_PROGRESS ");
        foundFlag = PR_TRUE;
    }
    flag = PR_FALSE;
    entry->GetInUse(&flag);
    if (flag) {
        mBuffer.Append("IN_USE");
        foundFlag = PR_TRUE;
    }

    if (!foundFlag) {
        mBuffer.Append("</b>none<br>\n");
    } else {
        mBuffer.Append("</b><br>\n");
    }
    */

    // Entry is done...
    mBuffer.Append("</p>\n");

    mStream->Write(mBuffer, mBuffer.Length(), &bytesWritten);

    *visitNext = PR_TRUE;
    return NS_OK;
}

#else

void nsAboutCache::DumpCacheInfo(nsIOutputStream *aStream, nsINetDataCache *aCache)
{
  PRUint32 bytesWritten, value;
  nsXPIDLString str;

  // Write out the Cache Name
  aCache->GetDescription(getter_Copies(str));

  mBuffer.Assign("<h2>");
  mBuffer.AppendWithConversion(str);
  mBuffer.Append("</h2>\n");

  // Write out cache info

  mBuffer.Append("<table>\n<tr><td><b>Current entries:</b></td>\n");
  value = 0;
  aCache->GetNumEntries(&value);
  mBuffer.Append("<td>");
  mBuffer.AppendInt(value);
  mBuffer.Append("</td>\n</tr>\n");

  mBuffer.Append("\n<tr><td><b>Maximum entries:</b></td>\n");
  value = 0;
  aCache->GetMaxEntries(&value);
  mBuffer.Append("<td>");
  mBuffer.AppendInt(value);
  mBuffer.Append("</td>\n</tr>\n");

  mBuffer.Append("\n<tr><td><b>Storage in use:</b></td>\n");
  mBuffer.Append("<td>");
  value = 0;
  aCache->GetStorageInUse(&value);
  mBuffer.AppendInt(value);
  mBuffer.Append(" KB</td>\n</tr>\n");

  mBuffer.Append("</table>\n<hr>\n");

  aStream->Write(mBuffer, mBuffer.Length(), &bytesWritten);
}


void nsAboutCache::DumpCacheEntryInfo(nsIOutputStream *aStream,
                                      nsINetDataCacheManager *aCacheMgr,
                                      char * aKey, char *aSecondaryKey,
                                      PRUint32 aSecondaryKeyLen)
{
  nsresult rv;
  PRUint32 bytesWritten;
  nsXPIDLCString str;
  nsCOMPtr<nsICachedNetData> entry;

  rv = aCacheMgr->GetCachedNetData(aKey, aSecondaryKey, aSecondaryKeyLen, 0,
                                   getter_AddRefs(entry));
  if (NS_FAILED(rv)) return;

  // Entry start...
  mBuffer.Assign("<p>\n");

  // URI
  mBuffer.Append("<tt>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;"
                     "&nbsp;&nbsp;&nbsp;&nbsp;URL: </tt>");
  entry->GetUriSpec(getter_Copies(str));
  mBuffer.Append("<a href=\"");
  mBuffer.Append(str);
  mBuffer.Append("\">");
  mBuffer.Append(str);
  mBuffer.Append("</a><br>\n");

  // Content length
  PRUint32 length = 0;
  entry->GetStoredContentLength(&length);

  mBuffer.Append("<tt>Content Length: </tt>");
  mBuffer.AppendInt(length);
  mBuffer.Append("<br>\n");

  // Number of accesses
  PRUint16 numAccesses = 0;
  entry->GetNumberAccesses(&numAccesses);

  mBuffer.Append("<tt>&nbsp;# of accesses: </tt>");
  mBuffer.AppendInt(numAccesses);
  mBuffer.Append("<br>\n");

  // Last modified time
  char buf[255];
  PRExplodedTime et;
  PRTime t;

  mBuffer.Append("<tt>&nbsp;Last Modified: </tt>");

  entry->GetLastModifiedTime(&t);
  if (LL_NE(t, LL_ZERO)) {
    PR_ExplodeTime(t, PR_LocalTimeParameters, &et);
    PR_FormatTime(buf, sizeof(buf), "%c", &et);
    mBuffer.Append(buf);
  } else {
    mBuffer.Append("No modified date sent");
  }
  mBuffer.Append("<br>");

  // Expires time
  mBuffer.Append("<tt>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;"
                 "Expires: </tt>");

  entry->GetExpirationTime(&t);
  if (LL_NE(t, LL_ZERO)) {
    PR_ExplodeTime(t, PR_LocalTimeParameters, &et);
    PR_FormatTime(buf, sizeof(buf), "%c", &et);
    mBuffer.Append(buf);
  } else {
    mBuffer.Append("No expiration date sent");
  }
  mBuffer.Append("<br>");

  // Flags
  PRBool flag = PR_FALSE, foundFlag = PR_FALSE;
  mBuffer.Append("<tt>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;"
                 "Flags: </tt><b>");

  flag = PR_FALSE;
  entry->GetPartialFlag(&flag);
  if (flag) {
    mBuffer.Append("PARTIAL ");
    foundFlag = PR_TRUE;
  }
  flag = PR_FALSE;
  entry->GetUpdateInProgress(&flag);
  if (flag) {
    mBuffer.Append("UPDATE_IN_PROGRESS ");
    foundFlag = PR_TRUE;
  }
  flag = PR_FALSE;
  entry->GetInUse(&flag);
  if (flag) {
    mBuffer.Append("IN_USE");
    foundFlag = PR_TRUE;
  }

  if (!foundFlag) {
    mBuffer.Append("</b>none<br>\n");
  } else {
    mBuffer.Append("</b><br>\n");
  }

  // Entry is done...
  mBuffer.Append("</p>\n");

  aStream->Write(mBuffer, mBuffer.Length(), &bytesWritten);
}

nsresult nsAboutCache::DumpCacheEntries(nsIOutputStream *aStream,
                                        nsINetDataCacheManager *aCacheMgr,
                                        nsINetDataCache *aCache)
{
  nsresult rv;
  nsCOMPtr<nsISimpleEnumerator> entryIterator;
  nsCOMPtr<nsISupports> item;
  nsCOMPtr<nsINetDataCacheRecord> cacheRecord;

  rv = aCache->NewCacheEntryIterator(getter_AddRefs(entryIterator));
  if (NS_FAILED(rv)) return rv;

  do {
    char *key, *secondaryKey;
    PRUint32 keyLength;
    PRBool bMoreEntries = PR_FALSE;

    rv = entryIterator->HasMoreElements(&bMoreEntries);
    if (!bMoreEntries) break;

    rv = entryIterator->GetNext(getter_AddRefs(item));
    if (NS_FAILED(rv)) break;

    cacheRecord = do_QueryInterface(item);

    rv = cacheRecord->GetKey(&keyLength, &key);
    if (NS_FAILED(rv)) break;

    // The URI spec is stored as the second of the two components that make up
    // the nsINetDataCacheRecord key and is separated from the first component
    // by a NUL character.
    for(secondaryKey = key; *secondaryKey; secondaryKey++)
        keyLength--;

    // Account for NUL character
    if (keyLength) {
      keyLength--;
      secondaryKey++;
    } else {
      secondaryKey = nsnull;
    }

    DumpCacheEntryInfo(aStream, aCacheMgr, key, secondaryKey, keyLength);

    nsMemory::Free(key);
  } while(1);

  return NS_OK;
}

#endif /* MOZ_NEW_CACHE */


NS_METHOD
nsAboutCache::Create(nsISupports *aOuter, REFNSIID aIID, void **aResult)
{
    nsAboutCache* about = new nsAboutCache();
    if (about == nsnull)
        return NS_ERROR_OUT_OF_MEMORY;
    NS_ADDREF(about);
    nsresult rv = about->QueryInterface(aIID, aResult);
    NS_RELEASE(about);
    return rv;
}

////////////////////////////////////////////////////////////////////////////////
