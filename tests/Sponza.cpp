#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <filesystem>

#include <assimp/Importer.hpp>      // C++ importer interface
#include <assimp/scene.h>           // Output data structure
#include <assimp/postprocess.h>     // Post processing flags

#include "ECS.h"

#include "Camera.h"
#include "SDLRenderGraph.h"
#include "Finally.h"

#include "SDLGPUDevice.h"

#include "MouseNav.h"

#include "ModelCatalog.h"

#include "Shapes.h"

#include "EcsChildTransformPool.h"

class WorldMatrix : public Mat44f
{    
public:
    WorldMatrix& operator=(const Mat44& that)
    {
        this->Mat44f::operator=(that);
        return *this;
    }
};

int main(int, [[maybe_unused]] char* argv[])
{
    Logging::SetLogLevel(spdlog::level::trace);

    auto cwd = std::filesystem::current_path();
    logInfo("Current working directory: {}", cwd.string());

    etry
    {
        pcheck(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());

        SDL_Rect displayRect;
        auto dm = SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &displayRect);
        const int winW = displayRect.w * 0.75;
        const int winH = displayRect.h * 0.75;

        // Create window
        SDL_Window* window = SDL_CreateWindow("SDL3 GPU Cube", winW, winH, SDL_WINDOW_RESIZABLE);
        pcheck(window, SDL_GetError());

        auto windowFinalizer = Finally([window]()
        {
            SDL_DestroyWindow(window);
        });

        auto gdResult = SDLGPUDevice::Create(window);
        pcheck(gdResult, gdResult.error());
        auto gd = *gdResult;

        EcsRegistry reg;
        ModelCatalog modelCatalog;

        auto modelSpec = modelCatalog.LoadFromFile("Box", "C:/Users/kbaca/Downloads/Box.gltf");
        //auto modelSpec= modelCatalog.LoadFromFile("Sponza", "C:/Users/kbaca/Downloads/main_sponza/NewSponza_Main_glTF_003.gltf");
        pcheck(modelSpec, modelSpec.error());

        auto model = gd->CreateModel(*modelSpec);
        pcheck(model, model.error());

        auto eidModel = reg.Create();
        reg.Add(eidModel, TrsTransformf{}, WorldMatrix{}, *model);

        auto eidCamera = reg.Create();

        const Degreesf fov(45);
        
        reg.Add(eidCamera, TrsTransformf{}, WorldMatrix{}, Camera{});

        reg.Get<TrsTransformf>(eidCamera).T = Vec3f{ 0,0,-4 };
        reg.Get<Camera>(eidCamera).SetPerspective(fov, static_cast<float>(winW), static_cast<float>(winH), 0.1f, 1000);

        // Main loop
        bool running = true;
        Radiansf planetSpinAngle(0), moonSpinAngle(0), moonOrbitAngle(0);

        GimbleMouseNav gimbleMouseNav(reg.Get<TrsTransformf>(eidCamera));
        MouseNav* mouseNav = &gimbleMouseNav;

        while (running)
        {
            int windowW, windowH;
            if (!SDL_GetWindowSizeInPixels(window, &windowW, &windowH))
            {
                logError(SDL_GetError());
                continue;
            }

            SDL_Event event;
            bool minimized = false;
            while (running && (minimized ? SDL_WaitEvent(&event) : SDL_PollEvent(&event)))
            {
                switch (event.type)
                {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;

                case SDL_EVENT_WINDOW_MINIMIZED:
                    minimized = true;
                    break;

                case SDL_EVENT_WINDOW_RESTORED:
                case SDL_EVENT_WINDOW_MAXIMIZED:
                    minimized = false;
                    break;

                case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                case SDL_EVENT_WINDOW_FOCUS_GAINED:
                case SDL_EVENT_WINDOW_FOCUS_LOST:
                    mouseNav->ClearButtons();
                    break;

                case SDL_EVENT_MOUSE_MOTION:
                    mouseNav->OnMouseMove(Vec2f(event.motion.xrel, event.motion.yrel));
                    break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    mouseNav->OnMouseDown(Vec2f(event.button.x, event.button.y), Vec2f(windowW, windowH), event.button.button - 1);
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    mouseNav->OnMouseUp(event.button.button - 1);
                    break;

                case SDL_EVENT_MOUSE_WHEEL:
                    mouseNav->OnScroll(Vec2f(event.wheel.x, event.wheel.y));
                    break;

                case SDL_EVENT_KEY_DOWN:
                    mouseNav->OnKeyDown(event.key.scancode);
                    break;

                case SDL_EVENT_KEY_UP:
                    mouseNav->OnKeyUp(event.key.scancode);
                    break;
                }
            }

            if (!running)
            {
                continue;
            }

            // Update model matrix

            reg.Get<Camera>(eidCamera).SetBounds(windowW, windowH);

            SDLRenderGraph renderGraph(gd.Get());

            auto& cameraXform = reg.Get<TrsTransformf>(eidCamera);
            cameraXform = gimbleMouseNav.GetTransform();

            // Transform roots
            for(const auto& tuple : reg.GetView<TrsTransformf, WorldMatrix>())
            {
                auto [eid, xform, worldMat] = tuple;
                worldMat = xform.ToMatrix();
            }

            // Transform parent/child relationships
            for(const auto& tuple : reg.GetView<ChildTransform, WorldMatrix>())
            {
                auto [eid, xform, worldMat] = tuple;

                const EntityId parentId = xform.ParentId;
                if(!parentId.IsValid())
                {
                    worldMat = xform.LocalTransform.ToMatrix();
                }
                else
                {
                    const auto parentWorldMat = reg.Get<WorldMatrix>(parentId);
                    worldMat = parentWorldMat * xform.LocalTransform.ToMatrix();
                }
            }

            // Transform to camera space and render
            for(const auto& cameraTuple : reg.GetView<WorldMatrix, Camera>())
            {
                for(const auto& tuple : reg.GetView<WorldMatrix, RefPtr<Model>>())
                {
                    const auto [eid, worldMat, model] = tuple;
                    renderGraph.Add(worldMat, model);
                }

                const auto [camEid, camWorldMat, camera] = cameraTuple;
                auto renderResult = renderGraph.Render(camWorldMat, camera.GetProjection());
                if (!renderResult)
                {
                    logError(renderResult.error().Message);
                }

                renderGraph.Reset();
            }
        }
    }
    ecatchall;

    SDL_Quit();

    return 0;
}