#ifndef GRAPHITE_WINDOWS_INTERNAL_HPP
#define GRAPHITE_WINDOWS_INTERNAL_HPP

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <graphite/backend.h>
#include <graphite/windows.h>

#include <windows.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dispatcherqueue.h>
#include <dxgi.h>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>
#include <windows.graphics.effects.interop.h>
#include <windows.graphics.interop.h>
#include <windows.ui.composition.interop.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.Effects.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.h>

namespace gr_win
{
using namespace winrt;
using ABI::Windows::Graphics::IGeometrySource2DInterop;
using ABI::Windows::Graphics::Effects::GRAPHICS_EFFECT_PROPERTY_MAPPING;
using ABI::Windows::Graphics::Effects::IGraphicsEffectD2D1Interop;
using ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop;
using ABI::Windows::UI::Composition::ICompositorInterop;
using ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop;
using namespace winrt::Windows::UI::Composition;
using namespace winrt::Windows::Foundation::Numerics;
using winrt::Windows::System::DispatcherQueue;
using winrt::Windows::System::DispatcherQueueController;
using winrt::Windows::UI::Color;

template <class T, void (*Drop)(T *)> using owned = std::unique_ptr<T, decltype(Drop)>;
using path_owner = owned<gr_path, gr_path_unref>;
using list_owner = owned<gr_display_list, gr_display_list_unref>;

struct Context
{
    Compositor compositor{nullptr};
    CompositionGraphicsDevice graphics{nullptr};
    com_ptr<ID2D1Factory1> factory;
    com_ptr<ID2D1Device> device;
    DispatcherQueue queue{nullptr};
    DispatcherQueueController controller{nullptr};
    Context();
    CompositionDrawingSurface surface(uint32_t width, uint32_t height);
    void draw(CompositionDrawingSurface const &surface, std::function<void(ID2D1DeviceContext *, POINT)> const &draw);
    void wait();
};

Context &context();
void pump();
void schedule(gr_scene_link *link);
void report(char const *operation) noexcept;
Color color(gr_color c);
D2D1_COLOR_F d2d_color(gr_color c);
float2 vec(double x, double y);
D2D1_POINT_2F point(gr_point p);
D2D1_RECT_F rect(gr_rect r);
D2D1_MATRIX_3X2_F matrix(gr_affine m);
float4x4 matrix4(gr_affine m);
gr_affine then(gr_affine a, gr_affine b);
gr_rect inset(gr_rect r, double amount);
std::optional<gr_rect> control_bounds(gr_path const *path);
com_ptr<ID2D1PathGeometry> geometry(ID2D1Factory1 *factory, gr_path const *path, gr_fill_rule rule);
CompositionPath composition_path(ID2D1Factory1 *factory, gr_path const *path, gr_fill_rule rule);
winrt::Windows::Graphics::Effects::IGraphicsEffect alpha_mask();
com_ptr<ID2D1Bitmap1> bitmap(ID2D1DeviceContext *dc, gr_image const *image);
void replay(ID2D1DeviceContext *dc, ID2D1Factory1 *factory, gr_display_list const *list, gr_affine base);
extern const gr_text_layout_vtable dwrite_layout_vtable;
void draw_text_layout(ID2D1DeviceContext *dc, gr_text_layout *layout, gr_point origin, gr_color color, float alpha);
bool install(gr_scene *scene, ContainerVisual const &scaled);
HWND create_window(gr_scene *scene, bool track_dpi, DWORD ex, DWORD style, int x, int y, int w, int h, HWND parent);
double window_scale(HWND hwnd);
std::vector<uint8_t> read_window(HWND hwnd, uint32_t width, uint32_t height);
} // namespace gr_win

struct gr_windows_host
{
    size_t refs = 1;
    gr_win::ContainerVisual visual{nullptr};
    gr_win::SpriteVisual backdrop{nullptr};
    winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget target{nullptr};
    HWND hwnd = nullptr;
    uint32_t width = 0, height = 0;
    ~gr_windows_host();
};

#endif
