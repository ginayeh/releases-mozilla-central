/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.0 (the "NPL"); you may not use this file except in
 * compliance with the NPL.  You may obtain a copy of the NPL at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the NPL is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the NPL
 * for the specific language governing rights and limitations under the
 * NPL.
 *
 * The Initial Developer of this code under the NPL is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation.  All Rights
 * Reserved.
 */

#include "nsRenderingContextUnix.h"
#include "nsDeviceContextUnix.h"

#include <math.h>
#include "nspr.h"

#include "nsRegionUnix.h"
#include "nsGfxCIID.h"

#include "X11/Xlib.h"
#include "X11/Xutil.h"

#define CLIPPING_NOT_FUNCTIONAL 1

class GraphicsState
{
public:
  GraphicsState();
  ~GraphicsState();

  nsTransform2D   *mMatrix;
  nsRect          mLocalClip;
  Region          mClipRegion;
  nscolor         mColor;
  nsIFontMetrics  *mFontMetrics;
  Font            mFont;
};

GraphicsState :: GraphicsState()
{
  mMatrix = nsnull;  
  mLocalClip.x = mLocalClip.y = mLocalClip.width = mLocalClip.height = 0;
  mClipRegion = nsnull;
  mColor = NS_RGB(0, 0, 0);
  mFontMetrics = nsnull;
  mFont = nsnull;
}

GraphicsState :: ~GraphicsState()
{
  if (nsnull != mClipRegion)
  {
    ::XDestroyRegion(mClipRegion);
    mClipRegion = NULL;
  }

  mFont = nsnull;

}


typedef unsigned char BYTE;

#define RGB(r,g,b) ((unsigned long) (((BYTE) (r) | ((unsigned long) ((BYTE) (g)) <<8)) | (((unsigned long)(BYTE)(b)) << 16)))

static NS_DEFINE_IID(kRenderingContextIID, NS_IRENDERING_CONTEXT_IID);

nsRenderingContextUnix :: nsRenderingContextUnix()
{
  NS_INIT_REFCNT();

  mFontCache = nsnull ;
  mFontMetrics = nsnull ;
  mContext = nsnull ;
  mFrontBuffer = nsnull ;
  mRenderingSurface = nsnull ;
  mCurrentColor = 0;
  mTMatrix = nsnull;
  mP2T = 1.0f;
  mStateCache = new nsVoidArray();
  mRegion = nsnull;
  mCurrFontHandle = 0;
  PushState();
}

nsRenderingContextUnix :: ~nsRenderingContextUnix()
{
  NS_IF_RELEASE(mContext);
  NS_IF_RELEASE(mFontCache);
  NS_IF_RELEASE(mFontMetrics);

  if (mRegion) {
    ::XDestroyRegion(mRegion);
    mRegion = nsnull;
  }

  mTMatrix = nsnull;
  PopState();

  if (nsnull != mStateCache)
  {
    PRInt32 cnt = mStateCache->Count();

    while (--cnt >= 0)
    {
      GraphicsState *state = (GraphicsState *)mStateCache->ElementAt(cnt);
      mStateCache->RemoveElementAt(cnt);

      if (nsnull != state)
        delete state;
    }

    delete mStateCache;
    mStateCache = nsnull;
  }

  delete mFrontBuffer;
}

NS_IMPL_QUERY_INTERFACE(nsRenderingContextUnix, kRenderingContextIID)
NS_IMPL_ADDREF(nsRenderingContextUnix)
NS_IMPL_RELEASE(nsRenderingContextUnix)

nsresult nsRenderingContextUnix :: Init(nsIDeviceContext* aContext,
					nsIWidget *aWindow)
{

  if (nsnull == aWindow->GetNativeData(NS_NATIVE_WINDOW))
    return NS_ERROR_NOT_INITIALIZED;

  mContext = aContext;
  NS_IF_ADDREF(mContext);

  mRenderingSurface = new nsDrawingSurfaceUnix();

  mRenderingSurface->display =  (Display *)aWindow->GetNativeData(NS_NATIVE_DISPLAY);
  mRenderingSurface->drawable = (Drawable)aWindow->GetNativeData(NS_NATIVE_WINDOW);
  mRenderingSurface->gc       = (GC)aWindow->GetNativeData(NS_NATIVE_GRAPHIC);

  XWindowAttributes wa;

  ::XGetWindowAttributes(mRenderingSurface->display,
			 mRenderingSurface->drawable,
			 &wa);

  mRenderingSurface->visual = wa.visual;
  mRenderingSurface->depth = wa.depth;
  
  mFrontBuffer = mRenderingSurface;

  ((nsDeviceContextUnix *)aContext)->SetDrawingSurface(mRenderingSurface);
  ((nsDeviceContextUnix *)aContext)->InstallColormap();

  mFontCache = mContext->GetFontCache();
  mP2T = mContext->GetDevUnitsToAppUnits();
  mTMatrix->AddScale(mContext->GetAppUnitsToDevUnits(),
                     mContext->GetAppUnitsToDevUnits());

  mRegion = ::XCreateRegion();
  // Select a default font here?
  return NS_OK;
}

nsresult nsRenderingContextUnix :: Init(nsIDeviceContext* aContext,
					nsDrawingSurface aSurface)
{

  mContext = aContext;
  NS_IF_ADDREF(mContext);

  mRenderingSurface = (nsDrawingSurfaceUnix *) aSurface;
  ((nsDeviceContextUnix *)aContext)->SetDrawingSurface(mRenderingSurface);
  ((nsDeviceContextUnix *)aContext)->InstallColormap();

  mFontCache = mContext->GetFontCache();
  mP2T = mContext->GetDevUnitsToAppUnits();
  mTMatrix->AddScale(mContext->GetAppUnitsToDevUnits(),
                     mContext->GetAppUnitsToDevUnits());
  mRegion = ::XCreateRegion();
  return NS_OK;
}

nsresult nsRenderingContextUnix :: SelectOffScreenDrawingSurface(nsDrawingSurface aSurface)
{  
  mRenderingSurface = (nsDrawingSurfaceUnix *) aSurface;
  return NS_OK;
}

void nsRenderingContextUnix :: Reset()
{
}

nsIDeviceContext * nsRenderingContextUnix :: GetDeviceContext(void)
{
  NS_IF_ADDREF(mContext);
  return mContext;
}

void nsRenderingContextUnix :: PushState(void)
{
  GraphicsState * state = new GraphicsState();

  // Push into this state object, add to vector
  state->mMatrix = mTMatrix;

  mStateCache->AppendElement(state);

  if (nsnull == mTMatrix)
    mTMatrix = new nsTransform2D();
  else
    mTMatrix = new nsTransform2D(mTMatrix);


}

PRBool nsRenderingContextUnix :: PopState(void)
{
  PRUint32 cnt = mStateCache->Count();
  GraphicsState * state;

  if (cnt > 0) {
    state = (GraphicsState *)mStateCache->ElementAt(cnt - 1);
    mStateCache->RemoveElementAt(cnt - 1);

    // Assign all local attributes from the state object just popped
    if (mTMatrix)
      delete mTMatrix;
    mTMatrix = state->mMatrix;

    // Delete this graphics state object
    delete state;
  }

  //XXX need to return if clip region is empty after pop. see nsirendering....h MMP
  return PR_FALSE;
}

PRBool nsRenderingContextUnix :: IsVisibleRect(const nsRect& aRect)
{
  return PR_TRUE;
}

PRBool nsRenderingContextUnix :: SetClipRect(const nsRect& aRect, nsClipCombine aCombine)
{
#ifdef CLIPPING_NOT_FUNCTIONAL
  return PR_FALSE;
#endif

  // Essentially, create the rect and select it into the GC. Get the current
  // ClipRegion first

  nsRect  trect = aRect;

  mTMatrix->TransformCoord(&trect.x, &trect.y,
                           &trect.width, &trect.height);

  //how we combine the new rect with the previous?
  if (aCombine == nsClipCombine_kIntersect)
  {
    Region a = ::XCreateRegion();
    Region tRegion = ::XCreateRegion();

    ::XOffsetRegion(a, trect.x, trect.y);
    ::XShrinkRegion(a, -trect.width, -trect.height);

    ::XIntersectRegion(a, mRegion, tRegion);

    ::XDestroyRegion(mRegion);
    ::XDestroyRegion(a);

    mRegion = tRegion;

  }
  else if (aCombine == nsClipCombine_kUnion)
  {
    Region a = ::XCreateRegion();
    Region tRegion = ::XCreateRegion();

    ::XOffsetRegion(a, trect.x, trect.y);
    ::XShrinkRegion(a, -trect.width, -trect.height);

    ::XUnionRegion(a, mRegion, tRegion);

    ::XDestroyRegion(mRegion);
    ::XDestroyRegion(a);

    mRegion = tRegion;
  }
  else if (aCombine == nsClipCombine_kSubtract)
  {
    Region a = ::XCreateRegion();
    Region tRegion = ::XCreateRegion();

    ::XOffsetRegion(a, trect.x, trect.y);
    ::XShrinkRegion(a, -trect.width, -trect.height);

    ::XSubtractRegion(a, mRegion, tRegion);

    ::XDestroyRegion(mRegion);
    ::XDestroyRegion(a);

    mRegion = tRegion;
  }
  else if (aCombine == nsClipCombine_kReplace)
  {
    Region a = ::XCreateRegion();

    ::XOffsetRegion(a, trect.x, trect.y);
    ::XShrinkRegion(a, -trect.width, -trect.height);

    ::XDestroyRegion(mRegion);

    mRegion = a;

  }
  else
    NS_ASSERTION(PR_FALSE, "illegal clip combination");

  ::XSetRegion(mRenderingSurface->display,
	       mRenderingSurface->gc,
	       mRegion);

  return PR_TRUE;
}

PRBool nsRenderingContextUnix :: GetClipRect(nsRect &aRect)
{
#ifdef CLIPPING_NOT_FUNCTIONAL
  return PR_FALSE;
#endif

  if (mRegion != nsnull) {
    XRectangle xrect;
    ::XClipBox(mRegion, &xrect);
    aRect.SetRect(xrect.x, xrect.y, xrect.width, xrect.height);
  } else {
    aRect.SetRect(0,0,0,0);
  }
  return PR_TRUE;

}

PRBool nsRenderingContextUnix :: SetClipRegion(const nsIRegion& aRegion, nsClipCombine aCombine)
{

#ifdef CLIPPING_NOT_FUNCTIONAL
  return PR_FALSE;
#endif

  nsRect rect;
  XRectangle xrect;

  nsRegionUnix *pRegion = (nsRegionUnix *)&aRegion;
  Region xregion = pRegion->GetXRegion();

  ::XSetRegion(mRenderingSurface->display,
	       mRenderingSurface->gc,
	       xregion);

  mRegion = xregion ;

  return (PR_TRUE);
}

void nsRenderingContextUnix :: GetClipRegion(nsIRegion **aRegion)
{
  nsIRegion * pRegion ;

  static NS_DEFINE_IID(kCRegionCID, NS_REGION_CID);
  static NS_DEFINE_IID(kIRegionIID, NS_IREGION_IID);

  nsresult rv = NSRepository::CreateInstance(kCRegionCID, 
					     nsnull, 
					     kIRegionIID, 
					     (void **)aRegion);

  if (NS_OK == rv) {
    nsRect rect;
    pRegion = (nsIRegion *)&aRegion;
    pRegion->Init();    
    this->GetClipRect(rect);
    pRegion->Union(rect.x,rect.y,rect.width,rect.height);
  }

  return ;

}

void nsRenderingContextUnix :: SetColor(nscolor aColor)
{
  XGCValues values ;

  mCurrentColor = aColor ;

  // XXX
  //mCurrentColor++;

  PRUint32 pixel ;

  pixel = ((nsDeviceContextUnix *)mContext)->ConvertPixel(aColor);

  mCurrentColor = pixel;

  values.foreground = mCurrentColor;
  values.background = mCurrentColor;

  ::XChangeGC(mRenderingSurface->display,
	      mRenderingSurface->gc,
	      GCForeground | GCBackground,
	      &values);
  
}

nscolor nsRenderingContextUnix :: GetColor() const
{
  return mCurrentColor;
}

void nsRenderingContextUnix :: SetFont(const nsFont& aFont)
{
  NS_IF_RELEASE(mFontMetrics);
  mFontMetrics = mFontCache->GetMetricsFor(aFont);

  if (mFontMetrics)
  {  
//    mCurrFontHandle = (Font)mFontMetrics->GetFontHandle();
    mCurrFontHandle = ::XLoadFont(mRenderingSurface->display, (char *)mFontMetrics->GetFontHandle());
    
    ::XSetFont(mRenderingSurface->display,
	       mRenderingSurface->gc,
	       mCurrFontHandle);
      
    ::XFlushGC(mRenderingSurface->display,
	       mRenderingSurface->gc);
  }
}

const nsFont& nsRenderingContextUnix :: GetFont()
{
  return mFontMetrics->GetFont();
}

nsIFontMetrics* nsRenderingContextUnix :: GetFontMetrics()
{
  return mFontMetrics;
}

// add the passed in translation to the current translation
void nsRenderingContextUnix :: Translate(nscoord aX, nscoord aY)
{
  mTMatrix->AddTranslation((float)aX,(float)aY);
}

// add the passed in scale to the current scale
void nsRenderingContextUnix :: Scale(float aSx, float aSy)
{
  mTMatrix->AddScale(aSx, aSy);
}

nsTransform2D * nsRenderingContextUnix :: GetCurrentTransform()
{
  return mTMatrix;
}

nsDrawingSurface nsRenderingContextUnix :: CreateDrawingSurface(nsRect *aBounds)
{
  // Must make sure this code never gets called when nsRenderingSurface is nsnull
  PRUint32 depth = DefaultDepth(mRenderingSurface->display,
				DefaultScreen(mRenderingSurface->display));
  Pixmap p;

  if (aBounds != nsnull) {
    p  = ::XCreatePixmap(mRenderingSurface->display,
			     mRenderingSurface->drawable,
			     aBounds->width, aBounds->height, depth);
  } else {
    p  = ::XCreatePixmap(mRenderingSurface->display,
			     mRenderingSurface->drawable,
			     2, 2, depth);
  }

  nsDrawingSurfaceUnix * surface = new nsDrawingSurfaceUnix();

  surface->drawable = p ;
  surface->display  = mRenderingSurface->display;
  surface->gc       = mRenderingSurface->gc;
  surface->visual   = mRenderingSurface->visual;
  surface->depth    = mRenderingSurface->depth;

  return ((nsDrawingSurface)surface);
}

void nsRenderingContextUnix :: DestroyDrawingSurface(nsDrawingSurface aDS)
{
  nsDrawingSurfaceUnix * surface = (nsDrawingSurfaceUnix *) aDS;

  // XXX - Could this be a GC? If so, store the type of surface in nsDrawingSurfaceUnix
  ::XFreePixmap(surface->display, surface->drawable);

  //XXX greg, this seems bad. MMP
  if (mRenderingSurface == surface)
    mRenderingSurface = nsnull;

  delete aDS;
}

void nsRenderingContextUnix :: DrawLine(nscoord aX0, nscoord aY0, nscoord aX1, nscoord aY1)
{
  mTMatrix->TransformCoord(&aX0,&aY0);
  mTMatrix->TransformCoord(&aX1,&aY1);

  ::XDrawLine(mRenderingSurface->display, 
	      mRenderingSurface->drawable,
	      mRenderingSurface->gc,
	      aX0, aY0, aX1, aY1);
}

void nsRenderingContextUnix :: DrawRect(const nsRect& aRect)
{
  DrawRect(aRect.x, aRect.y, aRect.width, aRect.height);
}

void nsRenderingContextUnix :: DrawRect(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight)
{

  nscoord x,y,w,h;

  x = aX;
  y = aY;
  w = aWidth;
  h = aHeight;

  mTMatrix->TransformCoord(&x,&y,&w,&h);

  ::XDrawRectangle(mRenderingSurface->display, 
		   mRenderingSurface->drawable,
		   mRenderingSurface->gc,
		   x,y,w,h);
}

void nsRenderingContextUnix :: FillRect(const nsRect& aRect)
{
  FillRect(aRect.x, aRect.y, aRect.width, aRect.height);
}

void nsRenderingContextUnix :: FillRect(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight)
{
  nscoord x,y,w,h;

  x = aX;
  y = aY;
  w = aWidth;
  h = aHeight;

  mTMatrix->TransformCoord(&x,&y,&w,&h);

  ::XFillRectangle(mRenderingSurface->display, 
		   mRenderingSurface->drawable,
		   mRenderingSurface->gc,
		   x,y,w,h);

}


void nsRenderingContextUnix::DrawPolygon(nsPoint aPoints[], PRInt32 aNumPoints)
{
  PRUint32 i ;
  XPoint * xpoints;
  XPoint * thispoint;
  
  xpoints = (XPoint *) PR_Malloc(sizeof(XPoint) * aNumPoints);

  for (i = 0; i < aNumPoints; i++){
    thispoint = (xpoints+i);
    thispoint->x = aPoints[i].x;
    thispoint->y = aPoints[i].y;
    mTMatrix->TransformCoord((PRInt32*)&thispoint->x,(PRInt32*)&thispoint->y);
  }

  ::XDrawLines(mRenderingSurface->display,
	       mRenderingSurface->drawable,
	       mRenderingSurface->gc,
	       xpoints, aNumPoints, CoordModeOrigin);

  PR_Free((void *)xpoints);
}

void nsRenderingContextUnix::FillPolygon(nsPoint aPoints[], PRInt32 aNumPoints)
{
  PRUint32 i ;
  XPoint * xpoints;
  XPoint * thispoint;
  nscoord x,y;
  
  xpoints = (XPoint *) PR_Malloc(sizeof(XPoint) * aNumPoints);

  for (i = 0; i < aNumPoints; i++){
    thispoint = (xpoints+i);
    x = aPoints[i].x;
    y = aPoints[i].y;
    mTMatrix->TransformCoord(&x,&y);
    thispoint->x = x;
    thispoint->y = y;
  }

  ::XFillPolygon(mRenderingSurface->display,
		 mRenderingSurface->drawable,
		 mRenderingSurface->gc,
		 xpoints, aNumPoints, Convex, CoordModeOrigin);

  PR_Free((void *)xpoints);
}

void nsRenderingContextUnix :: DrawEllipse(const nsRect& aRect)
{
  DrawEllipse(aRect.x, aRect.y, aRect.width, aRect.height);
}

void nsRenderingContextUnix :: DrawEllipse(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight)
{
  nscoord x,y,w,h;

  x = aX;
  y = aY;
  w = aWidth;
  h = aHeight;

  mTMatrix->TransformCoord(&x,&y,&w,&h);

  ::XDrawArc(mRenderingSurface->display, 
	     mRenderingSurface->drawable,
	     mRenderingSurface->gc,
	     x,y,w,h, 0, 360 * 64);
}

void nsRenderingContextUnix :: FillEllipse(const nsRect& aRect)
{
  FillEllipse(aRect.x, aRect.y, aRect.width, aRect.height);
}

void nsRenderingContextUnix :: FillEllipse(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight)
{
  nscoord x,y,w,h;

  x = aX;
  y = aY;
  w = aWidth;
  h = aHeight;

  mTMatrix->TransformCoord(&x,&y,&w,&h);

  ::XFillArc(mRenderingSurface->display, 
	     mRenderingSurface->drawable,
	     mRenderingSurface->gc,
	     x,y,w,h, 0, 360 * 64);
}

void nsRenderingContextUnix :: DrawArc(const nsRect& aRect,
                                 float aStartAngle, float aEndAngle)
{
  this->DrawArc(aRect.x,aRect.y,aRect.width,aRect.height,aStartAngle,aEndAngle);
}

void nsRenderingContextUnix :: DrawArc(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight,
                                 float aStartAngle, float aEndAngle)
{
  nscoord x,y,w,h;

  x = aX;
  y = aY;
  w = aWidth;
  h = aHeight;

  mTMatrix->TransformCoord(&x,&y,&w,&h);

  ::XDrawArc(mRenderingSurface->display, 
	     mRenderingSurface->drawable,
	     mRenderingSurface->gc,
	     x,y,w,h, NS_TO_INT_ROUND(aStartAngle * 64.0f),
             NS_TO_INT_ROUND(aEndAngle * 64.0f));
}

void nsRenderingContextUnix :: FillArc(const nsRect& aRect,
                                 float aStartAngle, float aEndAngle)
{
  this->FillArc(aRect.x, aRect.y, aRect.width, aRect.height, aStartAngle, aEndAngle);
}

void nsRenderingContextUnix :: FillArc(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight,
                                 float aStartAngle, float aEndAngle)
{
  nscoord x,y,w,h;

  x = aX;
  y = aY;
  w = aWidth;
  h = aHeight;

  mTMatrix->TransformCoord(&x,&y,&w,&h);

  ::XFillArc(mRenderingSurface->display, 
	     mRenderingSurface->drawable,
	     mRenderingSurface->gc,
	     x,y,w,h, NS_TO_INT_ROUND(aStartAngle * 64.0f),
             NS_TO_INT_ROUND(aEndAngle * 64.0f));
}

void nsRenderingContextUnix :: DrawString(const char *aString, PRUint32 aLength,
                                    nscoord aX, nscoord aY,
                                    nscoord aWidth)
{
  PRInt32 x = aX;
  PRInt32 y = aY;

  // Substract xFontStruct ascent since drawing specifies baseline
  if (mFontMetrics)
      y += mFontMetrics->GetMaxAscent();

  mTMatrix->TransformCoord(&x,&y);

  ::XDrawString(mRenderingSurface->display, 
		mRenderingSurface->drawable,
		mRenderingSurface->gc,
		x, y, aString, aLength);

  if (mFontMetrics)
  {
    PRUint8 deco = mFontMetrics->GetFont().decorations;

    if (deco & NS_FONT_DECORATION_OVERLINE)
      DrawLine(aX, aY, aX + aWidth, aY);

    if (deco & NS_FONT_DECORATION_UNDERLINE)
    {
      nscoord ascent = mFontMetrics->GetMaxAscent();
      nscoord descent = mFontMetrics->GetMaxDescent();

      DrawLine(aX, aY + ascent + (descent >> 1),
               aX + aWidth, aY + ascent + (descent >> 1));
    }

    if (deco & NS_FONT_DECORATION_LINE_THROUGH)
    {
      nscoord height = mFontMetrics->GetHeight();

      DrawLine(aX, aY + (height >> 1), aX + aWidth, aY + (height >> 1));
    }
  }
}

void nsRenderingContextUnix :: DrawString(const PRUnichar *aString, PRUint32 aLength,
                                         nscoord aX, nscoord aY, nscoord aWidth)
{
  PRInt32 x = aX;
  PRInt32 y = aY;

  // Substract xFontStruct ascent since drawing specifies baseline
  if (mFontMetrics)
      y += mFontMetrics->GetMaxAscent();

  mTMatrix->TransformCoord(&x, &y);

  ::XDrawString16(mRenderingSurface->display, 
                mRenderingSurface->drawable,
                mRenderingSurface->gc,
                x, y, (XChar2b *)aString, aLength);

  if (mFontMetrics)
  {
    PRUint8 deco = mFontMetrics->GetFont().decorations;

    if (deco & NS_FONT_DECORATION_OVERLINE)
      DrawLine(aX, aY, aX + aWidth, aY);

    if (deco & NS_FONT_DECORATION_UNDERLINE)
    {
      nscoord ascent = mFontMetrics->GetMaxAscent();
      nscoord descent = mFontMetrics->GetMaxDescent();

      DrawLine(aX, aY + ascent + (descent >> 1),
               aX + aWidth, aY + ascent + (descent >> 1));
    }

    if (deco & NS_FONT_DECORATION_LINE_THROUGH)
    {
      nscoord height = mFontMetrics->GetHeight();

      DrawLine(aX, aY + (height >> 1), aX + aWidth, aY + (height >> 1));
    }
  }
}

void nsRenderingContextUnix :: DrawString(const nsString& aString,
                                         nscoord aX, nscoord aY, nscoord aWidth)
{
  DrawString(aString.GetUnicode(), aString.Length(), aX, aY, aWidth);
}

void nsRenderingContextUnix :: DrawImage(nsIImage *aImage, nscoord aX, nscoord aY)
{
nscoord width,height;
printf("1-----------------------\n");
  width = NS_TO_INT_ROUND(mP2T * aImage->GetWidth());
  height = NS_TO_INT_ROUND(mP2T * aImage->GetHeight());

  this->DrawImage(aImage,aX,aY,width,height);
}

void nsRenderingContextUnix :: DrawImage(nsIImage *aImage, nscoord aX, nscoord aY,
                                        nscoord aWidth, nscoord aHeight) 
{
nsRect	tr;

printf("2-----------------------\n");
  tr.x = aX;
  tr.y = aY;
  tr.width = aWidth;
  tr.height = aHeight;
  this->DrawImage(aImage,tr);
}

void nsRenderingContextUnix :: DrawImage(nsIImage *aImage, const nsRect& aSRect, const nsRect& aDRect)
{
nsRect	sr,dr;
  
  printf("nsRenderingContextUnixStretch :: DrawImage 0x%x\n",aImage);
  sr = aSRect;
  mTMatrix ->TransformCoord(&sr.x,&sr.y,&sr.width,&sr.height);

  dr = aDRect;
  mTMatrix->TransformCoord(&dr.x,&dr.y,&dr.width,&dr.height);
  
  ((nsImageUnix*)aImage)->Draw(*this,mRenderingSurface,sr.x,sr.y,sr.width,sr.height,
                                 dr.x,dr.y,dr.width,dr.height);
}

void nsRenderingContextUnix :: DrawImage(nsIImage *aImage, const nsRect& aRect)
{
nsRect	tr;

  printf("nsRenderingContextUnix :: DrawImage 0x%x\n",aImage);
  tr = aRect;
  mTMatrix->TransformCoord(&tr.x,&tr.y,&tr.width,&tr.height);

  if (aImage != nsnull) {
    ((nsImageUnix*)aImage)->Draw(*this,mRenderingSurface,tr.x,tr.y,tr.width,tr.height);
  } else {
    printf("Image is NULL!\n");
  }
}

nsresult nsRenderingContextUnix :: CopyOffScreenBits(nsRect &aBounds)
{
  
printf("CopyOffScreenBits-----------------------\n");
  ::XCopyArea(mRenderingSurface->display, 
	      mRenderingSurface->drawable,
	      mFrontBuffer->drawable,
	      mFrontBuffer->gc,
	      aBounds.x, aBounds.y, aBounds.width, aBounds.height, 0, 0);
	      
  return NS_OK;
}















