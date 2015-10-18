#include <Windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <comdef.h>
#include <atlbase.h>
#include <string>
#include <vector>
#include <gdiplus.h>
#include <time.h>
#include <iostream>

#include "VertexSource.h"
#include "DefaultSource.h"
#include "ResolveSource.h"

#define IMAGE_WIDTH 800
#define IMAGE_HEIGHT 800
#define SUPERSAMPLE 16
#define IMAGE_WIDTH_SSAA (IMAGE_WIDTH * sqrt(SUPERSAMPLE))
#define IMAGE_HEIGHT_SSAA (IMAGE_HEIGHT * sqrt(SUPERSAMPLE))
#define PIXELS (IMAGE_WIDTH * IMAGE_HEIGHT)
#define PIXELS_SSAA (IMAGE_WIDTH * IMAGE_HEIGHT * SUPERSAMPLE)

#define ARGV_START 1

#pragma comment(lib, "d3d11")
#pragma comment(lib, "d3dcompiler")
#pragma comment(lib, "gdiplus")

using namespace Gdiplus;

int wmain(int argc, wchar_t** argv) {
	time_t t = time(0);
	tm now;
	localtime_s(&now, &t);

	std::wstring png_address;

	png_address += std::to_wstring(now.tm_year + 1900);
	png_address += L"-";
	png_address += std::to_wstring(now.tm_mon + 1);
	png_address += L"-";
	png_address += std::to_wstring(now.tm_mday);
	png_address += L"-";
	png_address += std::to_wstring(now.tm_hour);
	png_address += L"-";
	png_address += std::to_wstring(now.tm_min);
	png_address += L"-";
	png_address += std::to_wstring(now.tm_sec);

	GdiplusStartupInput startupInput;
	ULONG_PTR token;
	Status stat = Ok;
	stat = GdiplusStartup(&token, &startupInput, nullptr);

	std::cout << "Initialized GDI+" << std::endl;

	CLSID encoderCLSID;
	UINT numEncoders = 0;
	UINT size = 0;
	ImageCodecInfo* codec = nullptr;

	GetImageEncodersSize(&numEncoders, &size);

	codec = (ImageCodecInfo*)(malloc(size));

	GetImageEncoders(numEncoders, size, codec);

	for (UINT i = 0; i < numEncoders; i++) {
		if (wcscmp(codec[i].MimeType, L"image/png") == 0) {
			encoderCLSID = codec[i].Clsid;
			std::cout << "Retrieved codec image/png" << std::endl;
			break;
		}
	}

	free(codec);

	HGLOBAL byteArray = GlobalAlloc(NULL, 3 * IMAGE_WIDTH * IMAGE_HEIGHT);

	BITMAPINFO info = { 0 };

	info.bmiHeader.biWidth = 800;
	info.bmiHeader.biHeight = 800;
	info.bmiHeader.biBitCount = 24;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biCompression = BI_RGB;

	CComPtr<ID3D11Device> Device;
	CComPtr<ID3D11DeviceContext> Context;
	CComPtr<ID3D11VertexShader> VertexShader;
	CComPtr<ID3D11Texture2D> NoiseTexture;
	CComPtr<ID3D11ShaderResourceView> NoiseResource;
	CComPtr<ID3D11Texture2D> TextureA;
	CComPtr<ID3D11Texture2D> TextureB;
	CComPtr<ID3D11RenderTargetView> TargetA;
	CComPtr<ID3D11RenderTargetView> TargetB;
	CComPtr<ID3D11ShaderResourceView> ResourceA;
	CComPtr<ID3D11ShaderResourceView> ResourceB;
	CComPtr<ID3D11PixelShader> ResolveShader;
	CComPtr<ID3D11Texture2D> ResolveTexture;
	CComPtr<ID3D11RenderTargetView> ResolveTarget;
	CComPtr<ID3D11Texture2D> ReadBuffer;

	HRESULT hr = D3D11CreateDevice (
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		NULL,
		0,
		nullptr,
		0,
		D3D11_SDK_VERSION,
		&Device,
		nullptr,
		&Context
	);

	if (FAILED(hr)) {
		std::cout << "Failed to create D3D11 device - HRESULT 0x" << hr << std::endl;
		system("pause");
		return -1;
	}

	std::cout << "Created D3D11 device" << std::endl;

	hr = Device->CreateVertexShader (
		VertexSource,
		sizeof(VertexSource),
		nullptr,
		&VertexShader
	);

	if (FAILED(hr)) {
		std::cout << "Failed to create vertex shader - HRESULT 0x" << hr << std::endl;
		system("pause");
		return -1;
	}

	std::cout << "Created vertex shader" << std::endl;

	std::vector<CComPtr<ID3D11PixelShader>> shaders;
	bool debug = false;
	UINT numRenders = 1;

	for (int i = ARGV_START; i < argc; i++) {
		if (i == ARGV_START && wcscmp(argv[i], L"debug") == 0) {
			std::cout << "Using debug mode" << std::endl;
			debug = true;
			continue;
		}

		DWORD result = GetFileAttributesW(argv[i]);

		if (result != INVALID_FILE_ATTRIBUTES) {
			CComPtr<ID3D11PixelShader> PixelShader;
			CComPtr<ID3DBlob> PixelSource;
			CComPtr<ID3DBlob> PixelDebug;

			hr = D3DCompileFromFile (
				argv[i],
				nullptr,
				nullptr,
				"main",
				"ps_5_0",
				NULL,
				NULL,
				&PixelSource,
				&PixelDebug
			);

			if (FAILED(hr)) {
				std::wcout << "Failed to compile " << argv[i] << ", error log:" << std::endl << std::endl;
				std::cout << (const char*)PixelDebug->GetBufferPointer() << std::endl;
				system("pause");
				return -1;
			}

			std::wcout << "Compiled pixel shader source: " << argv[i] << std::endl;

			hr = Device->CreatePixelShader (
				PixelSource->GetBufferPointer(),
				PixelSource->GetBufferSize(),
				nullptr,
				&PixelShader
			);

			if (FAILED(hr)) {
				std::cout << "Failed to create pixel shader - HRESULT 0x" << hr << std::endl;
				system("pause");
				return -1;
			}

			std::cout << "Created pixel shader" << std::endl;

			shaders.push_back(PixelShader);
		} else if (i == argc - 1) {
			std::wstring num = argv[i];

			try {
				numRenders = std::stoi(num);
			} catch (...) {
				std::wcout << "Cannot recognize argument \"" << argv[i] << ",\" exiting...";
				system("pause");
				return -1;
			}
		} else {
			std::wcout << "Unable to locate " << argv[i] << ", exiting..." << std::endl;
			system("pause");
			return -1;
		}
	}

	if (!debug) {
		srand((unsigned int)(time(NULL)));
	}

	if (numRenders > 1) {
		CreateDirectoryW(png_address.c_str(), NULL);
	}

	if (shaders.empty()) {
		CComPtr<ID3D11PixelShader> PixelShader;

		std::cout << "No pixel shader source(s) given, using Default.hlsl" << std::endl;

		hr = Device->CreatePixelShader (
			DefaultSource,
			sizeof(DefaultSource),
			nullptr,
			&PixelShader
		);

		if (FAILED(hr)) {
			std::cout << "Failed to create pixel shader - HRESULT 0x" << hr << std::endl;
			system("pause");
			return -1;
		}

		std::cout << "Created pixel shader" << std::endl;

		shaders.push_back(PixelShader);
	}

	hr = Device->CreatePixelShader (
		ResolveSource,
		sizeof(ResolveSource),
		nullptr,
		&ResolveShader
	);

	if (FAILED(hr)) {
		std::cout << "Failed to create SSAA resolve pixel shader - HRESULT 0x" << hr << std::endl;
		system("pause");
		return -1;
	}

	std::cout << "Created SSAA resolve pixel shader" << std::endl;

	D3D11_TEXTURE2D_DESC TextureDesc;
	TextureDesc.ArraySize = 1;
	TextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	TextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	TextureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	TextureDesc.Height = (UINT)(IMAGE_HEIGHT_SSAA);
	TextureDesc.MipLevels = 1;
	TextureDesc.MiscFlags = NULL;
	TextureDesc.SampleDesc.Count = 1;
	TextureDesc.SampleDesc.Quality = 0;
	TextureDesc.Usage = D3D11_USAGE_DYNAMIC;
	TextureDesc.Width = (UINT)(IMAGE_WIDTH_SSAA);

	hr = Device->CreateTexture2D (
		&TextureDesc,
		nullptr,
		&NoiseTexture
	);

	if (FAILED(hr)) {
		std::cout << "Failed to create noise texture - HRESULT 0x" << hr << std::endl;
		system("pause");
		return -1;
	}

	std::cout << "Created noise texture" << std::endl;

	hr = Device->CreateShaderResourceView (
		NoiseTexture,
		nullptr,
		&NoiseResource
	);

	if (FAILED(hr)) {
		std::cout << "Failed to create noise resource - HRESULT 0x" << hr << std::endl;
		system("pause");
		return -1;
	}

	std::cout << "Created noise resource" << std::endl;

	TextureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	TextureDesc.CPUAccessFlags = NULL;
	TextureDesc.Usage = D3D11_USAGE_DEFAULT;

	hr = Device->CreateTexture2D (
		&TextureDesc,
		nullptr,
		&TextureA
	);

	if (FAILED(hr)) {
		std::cout << "Failed to create TextureA - HRESULT 0x" << hr << std::endl;
		system("pause");
		return -1;
	}

	std::cout << "Created TextureA" << hr << std::endl;

	hr = Device->CreateRenderTargetView (
		TextureA,
		nullptr,
		&TargetA
	);

	if (FAILED(hr)) {
		std::cout << "Failed to create TargetA - HRESULT 0x" << hr << std::endl;
		system("pause");
		return -1;
	}

	std::cout << "Created TargetA" << std::endl;

	hr = Device->CreateShaderResourceView (
		TextureA,
		nullptr,
		&ResourceA
	);

	if (FAILED(hr)) {
		std::cout << "Failed to create ResourceA - HRESULT 0x" << hr << std::endl;
		system("pause");
		return -1;
	}

	std::cout << "Created ResourceA" << std::endl;

	hr = Device->CreateTexture2D (
		&TextureDesc,
		nullptr,
		&TextureB
	);

	if (FAILED(hr)) {
		std::cout << "Failed to create TextureB - HRESULT 0x" << hr << std::endl;
		system("pause");
		return -1;
	}

	std::cout << "Created TextureB" << std::endl;

	hr = Device->CreateRenderTargetView (
		TextureB,
		nullptr,
		&TargetB
	);

	if (FAILED(hr)) {
		std::cout << "Failed to create TargetB - HRESULT 0x" << hr << std::endl;
		system("pause");
		return -1;
	}

	std::cout << "Created TargetB" << std::endl;

	hr = Device->CreateShaderResourceView (
		TextureB,
		nullptr,
		&ResourceB
	);

	if (FAILED(hr)) {
		std::cout << "Failed to create ResourceB - HRESULT 0x" << hr << std::endl;
		system("pause");
		return -1;
	}

	std::cout << "Created ResourceB" << std::endl;

	TextureDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
	TextureDesc.Height = IMAGE_HEIGHT;
	TextureDesc.Width = IMAGE_WIDTH;

	hr = Device->CreateTexture2D (
		&TextureDesc,
		nullptr,
		&ResolveTexture
	);

	if (FAILED(hr)) {
		std::cout << "Failed to create SSAA resolve texture - HRESULT 0x" << hr << std::endl;
		system("pause");
		return -1;
	}

	std::cout << "Created SSAA resolve texture" << std::endl;

	hr = Device->CreateRenderTargetView (
		ResolveTexture,
		nullptr,
		&ResolveTarget
	);

	if (FAILED(hr)) {
		std::cout << "Failed to create SSAA resolve target - HRESULT 0x" << hr << std::endl;
		system("pause");
		return -1;
	}

	std::cout << "Created SSAA resolve target" << std::endl;

	TextureDesc.BindFlags = NULL;
	TextureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	TextureDesc.Usage = D3D11_USAGE_STAGING;

	hr = Device->CreateTexture2D (
		&TextureDesc,
		nullptr,
		&ReadBuffer
	);

	if (FAILED(hr)) {
		std::cout << "Failed to create cpu read texture - HRESULT 0x" << hr << std::endl;
		system("pause");
		return -1;
	}

	std::cout << "Created cpu read texture" << std::endl;

	for (UINT i = 0; i < numRenders; i++) {
		D3D11_VIEWPORT Viewport;
		Viewport.Width = FLOAT(IMAGE_WIDTH_SSAA);
		Viewport.Height = FLOAT(IMAGE_HEIGHT_SSAA);
		Viewport.MinDepth = 0.0f;
		Viewport.MaxDepth = 1.0f;
		Viewport.TopLeftX = 0.0f;
		Viewport.TopLeftY = 0.0f;

		std::cout << "Generating noise texture..." << std::endl;

		D3D11_MAPPED_SUBRESOURCE map = { 0 };

		hr = Context->Map (
			NoiseTexture,
			0,
			D3D11_MAP_WRITE_DISCARD,
			NULL,
			&map
		);

		if (FAILED(hr)) {
			std::cout << "Failed to map noise image to cpu - HRESULT 0x" << (void*)hr << std::endl;
			system("pause");
			return -1;
		}

		unsigned char* NoisePixels = (unsigned char*)(map.pData);

		for (int j = 0; j < PIXELS_SSAA * 4; j++) {
			NoisePixels[j] = rand() % 256;
		}

		Context->Unmap(NoiseTexture, 0);

		bool ImageA = true;
		ID3D11RenderTargetView* RTV = TargetA;
		ID3D11ShaderResourceView* SRV[] = { NoiseResource, ResourceB };

		Context->RSSetViewports(1, &Viewport);
		Context->IASetInputLayout(nullptr);
		Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		Context->VSSetShader(VertexShader, nullptr, 0);

		for (CComPtr<ID3D11PixelShader> PixelShader : shaders) {
			Context->OMSetRenderTargets(1, &RTV, nullptr);
			Context->PSSetShaderResources(0, 2, SRV);
			Context->PSSetShader(PixelShader, nullptr, 0);
			Context->Draw(3, 0);

			if (ImageA) {
				RTV = TargetB;
				SRV[1] = ResourceA;
			} else {
				RTV = TargetA;
				SRV[1] = ResourceB;
			}
		}

		std::cout << "Generated image" << std::endl;

		Viewport.Width = FLOAT(IMAGE_WIDTH);
		Viewport.Height = FLOAT(IMAGE_HEIGHT);

		RTV = ResolveTarget;

		Context->RSSetViewports(1, &Viewport);
		Context->OMSetRenderTargets(1, &RTV, nullptr);
		Context->PSSetShader(ResolveShader, nullptr, 0);
		Context->PSSetShaderResources(0, 1, &SRV[1]);
		Context->Draw(3, 0);

		std::cout << "Resolved image" << std::endl;

		Context->CopyResource(ReadBuffer, ResolveTexture);

		ZeroMemory(&map, sizeof(map));

		hr = Context->Map (
			ReadBuffer,
			0,
			D3D11_MAP_READ,
			NULL,
			&map
		);

		if (FAILED(hr)) {
			std::cout << "Failed to map image to cpu - HRESULT 0x" << (void*)hr << std::endl;
			system("pause");
			return -1;
		}

		unsigned char* byteIndex = (unsigned char*)(byteArray);
		unsigned char* pixelIndex = (unsigned char*)(map.pData);

		for (int j = 0; j < IMAGE_HEIGHT; j++) {
			for (int k = 0; k < IMAGE_WIDTH; k++) {
				*byteIndex++ = *pixelIndex++;
				*byteIndex++ = *pixelIndex++;
				*byteIndex++ = *pixelIndex++;
				pixelIndex++; //ignore alpha channel
			}
		}

		Context->Unmap(ReadBuffer, 0);

		std::cout << "Buffered image to cpu" << std::endl;

		Bitmap* bitmap = new Bitmap(&info, byteArray);

		std::wstring png_name = png_address;

		if (numRenders == 1) {
			png_name += L".png";
		} else {
			png_name += L"/";
			png_name += std::to_wstring(now.tm_year + 1900);
			png_name += L"-";
			png_name += std::to_wstring(now.tm_mon + 1);
			png_name += L"-";
			png_name += std::to_wstring(now.tm_mday);
			png_name += L"-";
			png_name += std::to_wstring(now.tm_hour);
			png_name += L"-";
			png_name += std::to_wstring(now.tm_min);
			png_name += L"-";
			png_name += std::to_wstring(now.tm_sec);
			png_name += L"-";
			png_name += std::to_wstring(i);
			png_name += L".png";
		}

		std::wcout << "Saving png image @ " << png_name << std::endl;

		stat = bitmap->Save(png_name.c_str(), &encoderCLSID);

		if (stat != Ok) {
			std::cout << "Failed to save png image - Gdi status " << stat << std::endl;
			system("pause");
			return -1;
		}

		std::cout << "Saved png image " << std::endl;

		delete bitmap;
	}

	GlobalFree(byteArray);

	GdiplusShutdown(token);

	std::cout << "The program completed successfully." << std::endl;

	system("pause");

	return 0;
}