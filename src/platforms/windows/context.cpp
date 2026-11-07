#include "internal.hpp"

#include <chrono>
#include <thread>

namespace gr_win
{
Context::Context()
{
    LoadLibraryW(L"dcomp.dll");
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, L"dcomp.dll", &module);
    queue = DispatcherQueue::GetForCurrentThread();
    if (!queue)
    {
        DispatcherQueueOptions options = {sizeof(options), DQTYPE_THREAD_CURRENT, DQTAT_COM_STA};
        HRESULT hr = CreateDispatcherQueueController(
            options, reinterpret_cast<ABI::Windows::System::IDispatcherQueueController **>(put_abi(controller)));
        if (FAILED(hr))
        {
            options.apartmentType = DQTAT_COM_NONE;
            check_hresult(CreateDispatcherQueueController(
                options, reinterpret_cast<ABI::Windows::System::IDispatcherQueueController **>(put_abi(controller))));
        }
        queue = controller.DispatcherQueue();
    }
    compositor = Compositor();
    com_ptr<ID3D11Device> d3d;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                   nullptr, 0, D3D11_SDK_VERSION, d3d.put(), nullptr, nullptr);
    if (FAILED(hr))
        check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                        nullptr, 0, D3D11_SDK_VERSION, d3d.put(), nullptr, nullptr));
    auto dxgi = d3d.as<IDXGIDevice>();
    check_hresult(
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), nullptr, factory.put_void()));
    check_hresult(factory->CreateDevice(dxgi.get(), device.put()));
    auto interop = compositor.as<ICompositorInterop>();
    check_hresult(interop->CreateGraphicsDevice(
        device.get(),
        reinterpret_cast<ABI::Windows::UI::Composition::ICompositionGraphicsDevice **>(put_abi(graphics))));
}

Context &context()
{
    // TLS teardown may run after DLL unloading starts; keep the thread context alive.
    static thread_local Context *instance = new Context();
    return *instance;
}

CompositionDrawingSurface Context::surface(uint32_t w, uint32_t h)
{
    if (!w || !h || w > INT_MAX || h > INT_MAX)
        throw_hresult(E_INVALIDARG);
    return graphics.CreateDrawingSurface({static_cast<float>(w), static_cast<float>(h)},
                                         winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                                         winrt::Windows::Graphics::DirectX::DirectXAlphaMode::Premultiplied);
}

void Context::draw(CompositionDrawingSurface const &surface, std::function<void(ID2D1DeviceContext *, POINT)> const &fn)
{
    auto interop = surface.as<ICompositionDrawingSurfaceInterop>();
    com_ptr<ID2D1DeviceContext> dc;
    POINT offset{};
    check_hresult(interop->BeginDraw(nullptr, __uuidof(ID2D1DeviceContext), dc.put_void(), &offset));
    try
    {
        dc->Clear(D2D1::ColorF(0, 0, 0, 0));
        fn(dc.get(), offset);
    }
    catch (...)
    {
        interop->EndDraw();
        throw;
    }
    check_hresult(interop->EndDraw());
}

void pump()
{
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void Context::wait()
{
    auto operation = compositor.RequestCommitAsync();
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (operation.Status() == winrt::Windows::Foundation::AsyncStatus::Started)
    {
        if (std::chrono::steady_clock::now() >= deadline)
            throw_hresult(HRESULT_FROM_WIN32(WAIT_TIMEOUT));
        pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    operation.GetResults();
}

static thread_local std::vector<std::shared_ptr<gr_scene_link>> pending;
static thread_local bool scheduled = false;

void schedule(gr_scene_link *link)
{
    pending.emplace_back(gr_scene_link_ref(link), gr_scene_link_unref);
    if (scheduled)
        return;
    scheduled = true;
    if (!context().queue.TryEnqueue(
            []
            {
                scheduled = false;
                auto jobs = std::move(pending);
                pending.clear();
                for (auto const &link : jobs)
                {
                    gr_scene *s = gr_scene_link_lock(link.get());
                    if (s)
                    {
                        gr_scene_commit(s);
                        gr_scene_unref(s);
                    }
                }
            }))
        scheduled = false;
}

void report(char const *operation) noexcept
{
    try
    {
        throw;
    }
    catch (hresult_error const &error)
    {
        fprintf(stderr, "graphite-windows: %s failed: 0x%08lx\n", operation,
                static_cast<unsigned long>(error.code().value));
    }
    catch (std::exception const &error)
    {
        fprintf(stderr, "graphite-windows: %s: %s\n", operation, error.what());
    }
    catch (...)
    {
        fprintf(stderr, "graphite-windows: %s failed\n", operation);
    }
}
} // namespace gr_win
