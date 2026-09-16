#include "System.h"

#include "FileFetcher.h"
#include "ImGuiRenderer.h"
#include "InputMapper.h"
#include "ThreadPool.h"

#include <imgui_impl_sdl3.h>
#include <memory>
#include <optional>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>
#include <utility>

class System::Impl
{
public:
    InputMapper m_InputMapper;
};

System::CreateTask::CreateTask(std::string appName)
    : m_GpuHelperTask(std::move(appName))
{
}

System::CreateTask::~CreateTask()
{
    MLG_ASSERT(IsComplete(), "Destroying task before it is complete");
}

Result<>
System::CreateTask::Begin()
{
    MLG_CHECKV(m_Stage == Stage::None, "Task is already in progress");

    // Set the initial stage to failed to ensure that any early exit will mark the task as failed.
    m_Stage = Stage::Failed;

    MLG_INFO("Creating System...");

    MLG_CHECK(m_GpuHelperTask.Begin(), "Failed to begin GpuHelper creation");

    m_Stage = Stage::CreatingGpuHelper;

    return Result<>::Ok;
}

void
System::CreateTask::Update()
{
    if(!MLG_VERIFY(IsRunning(), "Task is not running"))
    {
        return;
    }

    switch(m_Stage)
    {
        case Stage::None:
            break;

        case Stage::CreatingGpuHelper:
            if(!m_GpuHelperTask.IsComplete())
            {
                m_GpuHelperTask.Update();
                break;
            }

            if(m_GpuHelperTask.Succeeded())
            {
                MLG_INFO("GpuHelper creation succeeded");
                m_Stage = Stage::Succeeded;
            }
            else
            {
                MLG_ERROR("GpuHelper creation failed");
                m_Stage = Stage::Failed;
            }
            break;

        case Stage::Succeeded:
            break;

        case Stage::Failed:
            MLG_ERROR("System creation failed");
            break;
    }
}

bool
System::CreateTask::IsRunning() const
{
    return Stage::None != m_Stage && !IsComplete();
}

bool
System::CreateTask::IsComplete() const
{
    return MLG_VERIFY(m_Stage != Stage::None, "Task is not started")
        && (Stage::Succeeded == m_Stage || Stage::Failed == m_Stage);
}

bool
System::CreateTask::Succeeded() const
{
    MLG_ASSERT(IsComplete(), "Task is not complete");

    return m_Stage == Stage::Succeeded;
}

Result<System>
System::CreateTask::Take()
{
    MLG_CHECKV(IsComplete(), "Task is not complete");
    MLG_CHECKV(Succeeded(), "Task did not succeed");
    MLG_CHECKV(!m_Consumed, "Task result already consumed");

    m_Consumed = true;

    auto gpuHelperResult = m_GpuHelperTask.Take();
    MLG_CHECK(gpuHelperResult, "Failed to get GpuHelper instance");
    std::unique_ptr<GpuHelper> gpuHelper(std::move(*gpuHelperResult));

    auto fileFetcherResult = FileFetcher::Create();
    MLG_CHECK(fileFetcherResult, "Failed to create FileFetcher");
    std::unique_ptr<FileFetcher> fileFetcher(std::move(*fileFetcherResult));

    auto threadPoolResult = ThreadPool::Create();
    MLG_CHECK(threadPoolResult, "Failed to create ThreadPool");
    std::unique_ptr<ThreadPool> threadPool(std::move(*threadPoolResult));

    auto imGuiRendererResult = ImGuiRenderer::Create(*gpuHelper);
    MLG_CHECK(imGuiRendererResult, "Failed to create ImGuiRenderer");
    std::unique_ptr<ImGuiRenderer> imGuiRenderer(std::move(*imGuiRendererResult));

    return System(std::move(gpuHelper),
        std::move(fileFetcher),
        std::move(threadPool),
        std::move(imGuiRenderer));
}

////////// System

System::System(std::unique_ptr<GpuHelper>&& gpuHelper,
    std::unique_ptr<FileFetcher>&& fileFetcher,
    std::unique_ptr<ThreadPool>&& threadPool,
    std::unique_ptr<ImGuiRenderer>&& imGuiRenderer)
    : m_GpuHelper(std::move(gpuHelper)),
      m_FileFetcher(std::move(fileFetcher)),
      m_ThreadPool(std::move(threadPool)),
      m_ImGuiRenderer(std::move(imGuiRenderer)),
      m_Impl(std::make_unique<Impl>())
{
}

System::~System() = default;
System::System(System&&) noexcept = default;
System& System::operator=(System&&) noexcept = default;

void
System::SetActionMapping(const std::span<const ActionMapping> actionMappings)
{
    m_Impl->m_InputMapper = InputMapper(actionMappings);
}

GpuHelper&
System::GetGpuHelper()
{
    return *m_GpuHelper;
}

const GpuHelper&
System::GetGpuHelper() const
{
    return *m_GpuHelper;
}

FileFetcher&
System::GetFileFetcher()
{
    return *m_FileFetcher;
}

const FileFetcher&
System::GetFileFetcher() const
{
    return *m_FileFetcher;
}

ThreadPool&
System::GetThreadPool()
{
    return *m_ThreadPool;
}

const ThreadPool&
System::GetThreadPool() const
{
    return *m_ThreadPool;
}

const ImGuiRenderer&
System::GetImGuiRenderer() const
{
    return *m_ImGuiRenderer;
}

const InputMapper&
System::GetInputMapper() const
{
    return m_Impl->m_InputMapper;
}

void
System::PostQuitEvent()
{
    SDL_Event event;
    event.quit = SDL_QuitEvent //
        {
            .type = SDL_EVENT_QUIT,
            .timestamp = SDL_GetTicksNS(),
        };

    SDL_PushEvent(&event);
}

void
System::ProcessEvents()
{
    m_GpuHelper->GetInstance().ProcessEvents();

    m_FocusEvent = FocusEvent::None;
    m_WindowStateEvent = WindowStateEvent::None;

    m_Impl->m_InputMapper.BeginFrame();

    SDL_Event sdlEvent;
    while(SDL_PollEvent(&sdlEvent))
    {
        ImGui_ImplSDL3_ProcessEvent(&sdlEvent);

        switch(sdlEvent.type)
        {
            case SDL_EVENT_QUIT:
                m_ShouldQuit = true;
                break;

            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_WINDOW_MAXIMIZED:
                m_Minimized = false;
                m_WindowStateEvent = WindowStateEvent::Restored;
                m_Impl->m_InputMapper.Clear();
                break;

            case SDL_EVENT_WINDOW_MINIMIZED:
                m_Minimized = true;
                m_WindowStateEvent = WindowStateEvent::Minimized;
                m_Impl->m_InputMapper.Clear();
                break;

            default:
                break;
        }

        if(IsMinimized() || ShouldQuit())
        {
            continue;
        }

        switch(sdlEvent.type)
        {
            // case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            {
                const uint32_t newWidth = static_cast<uint32_t>(sdlEvent.window.data1);
                const uint32_t newHeight = static_cast<uint32_t>(sdlEvent.window.data2);
                if(!MLG_VERIFY(GetGpuHelper().Resize(newWidth, newHeight)))
                {
                    PostQuitEvent();
                }
            }
            break;

            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_WINDOW_MAXIMIZED:
                m_Minimized = false;
                break;

            // case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                m_FocusEvent = FocusEvent::Gained;
                m_Impl->m_InputMapper.Clear();
                break;

            case SDL_EVENT_WINDOW_FOCUS_LOST:
                m_FocusEvent = FocusEvent::Lost;
                m_Impl->m_InputMapper.Clear();
                break;

            case SDL_EVENT_KEY_DOWN:
                m_Impl->m_InputMapper.OnButtonPressed(InputButtonDevice::Keyboard,
                    static_cast<unsigned>(sdlEvent.key.scancode));
                break;

            case SDL_EVENT_KEY_UP:
                m_Impl->m_InputMapper.OnButtonReleased(InputButtonDevice::Keyboard,
                    static_cast<unsigned>(sdlEvent.key.scancode));
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                m_Impl->m_InputMapper.OnButtonPressed(InputButtonDevice::Mouse,
                    static_cast<unsigned>(sdlEvent.button.button));
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                m_Impl->m_InputMapper.OnButtonReleased(InputButtonDevice::Mouse,
                    static_cast<unsigned>(sdlEvent.button.button));
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                if(sdlEvent.wheel.x != 0)
                {
                    m_Impl->m_InputMapper.OnAxis(InputAxisDevice::MouseWheel,
                        InputAxisIdentifier::X,
                        sdlEvent.wheel.x);
                }

                if(sdlEvent.wheel.y != 0)
                {
                    m_Impl->m_InputMapper.OnAxis(InputAxisDevice::MouseWheel,
                        InputAxisIdentifier::Y,
                        sdlEvent.wheel.y);
                }
                break;

            case SDL_EVENT_MOUSE_MOTION:
                if(sdlEvent.motion.xrel != 0)
                {
                    m_Impl->m_InputMapper.OnAxis(InputAxisDevice::Mouse,
                        InputAxisIdentifier::X,
                        sdlEvent.motion.xrel);
                }

                if(sdlEvent.motion.yrel != 0)
                {
                    m_Impl->m_InputMapper.OnAxis(InputAxisDevice::Mouse,
                        InputAxisIdentifier::Y,
                        sdlEvent.motion.yrel);
                }
                break;

            default:
                break;
        }
    }

    m_Impl->m_InputMapper.EndFrame();
}

bool
System::SetMouseCaptured(const bool captured)
{
    const bool wasCaptured = IsMouseCaptured();
    SDL_SetWindowRelativeMouseMode(GetGpuHelper().GetWindow(), captured);

    return wasCaptured;
}

bool
System::IsMouseCaptured() const
{
    return SDL_GetWindowRelativeMouseMode(GetGpuHelper().GetWindow());
}