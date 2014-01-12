///////////////////////////////////////////////////
// File:		WindowWin32.h
// Date:		12th January 2014
// Authors:		Matt Phillips
// Description:	Win32 window
///////////////////////////////////////////////////

#pragma once

#include "renderer/Window.h"

namespace ion
{
	namespace render
	{
		class WindowWin32 : public Window
		{
		public:
			WindowWin32(const std::string& title, u32 width, u32 height, bool fullscreen);
			virtual ~WindowWin32();

			virtual bool Update();
			virtual void Resize(u32 width, u32 height);
			virtual void SetFullscreen(bool fullscreen);
			virtual void SetTitle(const std::string& title);

		protected:
			static LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

			HINSTANCE mhInstance;
			HWND mWindowHandle;
		};
	}
}