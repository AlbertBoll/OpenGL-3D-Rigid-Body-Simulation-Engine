#pragma once
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>

namespace Rbs::Editor::Numeric
{
    // Missing endpoints mean no semantic bound, not an invented authoring limit.
    template<class T> struct Bounds
    {
        std::optional<T> minimum{},maximum{};
        bool openMinimum{},openMaximum{};
        T Lower() const
        {
            if(!minimum)return std::numeric_limits<T>::lowest();
            if constexpr(std::is_floating_point_v<T>)
                return openMinimum?std::nextafter(*minimum,std::numeric_limits<T>::infinity()):*minimum;
            else return openMinimum && *minimum<std::numeric_limits<T>::max()?T(*minimum+1):*minimum;
        }
        T Upper() const
        {
            if(!maximum)return std::numeric_limits<T>::max();
            if constexpr(std::is_floating_point_v<T>)
                return openMaximum?std::nextafter(*maximum,-std::numeric_limits<T>::infinity()):*maximum;
            else return openMaximum && *maximum>std::numeric_limits<T>::lowest()?T(*maximum-1):*maximum;
        }
    };
    template<class T> Bounds<T> Closed(T low,T high){return {low,high};}
    template<class T> Bounds<T> Nonnegative(){return {T(0)};}
    template<class T> Bounds<T> Positive(){return {T(0),{},true};}
    template<class T> const char* Validate(T value,const Bounds<T>& bounds)
    {
        if constexpr(std::is_floating_point_v<T>)
            if(!std::isfinite(value))return "A finite number is required; value unchanged.";
        if(value<bounds.Lower() || value>bounds.Upper())return "Outside the allowed range; value unchanged.";
        return nullptr;
    }
    template<class T> const char* Parse(std::string_view text,T& value,const Bounds<T>& bounds)
    {
        while(!text.empty() && (text.front()==' '||text.front()=='\t'))text.remove_prefix(1);
        while(!text.empty() && (text.back()==' '||text.back()=='\t'))text.remove_suffix(1);
        if(text.starts_with('+'))
        {
            text.remove_prefix(1);
            if(!text.empty() && (text.front()=='+' || text.front()=='-'))return "Malformed number; value unchanged.";
        }
        if(text.empty())return "Enter a complete number; value unchanged.";
        T proposed{};const auto parsed=std::from_chars(text.data(),text.data()+text.size(),proposed);
        if(parsed.ec==std::errc::result_out_of_range)return "Number is not representable; value unchanged.";
        if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size())return "Malformed number; value unchanged.";
        if(const auto* error=Validate(proposed,bounds))return error;
        value=proposed;return nullptr;
    }
    struct Accept { template<class T> const char* operator()(T) const {return nullptr;} };
}
