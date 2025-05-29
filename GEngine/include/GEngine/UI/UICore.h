#pragma once
#include <imgui/imgui.h>
#include <cstdint>
#include <string_view>

namespace GEngine
{
	namespace Asset
	{
		class Texture;
	}

	namespace UI
	{
		const char* GenerateID();
		const char* GenerateLabelID(std::string_view label);
		void PushID();
		void PopID();

		bool IsInputEnabled();
		void SetInputEnabled(bool enabled);

		void ShiftCursorX(float distance);
		void ShiftCursorY(float distance);
		void ShiftCursor(float x, float y);

		void BeginPropertyGrid(uint32_t columns = 2);
		void EndPropertyGrid();

		bool BeginTreeNode(const char* name, bool defaultOpen = true);
		void EndTreeNode();

		bool ColoredButton(const char* label, const ImVec4& backgroundColor, ImVec2 buttonSize = { 16.0f, 16.0f });
		bool ColoredButton(const char* label, const ImVec4& backgroundColor, const ImVec4& foregroundColor, ImVec2 buttonSize = { 16.0f, 16.0f });


		

		bool TableRowClickable(const char* id, float rowHeight);
		void Separator(ImVec2 size, ImVec4 color);

		bool IsWindowFocused(const char* windowName, const bool checkRootWindow = true);
		void HelpMarker(const char* desc);

		//=========================================================================================
		/// Images / Textures (Requires e.g Vulkan Implementation)


		ImTextureID GetTextureID(const Asset::Texture* texture);
		void Image(const Asset::Texture* image, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), const ImVec4& border_col = ImVec4(0, 0, 0, 0));
		void Image(const Asset::Texture* image, uint32_t layer, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), const ImVec4& border_col = ImVec4(0, 0, 0, 0));
		bool ImageButton(const Asset::Texture* image, const ImVec2& size, const ImVec4& tint);
		/*

		void _Image(const Asset::Texture* image, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), const ImVec4& border_col = ImVec4(0, 0, 0, 0));
		void _Image(const Asset::Texture* image, uint32_t layer, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), const ImVec4& border_col = ImVec4(0, 0, 0, 0));
		void _ImageMip(const Asset::Texture* image, uint32_t mip, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), const ImVec4& border_col = ImVec4(0, 0, 0, 0));
		void _Image(const Asset::Texture* texture, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), const ImVec4& tint_col = ImVec4(1, 1, 1, 1), const ImVec4& border_col = ImVec4(0, 0, 0, 0));
		bool _ImageButton(const Asset::Texture* image, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), int frame_padding = -1, const ImVec4& bg_col = ImVec4(0, 0, 0, 0), const ImVec4& tint_col = ImVec4(1, 1, 1, 1));
		bool _ImageButton(const char* stringID, const Asset::Texture* image, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), int frame_padding = -1, const ImVec4& bg_col = ImVec4(0, 0, 0, 0), const ImVec4& tint_col = ImVec4(1, 1, 1, 1));
		bool _ImageButton(const Asset::Texture* texture, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), int frame_padding = -1, const ImVec4& bg_col = ImVec4(0, 0, 0, 0), const ImVec4& tint_col = ImVec4(1, 1, 1, 1));
		bool _ImageButton(const char* stringID, const Asset::Texture* texture, const ImVec2& size, const ImVec2& uv0 = ImVec2(0, 0), const ImVec2& uv1 = ImVec2(1, 1), int frame_padding = -1, const ImVec4& bg_col = ImVec4(0, 0, 0, 0), const ImVec4& tint_col = ImVec4(1, 1, 1, 1));
			 
		bool _ImageButton(const Asset::Texture* texture, const ImVec2& size, const ImVec4& tint);*/
	}
}
