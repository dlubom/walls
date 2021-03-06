#include "stdafx.h"
#include "resource.h"
#include "utility.h"
#include "MapLayer.h"
#include "WallsMapDoc.h"
#include "GdalLayer.h"
#include "NtiLayer.h"
#include "ExpandNTI.h"

//=====================================================================
//NTI File operations --

BOOL CNtiLayer::m_bExpanding=FALSE;

CNtiLayer::~CNtiLayer()
{
	if(m_nte) delete m_nte;
}

void CNtiLayer::AppendInfo(CString &cs) const
{
	AppendFileTime(cs,m_pathName,0);

	cs.AppendFormat("Type: WallsMap Image File\r\nImage size: %dx%dx%d (%d band%s)", 
            m_uWidth, m_uHeight,
			m_uBitsPerSample*m_uBands,m_uBands,(m_uBands>1)?"s":"");

	if(m_bPalette) {
		cs.AppendFormat(" %u-color Palette\r\n",m_uColors);
	}
	else cs+="\r\n";

	{
		COLORREF clr;
		if(ParseTransColor(clr)) {
			cs.AppendFormat("Transparent rgb color: (%u,%u,%u) Tolerance: %u bits\r\n",
				clr&0xff,(clr>>8)&0xff,(clr>>16)&0xff,GetTolerance(clr)+1);
		}
	}

	AppendGeoRefInfo(cs);

	cs.AppendFormat("Compression type: %s",m_nti.XfrFcnName());

	if(*m_nti.GetParams()) {
		cs.AppendFormat("  Param: %s",m_nti.GetParams());
	}
	if(m_nti.FilterFlag()) {
		cs.AppendFormat("  Filter: %s",m_nti.FilterName());
	}
	if(m_nti.IsCompressed()) {
		char buf[20];
		cs.AppendFormat("  Compressed data size: %s (%.2f%%)",CommaSep(buf,m_nti.SizeEncoded()),m_nti.Percent());
	}

	cs.AppendFormat("\r\nFile has %d resolution level(s) --",m_bLevelLimit+1);

	for(int i=0;i<=(int)m_bLevelLimit;i++) {
		if(m_nti.IsCompressed()) {
			char eSize[20];
			CommaSep(eSize,(m_nti.SizeEncoded(i)+512)/1024);
			cs.AppendFormat("\r\n (%dx%dx%d) compressed to %s KB (%.2f%%)",
				m_nti.LvlWidth(i),m_nti.LvlHeight(i),m_nti.LvlBands(i),eSize,m_nti.LvlPercent(i));
		}
		else
			cs.AppendFormat("\r\n (%dx%d)",m_nti.LvlWidth(i),m_nti.LvlHeight(i));
	}
	cs+="\r\n";
	AppendProjectionRefInfo(cs);
	cs+="\r\n";
}

//All users of nti_file.lib must define this function to establish
//suported compression methods --
CSF_XFRFCN nti_XfrFcn(int typ)
{
	if(typ==-1) { //FreeDecode(): Uninitialize all encode/decode fcns --
		nti_zlib_d(NULL,0); //decode uninit - also frees buffers
		nti_zlib_e(NULL,1); //encode uninit
		nti_loco_d(NULL,0); //decode uninit - also frees buffers
		nti_loco_e(NULL,1); //encode uninit
		nti_jp2k_d(NULL,0); //decode uninit - also frees buffers
		nti_jp2k_e(NULL,1); //encode uninit
	}
	else {
		bool bEncode=(typ&NTI_FCNENCODE)!=0;
		typ&=~NTI_FCNENCODE;

		switch(typ) {
			case NTI_FCNLOCO : return (CSF_XFRFCN)(bEncode?nti_loco_e:nti_loco_d);
			case NTI_FCNZLIB : return (CSF_XFRFCN)(bEncode?nti_zlib_e:nti_zlib_d);
			case NTI_FCNJP2K : return (CSF_XFRFCN)(bEncode?nti_jp2k_e:nti_jp2k_d);
		}
	}
	return NULL;
}

void CNtiLayer::Init()
{
}

void CNtiLayer::UnInit()
{
	CNTIFile::FreeDefaultCache();
	CNTIFile::FreeDecode();
	CNTIFile::FreeBuffer();
	CNTIFile::Lco_UnInit();
}

//Virtual fcns --

int CNtiLayer::LayerType() const
{
	return CLayerSet::TYP_NTI;
}

void CNtiLayer::SetNTIPointers()
{
	m_pathName=m_nti.PathName();
	m_datumName=m_nti.GetDatumName();
	if(!*m_datumName) m_datumName=NULL;
	m_unitsName=m_nti.GetUnitsName();
	m_projectionRef=m_nti.GetProjectionRef();
}

BOOL CNtiLayer::SetNTIProperties()
{
	SetNTIPointers();
	m_uBands=m_nti.NumBands();
	m_uColors=m_nti.NumColors();
	m_uBitsPerSample=8;
	m_uOvrBands=m_nti.LvlBands(1);
	if(m_uOvrBands!=3) m_bUseTransColor=false; //at least for now
	m_uWidth=m_nti.Width();
	m_uHeight=m_nti.Height();
	InitSizeStr();
	m_nti.GetTransform(m_fTransImg);
	if(m_bTrans=(m_fTransImg[5]<0.0)) { 
		ComputeInverseTransform();
	}
	ASSERT(m_bTrans || m_fTransImg[0]==0.0 && m_fTransImg[3]==0.0 &&
		m_fTransImg[1]==1.0 && m_fTransImg[5]==1.0);
	ComputeCellRatios();
	ComputeExtent();
	m_iNad=GetNad(m_datumName);
	m_fMetersPerUnit=m_nti.GetMetersPerUnit();
	m_iZone=m_nti.GetUTMZone();
	m_bProjected=m_bTrans && !IsLatLongExtent();
	m_bPalette=m_uColors>0;
	m_bLevelLimit=(BYTE)m_nti.NumLevels()-1;
	//
	//Make use of GDAL's static message handlers for Kakadu --
	if(m_nti.XfrFcnID()==NTI_FCNJP2K) JP2KAKDataset::KakaduInitialize(); 

	int nColors=m_nti.IsGrayscale()?256:m_uColors;

	m_lpBitmapInfo=(LPBITMAPINFO)calloc(1,sizeof(BITMAPINFOHEADER)+nColors*sizeof(RGBQUAD));

	if(!m_lpBitmapInfo) {
		m_nti.Close();
		return FALSE;
	}

	m_lpBitmapInfo->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	m_lpBitmapInfo->bmiHeader.biPlanes = 1;
	m_lpBitmapInfo->bmiHeader.biCompression = BI_RGB;
	m_lpBitmapInfo->bmiHeader.biClrImportant = 0;

	if(nColors) {
		if(m_bPalette)
			memcpy(m_lpBitmapInfo->bmiColors,m_nti.Palette(),nColors*sizeof(RGBQUAD));
		else {
			for(int i=0;i<nColors;i++)
					m_lpBitmapInfo->bmiColors[i].rgbRed=m_lpBitmapInfo->bmiColors[i].rgbGreen=
					m_lpBitmapInfo->bmiColors[i].rgbBlue=(BYTE)i;
		}
	}
 	m_bOpen=true;
	return TRUE;
}

BOOL CNtiLayer::OpenHandle(LPCTSTR lpszPathName,UINT uFlags /*=0*/)
{
	if(m_nti.Opened()) return TRUE;
	if(!m_nti.Open(lpszPathName))
	{
		if(!m_nti.AttachCache()) {
			SetNTIPointers();
			m_bOpen=true;
			return TRUE;
		}
		m_nti.Close();
		m_bOpen=false;
	}
	return FALSE;
}

void CNtiLayer::CloseHandle()
{
	m_dib.DeleteObject();
	if(m_nti.Opened()) {
		m_nti.Close();
		m_bOpen=false;
	}
}

void CNtiLayer::CheckForNTE()
{
	char pathbuf[MAX_PATH];
	strcpy(pathbuf,PathName());
	strcpy(trx_Stpext(pathbuf),".nte");
	GDALOpenInfo oOpenInfo(pathbuf,GA_ReadOnly);
	if(!oOpenInfo.pabyHeader) return;

	ASSERT(!m_nte);

	CGdalLayer::Init();
	GDALDataset *pGD=(GetGDALDriverManager()->GetDriver(0))->pfnOpen(&oOpenInfo);

	if(!pGD) goto _close;
	GDALRasterBand *po=pGD->GetRasterBand(1);
	if(!po) goto _close;

	int blkX=0,blkY=0;
	po->GetBlockSize(&blkX,&blkY);
	int nOvr=po->GetOverviewCount();

	if(!pGD || po->GetXSize()!=m_uWidth || po->GetYSize()!=m_uHeight ||
		pGD->GetRasterCount()!=1 || pGD->GetBitsPerSample()!=16 ||
		nOvr!=(int)m_bLevelLimit || blkX!=128 || blkY!=blkX)
			goto _close;
	try {
		m_nte=new CNTERecord(pGD,m_extent);
		m_nte->vOvrBands.reserve(nOvr+1);
		m_nte->vOvrBands.push_back(po);
		for(int i=0;i<nOvr;i++) {
			GDALRasterBand *rb=po->GetOverview(i);
			rb->GetBlockSize(&blkX,&blkY);
			if(blkX!=128 || blkY!=blkX) throw 0;
			m_nte->vOvrBands.push_back(rb);
		}
	}
	catch(...) {
		goto _close;
	}
	return;

_close:
	if(m_nte) delete m_nte;
	else if(pGD) GDALClose(pGD);
	m_nte=NULL;
	CMsgBox("File: %s\n\nThis file could not be opened or has the wrong format for elevation data.",pathbuf);
}

bool CNtiLayer::ParseTransColor(COLORREF &clr) const
{
	//format: r,b,g,e
	LPCSTR p=m_nti.GetTransColorStr();
	if(*p && isdigit(*p)) {
		DWORD r,b,g,e;
		r=atoi(p);
		p=strchr(p,',');
		if(p && isdigit(*++p)) {
			b=atoi(p++);
			p=strchr(p,',');
			if(p && isdigit(*++p)) {
				g=atoi(p++);
				p=strchr(p,',');
				if(p && isdigit(*++p)) {
					e=atoi(p);
					clr=((r&0xff)|((b&0xff)<<8)|((g&0xdff)<<16)|((e&0xff)<<24));
					return true;
				}
			}
		}
	}
	return false;
}

BOOL CNtiLayer::Open(LPCTSTR lpszPathName,UINT uFlags)
{
	assert(!m_nti.Opened());
	bool bExpand=false;

	if(m_nti.Open(lpszPathName)) {
		if(uFlags&NTL_FLG_SILENT) return FALSE;
		if(nti_errno==NTI_ErrNoOverviews) {
			if(m_nti.Open(lpszPathName,FALSE)) {
				if(nti_errno==NTI_ErrReadOnly) {
					CMsgBox(MB_ICONEXCLAMATION,IDS_ERR_NTIEXPANDRO,lpszPathName);
					return FALSE;
				}
				goto _fail;
			}
			bExpand=true;
		}
		else
		  goto _fail;
	}

	VERIFY(SetNTIProperties());

	if((uFlags&NTL_FLG_LAYERSET)==0) {
		 //If opened singly, parse opacity settings --
		m_bUseTransColor=ParseTransColor(m_crTransColor);
	}
	else {
		if(m_uOvrBands!=3) m_crTransColor&=0xFFFFFF; //in case file format changed!
	}

	if(bExpand) {
		CExpandNTI dlg(this);
		//if(uFlags&NTL_FLG_LAYERSET)	m_bExpanding++;
		//else m_bExpanding=FALSE;
		m_bExpanding++;
		if(dlg.DoModal()!=IDOK) {
			m_nti.Close();
			return FALSE;
		}
		ASSERT(!m_nti.Opened());
		if(m_nti.Open(lpszPathName)) goto _fail;
		SetNTIPointers();
	}

	m_uFlags=(uFlags&~NTL_FLG_OPENONLY);

	if(!m_nti.AttachCache()) {
		if(IsTransformed())
			CheckForNTE();
		return TRUE;
	}

_fail:
	if(!(uFlags&NTL_FLG_SILENT)) {
		if(uFlags&NTL_FLG_LAYERSET)
			CLayerSet::m_bOpenConflict=-1; //not found or could not be opened
		else
			CMsgBox("%s\n\nOpen failure: %s.",lpszPathName,m_nti.Errstr());
	}
	if(m_nti.Opened()) m_nti.Close();
	return FALSE;
}

int CNtiLayer::CopyAllToDC(CDC *pDC,const CRect &crDst,HBRUSH hbr)
{
	//Copy complete image to destination DC, filling and centering it as needed.
	//iWidth and iHeight are dimensions of bitmap selected in *pDC (or of window)

	CSize szDst(crDst.Width(),crDst.Height());
	CSize szSrc(m_uWidth,m_uHeight);

	if(szDst.cx<=0 || szDst.cy<=0 || szSrc.cx<=0 || szSrc.cy<=0) return 0;

	CSize  szDIB=m_dib.GetHandle()?m_dib.GetSize():CSize(0,0);

	bool bFitWidth=szSrc.cx*szDst.cy>=szDst.cx*szSrc.cy;

	//Generate a new local DIB if required --
	if( (bFitWidth && szDIB.cx<szDst.cx && szDIB.cx<szSrc.cx) ||
		(!bFitWidth && szDIB.cy<szDst.cy && szDIB.cy<szSrc.cy)) {

		//We need a higher resolution bitmap than what's currently contained in m_DIB --
		UINT lvl=(int)m_bLevelLimit;
		while(lvl && m_nti.LvlWidth(lvl)<(UINT)szDst.cx)  lvl--;

		int imgWidth=m_nti.LvlWidth(lvl);
		int imgHeight=m_nti.LvlHeight(lvl);

		int nBands=lvl?m_uOvrBands:m_uBands;
		int nRowBytes=(imgWidth*nBands+3)&~3;

		m_lpBitmapInfo->bmiHeader.biClrUsed=(nBands>1)?0:(m_bPalette?m_uColors:256);
		m_lpBitmapInfo->bmiHeader.biBitCount = nBands*8;
		m_lpBitmapInfo->bmiHeader.biWidth = imgWidth;
		m_lpBitmapInfo->bmiHeader.biHeight = -imgHeight;
		m_lpBitmapInfo->bmiHeader.biSizeImage = imgHeight*nRowBytes;

		if(!m_dib.InitBitmap(m_lpBitmapInfo)) return 0;

		if(m_nti.LvlBands(lvl)==1) {
			m_nti.ReadBytes((BYTE *)m_dib.GetDIBits(),lvl,0,0,imgWidth,imgHeight,nRowBytes);
		}
		else m_nti.ReadBGR((BYTE *)m_dib.GetDIBits(),lvl,0,0,imgWidth,imgHeight,nRowBytes);

		if(m_nti.Errno()) {
			CMsgBox("Error accessing %s: %s",FileName(),m_nti.Errstr());
			m_dib.DeleteObject();
			return 0;
		}
		m_dib_lvl=lvl;		//Resolution level of data in local bitmap
		//Position and size (pixels) of requested bitmap at this resolution level --
		m_dib_rect=CRect(0,0,imgWidth,imgHeight);
	}

	//We now have a local bitmap, m_dib, containing the *entire* image rectangle
	//m_dib_rect at zoom level m_dib_zoom

	//Compute position of m_dib's upper left corner in *pDestDIB.
	//Also ddjust szSrc to be the size of the image in *pDestDID --
	int dibOffX,dibOffY;

	if(bFitWidth) {
		if(szSrc.cx<=szDst.cx) {
			dibOffX=((szDst.cx-szSrc.cx)>>1);
		}
		else {
			bFitWidth=false;
			szSrc.cx=szDst.cx;
			szSrc.cy=(int)(szSrc.cx*(m_uHeight/(double)m_uWidth));
			dibOffX=0;
		}
		dibOffY=((szDst.cy-szSrc.cy)>>1);
	}
	else {
		if(szSrc.cy<=szDst.cy) {
			bFitWidth=true;
			dibOffY=((szDst.cy-szSrc.cy)>>1);
		}
		else {
			szSrc.cy=szDst.cy;
			szSrc.cx=(int)(szSrc.cy*(m_uWidth/(double)m_uHeight));
			dibOffY=0;
		}
		dibOffX=((szDst.cx-szSrc.cx)>>1);
	}

	dibOffX+=crDst.left;
	dibOffY+=crDst.top;

	if(hbr) ::FillRect(pDC->m_hDC,&crDst,hbr);

	if(!bFitWidth) {
		//Compute final screen pixels / image pixels --
		m_dib.Stretch(pDC->m_hDC,dibOffX,dibOffY,szSrc.cx,szSrc.cy,HALFTONE);
	}
	else m_dib.Draw(pDC->m_hDC,dibOffX,dibOffY);

	return 1;
}

int CNtiLayer::CopyToDIB(CDIBWrapper *pDestDIB,const CFltRect &geoExt,double fScale)
{
	//pDestDIB				- Points to screen bitmap to which image data will be copied.
	//geoExt				- Geographical extent corresponding to bitmap.
	//fScale				- Screen pixels / geo-units.
	//
	//Returns:	0			- No data copied to bitmap.
	//			1			- Only a portion of the data copied.
	//			2			- All data copied.

	double fZoom;
	BYTE lvl=GetZoomLevel(fZoom,fScale);

	fZoom=fScale/fZoom;
	// fZoom = (screen pixels / image pixels) at level lvl

	int lvlWidth=m_nti.LvlWidth(lvl);
	int lvlHeight=m_nti.LvlHeight(lvl);

	CFltRect viewExt=geoExt;
	GeoPtToImgPt(viewExt.tl,lvl);
	GeoPtToImgPt(viewExt.br,lvl);
	if(viewExt.l<0.0) viewExt.l=0.0;
	if(viewExt.r>(double)lvlWidth) viewExt.r=lvlWidth;
	if(viewExt.t<0.0) viewExt.t=0.0;
	if(viewExt.b>(double)lvlHeight) viewExt.b=lvlHeight;
	CRect crImage((int)floor(viewExt.l),(int)floor(viewExt.t),
		(int)ceil(viewExt.r),(int)ceil(viewExt.b));

	if(crImage.Width()<=0 || crImage.Height()<=0) return 0;

	//Return value is 2 if all image data is copied --
	int iRet=(crImage.Width()==(int)lvlWidth && crImage.Height()==(int)lvlHeight)?2:1;
	if(!m_bAlpha) return iRet;

	if(!m_dib.GetHandle() || lvl!=m_dib_lvl || ContainsRect(m_dib_rect,crImage)!=2 ||
		m_bUseTransColor!=(m_hdcMask!=NULL) || m_bUseTransColor && m_crTransColor!=m_crTransColorMask) {

		if(m_hdcMask) DeleteMask();

		//For our new local DIB let's expand this minimal image region to block boundaries,
		//but not beyond the image itself --
		crImage.left-=(crImage.left%BlkWidth);
		crImage.top-=(crImage.top%BlkWidth);
		if(crImage.right%BlkWidth) {
			crImage.right+=BlkWidth-(crImage.right%BlkWidth);
			if(crImage.right>(int)lvlWidth) crImage.right=lvlWidth;
		}
		if(crImage.bottom%BlkWidth) {
			crImage.bottom+=BlkWidth-(crImage.bottom%BlkWidth);
			if(crImage.bottom>(int)lvlHeight) crImage.bottom=lvlHeight;
		}
		//Convert crImage dimensions (image pixels) to m_dib dimensions --
		int imgWidth=crImage.Width();
		int imgHeight=crImage.Height();
		int nBands=lvl?m_uOvrBands:m_uBands;
		int nRowBytes=(imgWidth*nBands+3)&~3;

		m_lpBitmapInfo->bmiHeader.biClrUsed=(nBands>1)?0:(m_bPalette?m_uColors:256);
		m_lpBitmapInfo->bmiHeader.biBitCount = nBands*8;
		m_lpBitmapInfo->bmiHeader.biWidth = imgWidth;
		m_lpBitmapInfo->bmiHeader.biHeight = -imgHeight;
		m_lpBitmapInfo->bmiHeader.biSizeImage = imgHeight*nRowBytes;

		if(!m_dib.InitBitmap(m_lpBitmapInfo)) return 0;

		ASSERT(m_nti.LvlBands(lvl)==nBands);

		if(nBands==1) {
			m_nti.ReadBytes((BYTE *)m_dib.GetDIBits(),lvl,
				crImage.left,crImage.top,imgWidth,imgHeight,nRowBytes);
		}
		else {
			m_nti.ReadBGR((BYTE *)m_dib.GetDIBits(),lvl,
				crImage.left,crImage.top,imgWidth,imgHeight,nRowBytes);
			if(IsUsingBGRE()) ApplyBGRE();
		}

		if(m_nti.Errno()) {
			CMsgBox("Error accessing %s: %s",FileName(),m_nti.Errstr());
			m_dib.DeleteObject();
			return 0;
		}
		m_dib_lvl=lvl;		//Resolution level of data to retrieve
		m_dib_rect=crImage; //Position and size (pixels) of requested bitmap at this resolution level
	}
	//else we're zooming out from the zoom level of the previous bitmap, which still contains it,
	//or else zooming greater than 1:1 and the bitmap still contains it.

	//Compute position of m_dib's upper left corner in *pDestDIB --
	CFltPoint dibOffset((double)m_dib_rect.left,(double)m_dib_rect.top);
	ImgPtToGeoPt(dibOffset,lvl);
	int destOffX=_rnd(fScale*(dibOffset.x-geoExt.l));
	int destOffY=_rnd(fScale*(dibOffset.y-geoExt.t));
	if(geoExt.t>geoExt.b) destOffY=-destOffY;

	if(!CopyData(pDestDIB,destOffX,destOffY,fZoom))
		iRet=0;

	pDestDIB->ReleaseMemoryDC();
	if(CLayerSet::m_bExporting) {
		m_dib.DeleteObject();
	}
	return iRet;
}

void CNtiLayer::CopyBlock(LPBYTE pDstOrg,LPBYTE pSrcOrg,int srcLenX,int srcLenY,int nSrcRowWidth)
{
	//Copies srcLenX x srcLenY portion of a 256 x 256 block of bytes --
	nSrcRowWidth+=3;
	nSrcRowWidth&=~3;
	for(int row=0;row<srcLenY;row++) {
		memcpy(pDstOrg+row*NTI_BLKWIDTH,pSrcOrg+(row*nSrcRowWidth),srcLenX);
	}
}

int CNtiLayer::ReadRasterBand(UINT band,UINT xStart,UINT yStart,UINT xSize,UINT ySize,LPBYTE pDest)
{
	return m_nti.ReadBytes(pDest,0,xStart,yStart,xSize,ySize,(xSize+3)&~3,band);
}

