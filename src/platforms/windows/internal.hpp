#ifndef GRAPHITE_WINDOWS_INTERNAL_HPP
#define GRAPHITE_WINDOWS_INTERNAL_HPP

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <graphite/backend.h>
#include <graphite/windows.h>

#include <windows.h>
#include <algorithm>
#include <atomic>
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
#include <thread>
#include <unordered_map>
#include <unordered_set>
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
winrt::Windows::Graphics::Effects::IGraphicsEffect gaussian_blur();
com_ptr<ID2D1Bitmap1> bitmap(ID2D1DeviceContext *dc, gr_image const *image);
void replay(ID2D1DeviceContext *dc, ID2D1Factory1 *factory, gr_display_list const *list, gr_affine base);
extern const gr_text_layout_vtable dwrite_layout_vtable;
void draw_text_layout(ID2D1DeviceContext *dc, gr_text_layout *layout, gr_point origin, gr_color color, float alpha);
void draw_text_glyphs(ID2D1DeviceContext *dc, gr_text_layout *layout, size_t line, gr_glyph_range glyphs,
                      gr_point origin, gr_color color, float alpha);
bool install(gr_scene *scene, ContainerVisual const &scaled);
HWND create_window(gr_scene *scene, bool track_dpi, DWORD ex, DWORD style, int x, int y, int w, int h, HWND parent);
/* Points a host window's events at host (borrowed), or detaches them with nullptr. */
void set_window_host(HWND hwnd, gr_scene_host *host);
/* Sets the cursor shown over a host window. */
void set_window_cursor(HWND hwnd, gr_cursor cursor);
/* Physical key of a scan code, with 0xE000 set for E0-prefixed keys. */
gr_key_code key_code(unsigned scan);
/* Named key of a virtual key, or GR_KEY_UNKNOWN for keys that type text. */
gr_key named_key(unsigned vk);
/* Reports the host window's client size and scale as GR_EVENT_VIEWPORT. */
void report_viewport(HWND hwnd);
double seconds_now();

/* Waits for compositor clock ticks on a worker thread while a frame is requested and posts each tick to the host
 * window, which delivers GR_EVENT_FRAME on the UI thread. */
class FrameClock
{
  public:
    explicit FrameClock(HWND hwnd);
    ~FrameClock();
    FrameClock(FrameClock const &) = delete;
    FrameClock &operator=(FrameClock const &) = delete;
    void request();
    /* Returns the time of the posted tick in seconds and allows the next post. */
    double take_tick();

  private:
    void run() noexcept;
    HWND hwnd;
    HANDLE stop = nullptr, requested = nullptr;
    std::atomic<bool> pending{false};
    std::atomic<int64_t> tick{0};
    std::thread worker;
};

double window_scale(HWND hwnd);
std::vector<uint8_t> read_window(HWND hwnd, uint32_t width, uint32_t height);
} // namespace gr_win

struct gr_windows_host
{
    std::unique_ptr<gr_win::FrameClock> clock; /* Created on the first frame request of an HWND host. */
    gr_win::ContainerVisual visual{nullptr};
    gr_win::SpriteVisual backdrop{nullptr};
    winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget target{nullptr};
    HWND hwnd = nullptr;
    uint32_t width = 0, height = 0;
    ~gr_windows_host();
};

/** Returns the Windows host behind host, or nullptr for nullptr and hosts of other platforms. */
gr_windows_host *gr_windows_host_data(gr_scene_host const *host);

#endif
