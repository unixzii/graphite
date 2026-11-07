#include "internal.hpp"

#include <cwchar>
#include <d2d1effects.h>
#include <mutex>

namespace gr_win
{
using namespace winrt::Windows::Graphics::Effects;

struct AlphaMask : implements<AlphaMask, IGraphicsEffect, IGraphicsEffectSource, IGraphicsEffectD2D1Interop>
{
    std::mutex mutex;
    hstring name;
    IGraphicsEffectSource sources[2] = {CompositionEffectSourceParameter(L"source"),
                                        CompositionEffectSourceParameter(L"mask")};

    hstring Name()
    {
        std::lock_guard lock(mutex);
        return name;
    }

    void Name(hstring const &value)
    {
        std::lock_guard lock(mutex);
        name = value;
    }

    HRESULT __stdcall GetEffectId(GUID *id) noexcept override
    {
        *id = {0xc80ecff0, 0x3fd5, 0x4f05, {0x83, 0x28, 0xc5, 0xd1, 0x72, 0x4b, 0x4f, 0x0a}};
        return S_OK;
    }

    HRESULT __stdcall GetNamedPropertyMapping(LPCWSTR, UINT *, GRAPHICS_EFFECT_PROPERTY_MAPPING *) noexcept override
    {
        return E_INVALIDARG;
    }

    HRESULT __stdcall GetPropertyCount(UINT *out) noexcept override
    {
        *out = 0;
        return S_OK;
    }

    HRESULT __stdcall GetProperty(UINT, ABI::Windows::Foundation::IPropertyValue **out) noexcept override
    {
        *out = nullptr;
        return E_INVALIDARG;
    }

    HRESULT __stdcall GetSource(UINT i, ABI::Windows::Graphics::Effects::IGraphicsEffectSource **out) noexcept override
    {
        if (i >= 2)
        {
            *out = nullptr;
            return E_INVALIDARG;
        }
        copy_to_abi(sources[i], *reinterpret_cast<void **>(out));
        return S_OK;
    }

    HRESULT __stdcall GetSourceCount(UINT *out) noexcept override
    {
        *out = 2;
        return S_OK;
    }
};

IGraphicsEffect alpha_mask()
{
    return make<AlphaMask>();
}

/* D2D1 Gaussian blur over the "source" parameter. Its standard deviation, named "BlurAmount", is in device pixels. */
struct GaussianBlur : implements<GaussianBlur, IGraphicsEffect, IGraphicsEffectSource, IGraphicsEffectD2D1Interop>
{
    std::mutex mutex;
    hstring name = L"Blur";
    IGraphicsEffectSource source = CompositionEffectSourceParameter(L"source");

    hstring Name()
    {
        std::lock_guard lock(mutex);
        return name;
    }

    void Name(hstring const &value)
    {
        std::lock_guard lock(mutex);
        name = value;
    }

    HRESULT __stdcall GetEffectId(GUID *id) noexcept override
    {
        *id = {0x1feb6d69, 0x2fe6, 0x4ac9, {0x8c, 0x58, 0x1d, 0x7f, 0x93, 0xe7, 0xa6, 0xa5}};
        return S_OK;
    }

    HRESULT __stdcall GetNamedPropertyMapping(LPCWSTR property, UINT *index,
                                              GRAPHICS_EFFECT_PROPERTY_MAPPING *mapping) noexcept override
    {
        if (std::wcscmp(property, L"BlurAmount"))
            return E_INVALIDARG;
        *index = D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION;
        *mapping = ABI::Windows::Graphics::Effects::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
        return S_OK;
    }

    HRESULT __stdcall GetPropertyCount(UINT *out) noexcept override
    {
        *out = 3;
        return S_OK;
    }

    HRESULT __stdcall GetProperty(UINT i, ABI::Windows::Foundation::IPropertyValue **out) noexcept override
    {
        using winrt::Windows::Foundation::PropertyValue;
        winrt::Windows::Foundation::IInspectable value;
        try
        {
            switch (i)
            {
                case D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION:
                    value = PropertyValue::CreateSingle(0);
                    break;
                case D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION:
                    value = PropertyValue::CreateUInt32(D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED);
                    break;
                case D2D1_GAUSSIANBLUR_PROP_BORDER_MODE:
                    value = PropertyValue::CreateUInt32(D2D1_BORDER_MODE_SOFT);
                    break;
                default:
                    *out = nullptr;
                    return E_INVALIDARG;
            }
        }
        catch (...)
        {
            *out = nullptr;
            return to_hresult();
        }
        copy_to_abi(value, *reinterpret_cast<void **>(out));
        return S_OK;
    }

    HRESULT __stdcall GetSource(UINT i, ABI::Windows::Graphics::Effects::IGraphicsEffectSource **out) noexcept override
    {
        if (i)
        {
            *out = nullptr;
            return E_INVALIDARG;
        }
        copy_to_abi(source, *reinterpret_cast<void **>(out));
        return S_OK;
    }

    HRESULT __stdcall GetSourceCount(UINT *out) noexcept override
    {
        *out = 1;
        return S_OK;
    }
};

IGraphicsEffect gaussian_blur()
{
    return make<GaussianBlur>();
}
} // namespace gr_win
