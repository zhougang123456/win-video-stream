#include <stdio.h>
#include <assert.h>
#include <process.h>
#include <time.h>
#include <stdint.h>
#include "D3D11.h"
#include "DXGI1_2.h"
//#include "DXGICaptor.h"
#include "video-stream.hpp"

#pragma comment(lib, "D3D11.lib")
#pragma comment(lib, "DXGI.lib")

#define RESET_OBJECT(obj) { if(obj) obj->Release(); obj = NULL; }

ID3D11Device* m_hDevice = nullptr;
ID3D11DeviceContext* m_hContext = nullptr;
IDXGIOutputDuplication* m_hDeskDupl = nullptr;
VideoStream* m_videoStream = nullptr;
int iWidth = 0;
int iHeight = 0;

static inline uint64_t get_time()
{
	time_t clock;
	timeval now;
	struct tm tm;
	SYSTEMTIME wtm;
	GetLocalTime(&wtm);
	tm.tm_year = wtm.wYear - 1900;
	tm.tm_mon = wtm.wMonth - 1;
	tm.tm_mday = wtm.wDay;
	tm.tm_hour = wtm.wHour;
	tm.tm_min = wtm.wMinute;
	tm.tm_sec = wtm.wSecond;
	tm.tm_isdst = -1;
	clock = mktime(&tm);
	now.tv_sec = clock;
	now.tv_usec = wtm.wMilliseconds * 1000;
	return (uint64_t)now.tv_sec * 1000000 + (uint64_t)now.tv_usec;
}

BOOL Init()
{
	HRESULT hr = S_OK;


	// Driver types supported
	D3D_DRIVER_TYPE DriverTypes[] =
	{
		D3D_DRIVER_TYPE_HARDWARE,
		D3D_DRIVER_TYPE_WARP,
		D3D_DRIVER_TYPE_REFERENCE,
		

	};
	UINT NumDriverTypes = ARRAYSIZE(DriverTypes);

	// Feature levels supported
	D3D_FEATURE_LEVEL FeatureLevels[] =
	{
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
		D3D_FEATURE_LEVEL_9_1
	};
	UINT NumFeatureLevels = ARRAYSIZE(FeatureLevels);

	D3D_FEATURE_LEVEL FeatureLevel;
	HMODULE software = LoadLibrary(L"D3D11Ref.dll");
	if (software == NULL) {
		return FALSE;
	}
	//
	// Create D3D device
	//
	for (UINT DriverTypeIndex = 0; DriverTypeIndex < NumDriverTypes; ++DriverTypeIndex)
	{
		hr = D3D11CreateDevice(NULL, DriverTypes[DriverTypeIndex], NULL, 0, FeatureLevels, NumFeatureLevels, D3D11_SDK_VERSION, &m_hDevice, &FeatureLevel, &m_hContext);
		if (SUCCEEDED(hr))
		{
			break;
		}
	}
	if (FAILED(hr))
	{
		return FALSE;
	}

	//
	// Get DXGI device
	//
	IDXGIDevice* hDxgiDevice = NULL;
	hr = m_hDevice->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&hDxgiDevice));
	if (FAILED(hr))
	{
		return FALSE;
	}

	//
	// Get DXGI adapter
	//
	IDXGIAdapter* hDxgiAdapter = NULL;
	hr = hDxgiDevice->GetParent(__uuidof(IDXGIAdapter), reinterpret_cast<void**>(&hDxgiAdapter));
	RESET_OBJECT(hDxgiDevice);
	if (FAILED(hr))
	{
		return FALSE;
	}

	//
	// Get output
	//
	INT nOutput = 0;
	IDXGIOutput* hDxgiOutput = NULL;
	hr = hDxgiAdapter->EnumOutputs(nOutput, &hDxgiOutput);
	RESET_OBJECT(hDxgiAdapter);
	if (FAILED(hr))
	{
		return FALSE;
	}

	//
	// get output description struct
	//
	DXGI_OUTPUT_DESC        m_dxgiOutDesc;
	hDxgiOutput->GetDesc(&m_dxgiOutDesc);
	iWidth = m_dxgiOutDesc.DesktopCoordinates.right - m_dxgiOutDesc.DesktopCoordinates.left;
	iHeight = m_dxgiOutDesc.DesktopCoordinates.bottom - m_dxgiOutDesc.DesktopCoordinates.top;
	//
	// QI for Output 1
	//
	IDXGIOutput1* hDxgiOutput1 = NULL;
	hr = hDxgiOutput->QueryInterface(__uuidof(hDxgiOutput1), reinterpret_cast<void**>(&hDxgiOutput1));
	RESET_OBJECT(hDxgiOutput);
	if (FAILED(hr))
	{
		return FALSE;
	}

	//
	// Create desktop duplication
	//
	hr = hDxgiOutput1->DuplicateOutput(m_hDevice, &m_hDeskDupl);
	RESET_OBJECT(hDxgiOutput1);
	if (FAILED(hr))
	{
		return FALSE;
	}

	m_videoStream = new VideoStream;
	return TRUE;

}
void Finit()
{
	RESET_OBJECT(m_hDeskDupl);
	RESET_OBJECT(m_hContext);
	RESET_OBJECT(m_hDevice);
	delete m_videoStream;
}

BOOL AttatchToThread(VOID)
{
	HDESK hold = GetThreadDesktop(GetCurrentThreadId());
	HDESK hCurrentDesktop = OpenInputDesktop(0, FALSE, GENERIC_ALL);
	if (!hCurrentDesktop)
	{
		return FALSE;
	}

	// Attach desktop to this thread
	BOOL bDesktopAttached = SetThreadDesktop(hCurrentDesktop) ? true : false;
	int err = GetLastError();
	CloseDesktop(hold);
	CloseDesktop(hCurrentDesktop);
	//hCurrentDesktop = NULL;


	return bDesktopAttached;
}

BOOL QueryFrame()
{
	if (!AttatchToThread())
	{
		return FALSE;
	}

	INT32 time = (INT32)get_time() / 1000;

	if (m_videoStream != nullptr) {
		m_videoStream->Stream_Timeout(time);
		if (m_videoStream->Is_StreamStart()) {
			printf("is stream start!\n");
		}
		else
		{
			printf("is stream stop!\n");
		}
	}
	
	IDXGIResource* hDesktopResource = NULL;
	DXGI_OUTDUPL_FRAME_INFO FrameInfo;
	m_hDeskDupl->ReleaseFrame();
	HRESULT hr = m_hDeskDupl->AcquireNextFrame(500, &FrameInfo, &hDesktopResource);
	if (FAILED(hr))
	{
		//
		// 在一些win10的系统上,如果桌面没有变化的情况下，
		// 这里会发生超时现象，但是这并不是发生了错误，而是系统优化了刷新动作导致的。
		// 所以，这里没必要返回FALSE，返回不带任何数据的TRUE即可
		//
		return TRUE;
	}

	//
	// query next frame staging buffer
	//
	ID3D11Texture2D* hAcquiredDesktopImage = NULL;
	hr = hDesktopResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&hAcquiredDesktopImage));
	RESET_OBJECT(hDesktopResource);
	if (FAILED(hr))
	{
		return FALSE;
	}
	if (FrameInfo.TotalMetadataBufferSize)
	{
		
		
		BYTE* MetaDataBuffer = new BYTE[FrameInfo.TotalMetadataBufferSize];
		INT MetaDataSize = FrameInfo.TotalMetadataBufferSize;
		

		UINT BufSize = FrameInfo.TotalMetadataBufferSize;

		// Get move rectangles
		hr = m_hDeskDupl->GetFrameMoveRects(BufSize, reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT*>(MetaDataBuffer), &BufSize);
		if (FAILED(hr))
		{
			if (hr != DXGI_ERROR_ACCESS_LOST)
			{
				printf("DXGI_ERROR_ACCESS_LOST");
			}
			return hr;
		}
		INT MoveCount = BufSize / sizeof(DXGI_OUTDUPL_MOVE_RECT);

		BYTE* DirtyRects = MetaDataBuffer + BufSize;
		BufSize = FrameInfo.TotalMetadataBufferSize - BufSize;

		// Get dirty rectangles
		hr = m_hDeskDupl->GetFrameDirtyRects(BufSize, reinterpret_cast<RECT*>(DirtyRects), &BufSize);
		if (FAILED(hr))
		{
			if (hr != DXGI_ERROR_ACCESS_LOST)
			{
				printf("DXGI_ERROR_ACCESS_LOST");
			}
			return hr;
		}

		D3D11_TEXTURE2D_DESC frameDescriptor;
		hAcquiredDesktopImage->GetDesc(&frameDescriptor);

		//
		// create a new staging buffer for fill frame image
		//
		ID3D11Texture2D* hNewDesktopImage = NULL;
		frameDescriptor.Usage = D3D11_USAGE_STAGING;
		frameDescriptor.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		frameDescriptor.BindFlags = 0;
		frameDescriptor.MiscFlags = 0;
		frameDescriptor.MipLevels = 1;
		frameDescriptor.ArraySize = 1;
		frameDescriptor.SampleDesc.Count = 1;
		frameDescriptor.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		hr = m_hDevice->CreateTexture2D(&frameDescriptor, NULL, &hNewDesktopImage);
		if (FAILED(hr))
		{
			RESET_OBJECT(hAcquiredDesktopImage);
			m_hDeskDupl->ReleaseFrame();
			return FALSE;
		}

		//
		// copy next staging buffer to new staging buffer
		//
		m_hContext->CopyResource(hNewDesktopImage, hAcquiredDesktopImage);

		RESET_OBJECT(hAcquiredDesktopImage);
		m_hDeskDupl->ReleaseFrame();

		//
		// create staging buffer for map bits
		//
		IDXGISurface* hStagingSurf = NULL;
		hr = hNewDesktopImage->QueryInterface(__uuidof(IDXGISurface), (void**)(&hStagingSurf));
		RESET_OBJECT(hNewDesktopImage);
		if (FAILED(hr))
		{
			return FALSE;
		}
		DXGI_SURFACE_DESC hStagingSurfDesc;
		// BGRA8
		hStagingSurf->GetDesc(&hStagingSurfDesc);
		//
		// copy bits to user space
		//
		DXGI_MAPPED_RECT mappedRect;
		hr = hStagingSurf->Map(&mappedRect, DXGI_MAP_READ);
		int imgSize = iWidth * iHeight * 4;

		unsigned char* pImgData = new unsigned char[imgSize];

		if (SUCCEEDED(hr))
		{
			for (int i = 0; i < iHeight; i++) {


				memcpy((BYTE*)pImgData + i * iWidth * 32 / 8,
					(BYTE*)mappedRect.pBits + i * mappedRect.Pitch,
					iWidth * 32 / 8);


			}

			hStagingSurf->Unmap();
		}
		else {
			printf("failed.\n");
		}

		INT Pitch = iWidth * 4;
		INT DirtyCount = BufSize / sizeof(RECT);
		//printf("count %d \n", DirtyCount);
		RECT* dirtyRects = reinterpret_cast<RECT*>(DirtyRects);
		
		for (int i = 0; i < DirtyCount; i++) 
		{	
			/*printf("%d i: left %d right %d top %d bottom %d",
				i, dirtyRects[i].left, dirtyRects[i].right, dirtyRects[i].top, dirtyRects[i].bottom);*/
			
			UINT32 width = dirtyRects[i].right - dirtyRects[i].left;
			UINT32 height = dirtyRects[i].bottom - dirtyRects[i].top;

			UINT32 line_size = width * 4;
			size_t alloc_size = height * line_size;
			POINT sourcePoint;
			sourcePoint.x = dirtyRects[i].left;
			sourcePoint.y = dirtyRects[i].top;

			BYTE* src = (BYTE*)pImgData + sourcePoint.y * Pitch + sourcePoint.x * 4;
			BYTE* src_end = src - Pitch;
			src += Pitch * (height - 1);

			Drawable drawable;
			drawable.width = width;
			drawable.height = height;
			drawable.bpp = 4;
			drawable.data = new BYTE[alloc_size];

			for (int j = 0; j < height, src != src_end; src -= Pitch, ++j)
			{
				memcpy((BYTE*)drawable.data + j * line_size, src, line_size);
			}
			if (m_videoStream != nullptr)
			{	
				m_videoStream->Add_Stream(&dirtyRects[i], time, NULL);
			}
			delete[] drawable.data;
		}

		
		
		RESET_OBJECT(hStagingSurf);
		delete[] pImgData;
		delete[] MetaDataBuffer;
		
	}
	return true;
	
}

unsigned __stdcall capture(void* arg)
{
	
	
	while (true) {
		QueryFrame();
		Sleep(40);
	}
	return 0;
}


int main(int argc, TCHAR* argv[])
{
	if (!Init()) {
		Finit();
		printf("not support dxgi.");
		return -1;
	}
	HANDLE handle = (HANDLE)_beginthreadex(NULL, 0, capture, NULL, 0, NULL);
	CloseHandle(handle);
	getchar();
	Finit();
	return 0;
}