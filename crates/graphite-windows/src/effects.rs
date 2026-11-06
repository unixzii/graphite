//! Composition effect descriptions.
//!
//! `Compositor::CreateEffectFactory` takes an `IGraphicsEffect` that also
//! implements `IGraphicsEffectD2D1Interop`, which names the Direct2D effect
//! and its inputs. Win2D provides these classes for C#; here they are
//! implemented directly.

use std::sync::Mutex;

use windows::Foundation::IPropertyValue;
use windows::Graphics::Effects::{
    IGraphicsEffect, IGraphicsEffect_Impl, IGraphicsEffectSource, IGraphicsEffectSource_Impl,
};
use windows::UI::Composition::CompositionEffectSourceParameter;
use windows::Win32::Foundation::E_INVALIDARG;
use windows::Win32::System::WinRT::Graphics::Direct2D::{
    GRAPHICS_EFFECT_PROPERTY_MAPPING, IGraphicsEffectD2D1Interop, IGraphicsEffectD2D1Interop_Impl,
};
use windows::core::{GUID, HSTRING, Interface, PCWSTR, Result, implement};

/// `CLSID_D2D1AlphaMask`: multiplies the first input by the alpha of the
/// second.
const CLSID_D2D1_ALPHA_MASK: GUID = GUID::from_u128(0xc80ecff0_3fd5_4f05_8328_c5d1724b4f0a);

/// Names of the effect's source parameters.
pub(crate) const SOURCE: &str = "source";
pub(crate) const MASK: &str = "mask";

/// Direct2D's alpha-mask effect with two named source parameters.
#[implement(IGraphicsEffect, IGraphicsEffectSource, IGraphicsEffectD2D1Interop)]
struct AlphaMask {
    name: Mutex<HSTRING>,
    sources: [IGraphicsEffectSource; 2],
}

/// `source` masked by the alpha of `mask` (see [`SOURCE`] and [`MASK`]).
pub(crate) fn alpha_mask() -> Result<IGraphicsEffect> {
    let param = |name: &str| -> Result<IGraphicsEffectSource> {
        CompositionEffectSourceParameter::Create(&HSTRING::from(name))?.cast()
    };
    Ok(AlphaMask { name: Mutex::new(HSTRING::new()), sources: [param(SOURCE)?, param(MASK)?] }.into())
}

impl IGraphicsEffect_Impl for AlphaMask_Impl {
    fn Name(&self) -> Result<HSTRING> {
        Ok(self.name.lock().unwrap().clone())
    }

    fn SetName(&self, name: &HSTRING) -> Result<()> {
        *self.name.lock().unwrap() = name.clone();
        Ok(())
    }
}

impl IGraphicsEffectSource_Impl for AlphaMask_Impl {}

impl IGraphicsEffectD2D1Interop_Impl for AlphaMask_Impl {
    fn GetEffectId(&self) -> Result<GUID> {
        Ok(CLSID_D2D1_ALPHA_MASK)
    }

    fn GetNamedPropertyMapping(
        &self,
        _name: &PCWSTR,
        _index: *mut u32,
        _mapping: *mut GRAPHICS_EFFECT_PROPERTY_MAPPING,
    ) -> Result<()> {
        Err(E_INVALIDARG.into())
    }

    fn GetPropertyCount(&self) -> Result<u32> {
        Ok(0)
    }

    fn GetProperty(&self, _index: u32) -> Result<IPropertyValue> {
        Err(E_INVALIDARG.into())
    }

    fn GetSource(&self, index: u32) -> Result<IGraphicsEffectSource> {
        self.sources.get(index as usize).cloned().ok_or_else(|| E_INVALIDARG.into())
    }

    fn GetSourceCount(&self) -> Result<u32> {
        Ok(self.sources.len() as u32)
    }
}
