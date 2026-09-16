#include "gepch.h"
#include "Assets/Fonts/Font.h"
#include <sdl2/SDL_ttf.h>
#include <memory>
#include <stdexcept>

namespace GEngine::Asset
{
	Font::~Font()
	{
		UnLoad();
	}

	void Font::LoadFont(const std::string& fileName)
	{
        if (!m_FontData.empty()) throw std::logic_error("Cannot replace a loaded font with live borrowers");
        Font pending;
		// We support these font sizes
		std::vector<int> fontSizes = {
			8, 9,
			10, 11, 12, 14, 16, 18,
			20, 22, 24, 26, 28,
			30, 32, 34, 36, 38,
			40, 42, 44, 46, 48,
			52, 56,
			60, 64, 68,
			72
		};


		for (auto& size : fontSizes)
		{

			std::unique_ptr<TTF_Font, decltype(&TTF_CloseFont)> font(
                TTF_OpenFont(fileName.c_str(), size), TTF_CloseFont);
            if (!font) throw std::runtime_error("Failed to load font " + fileName
                + " in size " + std::to_string(size) + ": " + TTF_GetError());
            pending.m_FontData.emplace(size, font.get());
            font.release();
		}
		m_FontData.swap(pending.m_FontData);
	}

	void Font::UnLoad()
	{
		for (auto& font : m_FontData)
		{
			TTF_CloseFont(font.second);
		}
		m_FontData.clear();
	}

}
