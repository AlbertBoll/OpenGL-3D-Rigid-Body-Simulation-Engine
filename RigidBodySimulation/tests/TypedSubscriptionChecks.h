#pragma once
#include "Managers/EventManager.h"
#include "GeometryAuthoringChecks.h"
#include "Physics/ShapeConvex.h"
#include <chrono>
#include <fstream>
#include <filesystem>
#include <thread>
#ifdef GENGINE_SUBSCRIPTION_NATIVE_OBSERVER
#include <glad/glad.h>
#endif

namespace PreEditorValidation
{
    inline ::GEngine::PlatformResult SubscriptionFailure(const char* message)
    {
        ::GEngine::Log::GetCoreLogger()->error("PRE_EDITOR_PHASE_13_FAIL {}", message);
        return std::unexpected(::GEngine::PlatformError{::GEngine::PlatformErrorCode::Initialization,
            "Phase 13 subscription checks", message});
    }
#define SUB13_REQUIRE(condition, message) do { if (!(condition)) return SubscriptionFailure(message); ++checks; } while (false)

    inline ::GEngine::PlatformResult CheckTypedSubscriptionLifetime(::GEngine::CompletionQueue& queue)
    {
        using namespace ::GEngine;
        std::size_t checks = 0;
        {
            Component::Transform3DComponent original;
            int calls = 0;
            auto token = original.OnScaleChanged.ConnectScoped([&](const Math::Vec3f&) { ++calls; });
            SUB13_REQUIRE(token, "Transform move setup");
            Component::Transform3DComponent moved(std::move(original)), assigned;
            moved.SetScale(2.f);
            assigned = std::move(moved); assigned.SetScale(3.f);
            SUB13_REQUIRE(calls == 2 && !original.OnScaleChanged && !moved.OnScaleChanged,
                "Transform move construction and assignment transfer live binding");
            auto copy = assigned; Component::Transform3DComponent copyAssigned;
            copyAssigned = assigned; copy.SetScale(4.f); copyAssigned.SetScale(5.f);
            SUB13_REQUIRE(calls == 2 && !copy.OnScaleChanged && !copyAssigned.OnScaleChanged,
                "Transform copy construction and assignment omit runtime binding");
            assigned.SetScale(6.f);
            SUB13_REQUIRE(calls == 3, "copy isolation retains source connection");
        }
        {
            TypedSubscriptions<void(const int&)> source;
            int calls = 0;
            auto callback = [&](const int&) { ++calls; };
            auto first = source.Subscribe(callback), second = source.Subscribe(callback);
            SUB13_REQUIRE(first && second, "same callable type subscriptions");
            auto old = source.Target<int>(*first);
            const auto id = first->Identity();
            SUB13_REQUIRE(old && first->Reset() && first->Reset() && source.Dispatch(1) && calls == 1,
                "release exactly one connection and release twice");
            auto replacement = source.Subscribe(callback);
            SUB13_REQUIRE(replacement && replacement->Identity().index == id.index &&
                replacement->Identity().generation != id.generation, "slot generation reused safely");
            auto stale = old->Deliver(1);
            SUB13_REQUIRE(!stale && stale.error() == SubscriptionError::StaleTarget && calls == 1, "old identity rejected");
            Subscription moved = std::move(*replacement);
            SUB13_REQUIRE(!*replacement && moved && moved.Reset(), "move transfers exactly once");
            bool wrongDispatch = false, wrongSubscribe = false, wrongRelease = false, wrongDrain = false;
            std::thread worker([&] {
                auto dispatched = source.Dispatch(1); auto subscribed = source.Subscribe(callback);
                auto released = second->Reset(); auto drained = queue.Drain();
                wrongDispatch = !dispatched && dispatched.error() == SubscriptionError::WrongThread;
                wrongSubscribe = !subscribed && subscribed.error() == SubscriptionError::WrongThread;
                wrongRelease = !released && released.error() == SubscriptionError::WrongThread;
                wrongDrain = !drained && drained.error() == SubscriptionError::WrongThread;
            }); worker.join();
            SUB13_REQUIRE(wrongDispatch && wrongSubscribe && wrongRelease && wrongDrain && calls == 1 && *second,
                "wrong-thread operations do not mutate or deliver");
        }
        {
            TypedSubscriptions<void(int)> source;
            std::vector<int> trace;
            Subscription self, cross, added;
            bool callbackOk = true, destructed = false;
            struct Lifetime { bool* flag; ~Lifetime() { *flag = true; } };
            auto first = source.Subscribe([&, lifetime = std::make_unique<Lifetime>(&destructed)](int depth) {
                trace.push_back(1);
                callbackOk = callbackOk && bool(self.Reset()) && bool(cross.Reset()) && !destructed;
                auto fresh = source.Subscribe([&](int) { trace.push_back(3); });
                if (!fresh) { callbackOk = false; return; }
                added = std::move(*fresh);
                if (!depth) callbackOk = callbackOk && bool(source.Dispatch(1));
                callbackOk = callbackOk && !destructed;
            });
            SUB13_REQUIRE(first, "self release setup"); self = std::move(*first);
            auto second = source.Subscribe([&](int) { trace.push_back(2); });
            auto last = source.Subscribe([&](int) { trace.push_back(4); });
            SUB13_REQUIRE(second && last, "cross release setup"); cross = std::move(*second);
            SUB13_REQUIRE(source.Dispatch(0) && callbackOk && destructed && trace == std::vector<int>({1,4,4}),
                "self/cross unsubscribe, live callable and nested trace");
            trace.clear();
            SUB13_REQUIRE(source.Dispatch(1) && trace == std::vector<int>({4,3}), "addition eligible only next top-level dispatch");
        }
        {
            TypedSubscriptions<void()> source;
            int calls = 0; bool bounded = false;
            auto token = source.Subscribe([&] { ++calls; auto nested = source.Dispatch();
                if (!nested) bounded = nested.error() == SubscriptionError::DepthLimit; });
            SUB13_REQUIRE(token && source.Dispatch() && calls == 16 && bounded, "depth limit before seventeenth delivery");
        }
        {
            auto source = std::make_unique<TypedSubscriptions<void()>>();
            auto token = source->Subscribe([&] { source.reset(); });
            SUB13_REQUIRE(token && source->Dispatch() && !source && token->Reset(), "publisher destruction during callback");
        }
        {
            TypedSubscriptions<void(const int&)> source;
            int calls = 0; bool inOwner = true;
            const auto owner = std::this_thread::get_id();
            auto token = source.Subscribe([&](const int&) { ++calls; inOwner &= owner == std::this_thread::get_id(); });
            SUB13_REQUIRE(token, "completion target setup");
            auto target = source.Target<int>(*token);
            SUB13_REQUIRE(target, "typed target");
            for (int i = 0; i < 256; ++i) SUB13_REQUIRE(queue.Post(*target, i), "fill exact capacity");
            auto full = queue.Post(*target, 256);
            SUB13_REQUIRE(!full && full.error() == SubscriptionError::Full, "overflow rejects new entry");
            for (int i = 0; i < 4; ++i)
            {
                auto drained = queue.Drain();
                SUB13_REQUIRE(drained && drained->examined == 64 && drained->delivered == 64 &&
                    drained->backlog == std::size_t(192 - 64*i), "four bounded prefix drains");
            }
            SUB13_REQUIRE(calls == 256 && inOwner, "exact deferred callback count and thread");
            SUB13_REQUIRE(queue.Post(*target, 1) && queue.Cancel(token->Identity()) == 1, "explicit pending cancellation");
            auto cancelled = queue.Drain();
            SUB13_REQUIRE(cancelled && cancelled->cancelled == 1 && calls == 256, "cancelled entry consumes budget without invocation");
            SUB13_REQUIRE(queue.Post(*target, 2) && token->Reset(), "queued owner destruction");
            auto replacement = source.Subscribe([&](const int&) { calls += 10000; });
            auto stale = queue.Drain();
            SUB13_REQUIRE(replacement && stale && stale->stale == 1 && calls == 256, "queued stale generation cannot reach recreated owner");
            auto fresh = source.Target<int>(*replacement);
            SUB13_REQUIRE(fresh, "worker target");
            bool posted = false;
            std::chrono::nanoseconds wait{};
            std::thread worker([&] { const auto begin = std::chrono::steady_clock::now();
                posted = bool(queue.Post(*fresh, 3)); wait = std::chrono::steady_clock::now() - begin; });
            worker.join();
            SUB13_REQUIRE(posted && queue.Drain() && calls == 10256, "worker post only executes on owner drain");
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_13_WORKER_POST_NS {} (one functional handoff, no contention throughput claim)", wait.count());
        }
        {
            struct Large { std::array<std::byte,257> value{}; };
            TypedSubscriptions<void(const Large&)> source;
            auto token = source.Subscribe([](const Large&) {});
            SUB13_REQUIRE(token, "oversize target setup"); auto target = source.Target<Large>(*token);
            SUB13_REQUIRE(target, "oversize target"); auto oversized = queue.Post(*target, Large{});
            SUB13_REQUIRE(!oversized && oversized.error() == SubscriptionError::PayloadTooLarge && queue.Observations().backlog == 0,
                "oversized payload leaves queue unchanged");
        }
        {
            TypedSubscriptions<void(const int&)> source;
            CompletionTarget<int> target;
            int calls = 0; bool nestedRejected = false, posted = false;
            auto token = source.Subscribe([&](const int&) {
                ++calls; auto nested = queue.Drain(); nestedRejected = !nested && nested.error() == SubscriptionError::ReentrantDrain;
                if (calls == 1) posted = bool(queue.Post(target, 2));
            });
            SUB13_REQUIRE(token, "post while draining setup"); auto made = source.Target<int>(*token);
            SUB13_REQUIRE(made, "post while draining target"); target = *made;
            SUB13_REQUIRE(queue.Post(target, 1), "initial prefix"); auto first = queue.Drain();
            SUB13_REQUIRE(first && first->examined == 1 && first->backlog == 1 && calls == 1 && posted && nestedRejected,
                "new posts wait and nested drain rejected");
            SUB13_REQUIRE(queue.Drain() && calls == 2, "next prefix processes new post");
            CompletionQueue closed;
            SUB13_REQUIRE(closed.Post(target, 1), "teardown pending payload"); closed.Close();
            auto late = closed.Post(target, 1);
            SUB13_REQUIRE(!late && late.error() == SubscriptionError::Closed && calls == 2 && closed.Observations().backlog == 0,
                "teardown discards pending work and rejects late posts");
        }
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_13_LIFETIME_PASS checks={}", checks);
        return {};
    }

    inline ::GEngine::PlatformResult CheckSubscriptionScaleBridge(::GEngine::SceneRenderResources& owner,
        ::GEngine::Asset::MeshHandle cube, ::GEngine::Asset::MaterialInstanceHandle material)
    {
        using namespace ::GEngine;
        using namespace ::GEngine::Component;
        std::size_t checks = 0;
        auto scene = CreateRefPtr<_Scene>();
        auto sphere = scene->CreateEntity("subscription-sphere");
        auto box = scene->CreateEntity("subscription-box");
        auto convex = scene->CreateEntity("subscription-convex");
        SUB13_REQUIRE(sphere && box && convex, "bridge entities");
        for (auto entity : {*sphere, *box, *convex})
        {
            SUB13_REQUIRE(owner.AssignRenderable(entity, {cube, material}), "bridge renderer assignment");
            SUB13_REQUIRE(owner.AttachPhysicsShape(entity, entity == *sphere ? "Sphere" : "Diamond"), "CPU shape attachment");
            entity.AddOrReplaceComponent<RigidBody3DComponent>();
        }
        SphereFixture3DComponent sphereFixture; sphereFixture.Radius = 2;
        sphere->AddOrReplaceComponent<SphereFixture3DComponent>(sphereFixture);
        box->AddOrReplaceComponent<BoxFixture3DComponent>();
        convex->AddOrReplaceComponent<ConvexFixture3DComponent>();
        box->Transform().SetScale({2,3,4}); convex->Transform().SetScale({2,3,4});
        struct Cleanup
        {
            RefPtr<_Scene> scene;
            std::vector<std::unique_ptr<PhysicalShape>> shapes;
            void Capture() { for (auto* body : scene->GetPhysicsSystem()->GetPhysicsWorld()->GetPhysicsBodies()) shapes.emplace_back(body->m_Shape); }
            ~Cleanup() { scene->OnRuntimeStop(); }
        } cleanup{scene};
        SUB13_REQUIRE(scene->OnRuntimeStart(), "bridge startup"); cleanup.Capture();
        auto* sphereShape = dynamic_cast<ShapeSphere*>(sphere->GetComponent<RigidBody3DComponent>().RuntimeBody->m_Shape);
        auto* boxShape = dynamic_cast<ShapeBox*>(box->GetComponent<RigidBody3DComponent>().RuntimeBody->m_Shape);
        auto* convexShape = dynamic_cast<ShapeConvex*>(convex->GetComponent<RigidBody3DComponent>().RuntimeBody->m_Shape);
        SUB13_REQUIRE(sphereShape && boxShape && convexShape, "bridge shape classes");
        SUB13_REQUIRE(sphereShape->IsValid() && boxShape->IsValid() && convexShape->IsValid(),
            "bridge source geometry creates valid shapes before scale checks");
        SUB13_REQUIRE(AuthoredClose(boxShape->GetBounds().WidthX(), convexShape->GetBounds().WidthX()*2) &&
            AuthoredClose(boxShape->GetBounds().WidthY(), convexShape->GetBounds().WidthY()*3) &&
            AuthoredClose(boxShape->GetBounds().WidthZ(), convexShape->GetBounds().WidthZ()*4),
            "box includes initial scale; convex source remains unit scale");
        int notifications = 0; Math::Vec3f argument{}, previous{};
        auto observed = sphere->Transform().OnScaleChanged.ConnectScoped([&](const Math::Vec3f& value) {
            ++notifications; argument = value; previous = sphere->Transform().Scale;
        });
        SUB13_REQUIRE(observed, "setter observation");
        sphere->Transform().SetScale(1.f);
        SUB13_REQUIRE(notifications == 0 && sphereShape->GetRadius() == 2, "unchanged scalar setter silent");
        sphere->Transform().SetScale({2,3,4});
        SUB13_REQUIRE(notifications == 1 && argument == Math::Vec3f(2,3,4) && previous == Math::Vec3f(1) &&
            sphereShape->GetRadius() == 4, "new callback value before storage and X-axis shape policy");
        sphere->Transform().SetScale({2,3,4}); sphere->Transform().Scale = {3,3,3};
        SUB13_REQUIRE(notifications == 1 && sphereShape->GetRadius() == 4, "unchanged vector and direct write silent");
        sphere->Transform().SetScale(4.f);
        SUB13_REQUIRE(notifications == 2 && sphereShape->GetRadius() == 8, "changed scalar and absolute base");
        sphereShape->SetRadius(3); sphere->Transform().SetScale(2.f);
        SUB13_REQUIRE(sphereShape->GetRadius() == 6, "explicit radius replaces base source");
        for (auto pair : {std::pair{*box, static_cast<PhysicalShape*>(boxShape)}, std::pair{*convex, static_cast<PhysicalShape*>(convexShape)}})
        {
            const auto base = pair.second->GetBounds();
            pair.first.Transform().SetScale({-.5f,1.5f,2}); const auto scaled = pair.second->GetBounds();
            SUB13_REQUIRE(AuthoredClose(scaled.WidthX(), base.WidthX()*.5) && AuthoredClose(scaled.WidthY(), base.WidthY()*1.5) &&
                AuthoredClose(scaled.WidthZ(), base.WidthZ()*2), "signed nonuniform point scaling");
            pair.first.Transform().SetScale(2.f); pair.first.Transform().SetScale({-.5f,1.5f,2});
            SUB13_REQUIRE(pair.second->GetBounds().mins == scaled.mins && pair.second->GetBounds().maxs == scaled.maxs, "repeated scaling retains base source");
            const auto revision = pair.second->GetRevision(); pair.first.Transform().SetScale({0,1,1});
            SUB13_REQUIRE(pair.second->GetRevision() == revision && pair.second->GetBounds().mins == scaled.mins, "invalid shape rejected without new setter policy");
        }
        SUB13_REQUIRE(observed->Reset(), "detach observer before Scene copies");
        auto duplicate = sphere->Duplicate(); auto copied = _Scene::Copy(scene);
        SUB13_REQUIRE(duplicate && copied && duplicate->GetComponent<RigidBody3DComponent>().RuntimeBody == nullptr,
            "live duplicate and Scene copy");
        duplicate->Transform().SetScale(7.f);
        auto copiedSphere = (*copied)->FindEntityByName("subscription-sphere");
        SUB13_REQUIRE(copiedSphere, "copied sphere"); copiedSphere.Transform().SetScale(9.f);
        SUB13_REQUIRE(sphereShape->GetRadius() == 6, "copies do not borrow source runtime connection");
        sphere->Transform().SetScale(3.f);
        SUB13_REQUIRE(sphereShape->GetRadius() == 9, "copy does not disconnect source");
        scene->OnRuntimeStop(); sphere->Transform().SetScale(4.f);
        SUB13_REQUIRE(sphereShape->GetRadius() == 9 && !sphere->Transform().OnScaleChanged, "stop disconnects before shape retirement");
        SUB13_REQUIRE(scene->DestroyEntity(*duplicate), "remove copied authoring body before restart");
        box->Transform().Scale = {1,1,1}; convex->Transform().Scale = {1,1,1};
        SUB13_REQUIRE(scene->OnRuntimeStart(), "bridge restart"); cleanup.Capture();
        auto* restarted = dynamic_cast<ShapeSphere*>(sphere->GetComponent<RigidBody3DComponent>().RuntimeBody->m_Shape);
        sphere->Transform().SetScale(2.f);
        SUB13_REQUIRE(restarted && restarted->GetRadius() == 4, "fresh runtime connection after restart");
        scene->OnRuntimeStop();
        {
            // Deleting a non-last component relocates the last Transform using
            // the actual EnTT packed storage, while its shape callback stays live.
            auto relocation = CreateRefPtr<_Scene>();
            auto removed = relocation->CreateEntity("relocation-hole"), survivor = relocation->CreateEntity("relocation-survivor");
            SUB13_REQUIRE(removed && survivor, "packed relocation entities");
            survivor->AddOrReplaceComponent<RigidBody3DComponent>();
            survivor->AddOrReplaceComponent<SphereFixture3DComponent>(sphereFixture);
            Cleanup relocationCleanup{relocation};
            SUB13_REQUIRE(relocation->OnRuntimeStart(), "packed relocation runtime"); relocationCleanup.Capture();
            auto* shape = dynamic_cast<ShapeSphere*>(survivor->GetComponent<RigidBody3DComponent>().RuntimeBody->m_Shape);
            auto* previousAddress = &survivor->Transform();
            SUB13_REQUIRE(shape && relocation->DestroyEntity(*removed) && &survivor->Transform() != previousAddress,
                "non-last entity removal relocates live Transform");
            const auto revision = shape->GetRevision(); survivor->Transform().SetScale(2.f);
            SUB13_REQUIRE(shape->GetRadius() == 4 && shape->GetRevision() == revision+1,
                "relocated live bridge receives exactly one changed setter");
            auto another = relocation->CreateEntity("relocation-swap");
            SUB13_REQUIRE(another, "packed swap entity");
            relocation->Reg().storage<Transform3DComponent>().swap_elements(
                static_cast<entt::entity>(*survivor), static_cast<entt::entity>(*another));
            const auto after = shape->GetRevision(); survivor->Transform().SetScale(3.f);
            SUB13_REQUIRE(shape->GetRadius() == 6 && shape->GetRevision() == after+1,
                "packed swap transfers live bridge");
        }
        // A later invalid sphere exercises rollback after a valid body when the
        // registry visits reverse creation order; verify that order explicitly.
        auto failedScene = CreateRefPtr<_Scene>();
        auto bad = failedScene->CreateEntity("invalid-first-created"), good = failedScene->CreateEntity("valid-last-created");
        SUB13_REQUIRE(bad && good, "rollback entities");
        for (auto entity : {*bad,*good}) { entity.AddOrReplaceComponent<RigidBody3DComponent>(); entity.AddOrReplaceComponent<SphereFixture3DComponent>(); }
        bad->GetComponent<SphereFixture3DComponent>().Radius = -1;
        auto visit = failedScene->GetAllEntitiesWith<RigidBody3DComponent>();
        SUB13_REQUIRE(*visit.begin() == static_cast<entt::entity>(*good), "rollback fixture visits valid body before invalid body");
        auto attempted = failedScene->OnRuntimeStart();
        SUB13_REQUIRE(!attempted && good->GetComponent<RigidBody3DComponent>().RuntimeBody == nullptr &&
            !good->Transform().OnScaleChanged && !bad->Transform().OnScaleChanged, "failed startup releases partial bridge");
        Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_13_SCALE_PASS checks={}", checks);
        return {};
    }

    struct SubscriptionCostPayload { std::uint64_t id{}, posted{}, pad0{}, pad1{}; };
    static_assert(sizeof(SubscriptionCostPayload) == 32);
    inline ::GEngine::PlatformResult CheckSubscriptionPresentation(unsigned width, unsigned height)
    {
#ifdef GENGINE_SUBSCRIPTION_NATIVE_OBSERVER
        static unsigned frames = 0;
        if (glGetError() != GL_NO_ERROR) return SubscriptionFailure("GL error in maintained RBS frame");
        if (++frames == 5)
        {
            const auto* renderer = glGetString(GL_RENDERER); const auto* version = glGetString(GL_VERSION);
            if (!renderer || !version || !width || !height) return SubscriptionFailure("missing runtime presentation identity");
            ::GEngine::Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_13_PRESENTATION_PASS viewport={},{} renderer={} version={}",
                width, height, reinterpret_cast<const char*>(renderer), reinterpret_cast<const char*>(version));
        }
        return {};
#else
        return SubscriptionFailure("presentation validation requires the isolated native observer build");
#endif
    }
    inline ::GEngine::PlatformResult ObserveSubscriptionFrame()
    {
        using namespace ::GEngine;
#ifndef GENGINE_CONFIG_RELEASE
        static bool reported = false;
        if (!reported) { Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_13_COMPLETE Debug functional only"); reported = true; }
        return {};
#else
        using Clock = std::chrono::steady_clock;
        const auto ns = [] { return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count(); };
        struct Cost
        {
            TypedSubscriptions<void(const SubscriptionCostPayload&)> synchronous, deferred;
            std::array<Subscription,8> listeners;
            Subscription completion;
            CompletionTarget<SubscriptionCostPayload> target;
            CompletionQueue queue;
            SubscriptionObservations syncObs, deferredObs;
            std::ofstream frames, events;
            std::array<std::uint64_t,64> latency{};
            std::uint64_t synchronousCalls{}, deferredCalls{};
            unsigned frame{};
            bool initialized{}, on{};
        };
        static Cost cost;
        if (!cost.initialized)
        {
            const char* directory = std::getenv("GENGINE_PRE_EDITOR_SUBSCRIPTION_OUTPUT");
            if (!directory) return SubscriptionFailure("missing measurement destination");
            const auto path = std::filesystem::weakly_canonical(directory);
            const auto allowed = std::filesystem::weakly_canonical("C:/dev/GEngine-pre-editor/logs/pre_editor/phase-13");
            const auto relative = path.lexically_relative(allowed);
            if (relative.empty() || relative.native().starts_with(L"..") || !std::filesystem::is_directory(path))
                return SubscriptionFailure("escaping measurement destination");
            if (std::filesystem::exists(path/"cost-frames.csv") || std::filesystem::exists(path/"cost-events.csv"))
                return SubscriptionFailure("measurement destination already used");
            cost.frames.open(path/"cost-frames.csv"); cost.events.open(path/"cost-events.csv");
            if (!cost.frames || !cost.events) return SubscriptionFailure("measurement output unavailable");
            cost.frames << "pair,on,sample,clock_pair_ns,sync_batch_ns,queue_batch_ns,sync_calls,queue_calls,allocations,allocated_bytes,lookups,payload_copies,payload_moves,peak_backlog,backlog\n";
            cost.events << "pair,sample,event,sync_ns,queue_to_callback_ns\n";
            for (auto& listener : cost.listeners)
            {
                auto token = cost.synchronous.Subscribe([](const SubscriptionCostPayload&) { ++cost.synchronousCalls; });
                if (!token) return SubscriptionFailure("cost listener allocation"); listener = std::move(*token);
            }
            auto token = cost.deferred.Subscribe([ns](const SubscriptionCostPayload& value) {
                ++cost.deferredCalls;
                if (cost.on) cost.latency[value.id] = ns() - value.posted;
            });
            if (!token) return SubscriptionFailure("cost completion allocation"); cost.completion = std::move(*token);
            auto target = cost.deferred.Target<SubscriptionCostPayload>(cost.completion);
            if (!target) return SubscriptionFailure("cost target"); cost.target = *target;
            cost.initialized = true;
        }
        if (cost.frame == 2160) return {};
        const unsigned pair = cost.frame/720, within = cost.frame%360;
        cost.on = (cost.frame%720) >= 360;
        const bool measured = within >= 120;
        cost.syncObs = {}; cost.deferredObs = {};
        (void)cost.synchronous.Observe(cost.on ? &cost.syncObs : nullptr);
        (void)cost.deferred.Observe(cost.on ? &cost.deferredObs : nullptr);
        cost.queue.Observe(cost.on);
        std::array<std::uint64_t,64> sync{};
        const auto syncBefore = cost.synchronousCalls, queueBefore = cost.deferredCalls;
        const auto clockStart = ns(); const auto clockEnd = ns();
        const auto syncBegin = ns();
        for (unsigned i=0; i<64; ++i)
        {
            SubscriptionCostPayload value{i}; const auto begin = cost.on ? ns() : 0;
            if (!cost.synchronous.Dispatch(value)) return SubscriptionFailure("cost synchronous dispatch");
            if (cost.on) sync[i] = ns() - begin;
        }
        const auto syncEnd = ns(); const auto queueBegin = ns();
        for (unsigned i=0; i<64; ++i)
        {
            SubscriptionCostPayload value{i, cost.on ? std::uint64_t(ns()) : 0};
            if (!cost.queue.Post(cost.target, value)) return SubscriptionFailure("cost queue post");
        }
        auto drained = cost.queue.Drain(); const auto queueEnd = ns();
        if (!drained || drained->examined != 64 || drained->delivered != 64 || drained->backlog ||
            cost.synchronousCalls - syncBefore != 512 || cost.deferredCalls - queueBefore != 64)
            return SubscriptionFailure("cost order/count/backlog");
        const auto q = cost.queue.Observations();
        if (measured)
        {
            cost.frames << pair << ',' << cost.on << ',' << within-120 << ',' << clockEnd-clockStart << ','
                << syncEnd-syncBegin << ',' << queueEnd-queueBegin << ",512,64," << cost.syncObs.allocations+cost.deferredObs.allocations
                << ',' << cost.syncObs.allocatedBytes+cost.deferredObs.allocatedBytes << ',' << cost.syncObs.lookups+cost.deferredObs.lookups
                << ',' << q.copies << ',' << q.moves << ',' << q.peakBacklog << ',' << drained->backlog << '\n';
            if (cost.on) for (unsigned i=0;i<64;++i)
                cost.events << pair << ',' << within-120 << ',' << i << ',' << sync[i] << ',' << cost.latency[i] << '\n';
        }
        ++cost.frame;
        if (cost.frame == 2160)
        {
            cost.frames.flush(); cost.events.flush();
            if (!cost.frames || !cost.events) return SubscriptionFailure("measurement write failed");
            cost.frames.close(); cost.events.close();
            (void)cost.synchronous.Observe(nullptr); (void)cost.deferred.Observe(nullptr);
            for (auto& listener : cost.listeners) (void)listener.Reset();
            (void)cost.completion.Reset(); cost.target = {};
            Log::GetCoreLogger()->info("PRE_EDITOR_PHASE_13_COMPLETE Release frames=2160 samples=1440 diagnostic_cost_only");
        }
        return {};
#endif
    }
#undef SUB13_REQUIRE
}
