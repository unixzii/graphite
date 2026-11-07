#include "internal.hpp"

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
} // namespace gr_win
