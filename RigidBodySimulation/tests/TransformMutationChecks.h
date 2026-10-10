#pragma once

#include "Component/Component.h"
#include "Core/Log.h"
#include "Core/Platform.h"
#include "Physics/PhysicsSystem.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/ShapeSphere.h"
#include "Physics/ShapeBox.h"
#include "Physics/ShapeConvex.h"
#include "Renderer/SceneRenderResources.h"
#include "Scene/_Entity.h"
#include "Scene/_Scene.h"
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace PreEditorValidation
{
    inline ::GEngine::PlatformResult TransformMutationFailure(const char* message)
    {
        ::GEngine::Log::GetCoreLogger()->error("PRE_EDITOR_PACKET_B_FAIL {}", message);
        return std::unexpected(::GEngine::PlatformError{
            ::GEngine::PlatformErrorCode::Initialization, "Phase 14 Transform mutation", message});
    }

#define TRANSFORM14_REQUIRE(condition, message)                                                    \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
            return TransformMutationFailure(message);                                              \
        ++checks;                                                                                  \
    } while (false)

    inline ::GEngine::PlatformResult CheckAuthoritativeTransformNotifications()
    {
        using namespace ::GEngine;
        using namespace ::GEngine::Component;
        std::size_t checks = 0;
        auto scene = CreateRefPtr<_Scene>();
        auto created = scene->CreateEntity("transform-authoring");
        TRANSFORM14_REQUIRE(created, "authoring entity");
        auto entity = *created;
        auto id = scene->RenderData().Identify(entity);
        TRANSFORM14_REQUIRE(id, "typed identity");
        unsigned notifications = 0;
        bool storedState = true;
        auto token = scene->SubscribeToAuthoritativeTransformChanges(
            [&](const AuthoritativeTransformChange& change)
            {
                ++notifications;
                const auto& transform = entity.Transform();
                storedState = storedState && change.entity == *id &&
                              change.translation == transform.Translation &&
                              change.rotation == transform.QuatRotation &&
                              change.eulerRotation == transform.EulerRotation &&
                              change.scale == transform.Scale;
            });
        TRANSFORM14_REQUIRE(token, "scoped authoritative Transform listener");
        auto moved = scene->SetLocalTranslation(entity, {2, 3, 4});
        TRANSFORM14_REQUIRE(moved && moved->changed && moved->notification && notifications == 1 &&
                                storedState,
                            "successful notification observes stored translation");
        auto same = scene->SetLocalTranslation(entity, {2, 3, 4});
        TRANSFORM14_REQUIRE(same && !same->changed && notifications == 1,
                            "translation no-op silent");
        const auto nan = std::numeric_limits<float>::quiet_NaN();
        const auto inf = std::numeric_limits<float>::infinity();
        auto invalid = scene->SetLocalTranslation(entity, {nan, 0, 0});
        TRANSFORM14_REQUIRE(
            !invalid && invalid.error().code == TransformMutationErrorCode::NonFiniteTransform &&
                notifications == 1 && entity.Transform().Translation == Vec3f(2, 3, 4),
            "invalid translation rejected before store and notification");
        auto rotation = scene->SetLocalRotation(entity, {.1f, .2f, .3f});
        TRANSFORM14_REQUIRE(rotation && rotation->changed && notifications == 2 && storedState,
                            "Euler rotation stored before notification");
        auto pose = scene->SetLocalPose(entity, {3, 4, 5}, Quat{1, 0, 0, 0});
        TRANSFORM14_REQUIRE(pose && pose->changed && notifications == 3 && storedState,
                            "complete local pose authoritative success");
        const auto previous = entity.Transform();
        auto badRotation = scene->SetLocalPose(entity, {7, 8, 9}, Quat{0, 0, 0, 0});
        auto badScale = scene->SetLocalScale(entity, Vec3f{1, inf, 1});
        TRANSFORM14_REQUIRE(!badRotation && !badScale && notifications == 3 &&
                                entity.Transform().Translation == previous.Translation &&
                                entity.Transform().QuatRotation == previous.QuatRotation &&
                                entity.Transform().Scale == previous.Scale,
                            "invalid pose/scale leave state unchanged");
        std::vector<int> order;
        auto internal = entity.Transform().OnScaleChanged.ConnectScoped(
            [&](const Vec3f& scale)
            {
                order.push_back(1);
                storedState =
                    storedState && entity.Transform().Scale == Vec3f(1) && scale == Vec3f(2);
                auto nested = scene->SetLocalTranslation(entity, {99, 0, 0});
                storedState = storedState && !nested &&
                              nested.error().code == TransformMutationErrorCode::MutationActive;
            });
        auto after = scene->SubscribeToAuthoritativeTransformChanges(
            [&](const AuthoritativeTransformChange& change)
            {
                if (change.kind == AuthoritativeTransformChangeKind::Scale)
                    order.push_back(2);
            });
        TRANSFORM14_REQUIRE(internal && after, "pre-store and post-commit observers");
        auto scale = scene->SetLocalScale(entity, 2.f);
        TRANSFORM14_REQUIRE(scale && scale->changed && storedState && notifications == 4 &&
                                order == std::vector<int>({1, 2}),
                            "internal hook then Transform-only committed notification");
        TRANSFORM14_REQUIRE(internal->Reset() && after->Reset(), "detach ordering observers");
        auto noScale = scene->SetLocalScale(entity, Vec3f{2});
        TRANSFORM14_REQUIRE(noScale && !noScale->changed && notifications == 4,
                            "vector scale no-op silent");
        entity.Transform().Scale = {3, 3, 3};
        entity.Transform().SetScale(4.f);
        TRANSFORM14_REQUIRE(notifications == 4,
                            "direct fields and Component setters are distinct compatibility paths");
        auto zero = scene->SetLocalScale(entity, Vec3f{0, -2, 3});
        TRANSFORM14_REQUIRE(zero && zero->changed && notifications == 5 && storedState,
                            "finite zero and signed Transform scale accepted");
        {
            auto extraction = scene->RenderData().BeginExtraction();
            TRANSFORM14_REQUIRE(extraction, "extraction boundary");
            auto rejected = scene->SetLocalTranslation(entity, {0, 0, 0});
            TRANSFORM14_REQUIRE(!rejected &&
                                    rejected.error().code ==
                                        TransformMutationErrorCode::ExtractionActive &&
                                    notifications == 5,
                                "extraction rejection has no successful notification");
        }
        auto other = CreateRefPtr<_Scene>();
        auto foreign = other->CreateEntity("foreign");
        TRANSFORM14_REQUIRE(foreign, "foreign fixture");
        auto wrong = scene->SetLocalTranslation(*foreign, {1, 0, 0});
        auto missing = scene->CreateEntity("missing-transform");
        TRANSFORM14_REQUIRE(missing, "missing-component fixture");
        missing->RemoveComponent<Transform3DComponent>();
        auto absent = scene->SetLocalScale(*missing, 2.f);
        TRANSFORM14_REQUIRE(
            !wrong && wrong.error().code == TransformMutationErrorCode::ForeignEntity && !absent &&
                absent.error().code == TransformMutationErrorCode::MissingTransform,
            "foreign and missing-component requests rejected");
        TRANSFORM14_REQUIRE(scene->DestroyEntity(*missing), "remove missing-component fixture");
        auto stale = scene->SetLocalScale(*missing, 3.f);
        TRANSFORM14_REQUIRE(!stale &&
                                stale.error().code == TransformMutationErrorCode::InvalidEntity &&
                                notifications == 5,
                            "stale identity rejected without notification");
        TRANSFORM14_REQUIRE(token->Reset(), "detach authoring observer");

        // The accepted provider bound rejects delivery 17, after mutation 17 committed.
        unsigned delivered = 0;
        bool deliveryError = false, nestedSuccess = true;
        auto nestedToken = scene->SubscribeToAuthoritativeTransformChanges(
            [&](const AuthoritativeTransformChange& change)
            {
                ++delivered;
                const auto result =
                    scene->SetLocalTranslation(entity, {change.translation.x + 1, 0, 0});
                nestedSuccess = nestedSuccess && result && result->changed;
                if (result && !result->notification)
                    deliveryError = result->notification.error() == SubscriptionError::DepthLimit;
            });
        TRANSFORM14_REQUIRE(nestedToken, "nested listener");
        auto first = scene->SetLocalTranslation(entity, {1, 0, 0});
        TRANSFORM14_REQUIRE(
            first && first->changed && first->notification && nestedSuccess && delivered == 16 &&
                deliveryError && entity.Transform().Translation.x == 17,
            "post-commit delivery error does not become mutation rejection or rollback");
        scene.reset();
        TRANSFORM14_REQUIRE(nestedToken->Reset(),
                            "scoped notification token safely outlives publisher");
        Log::GetCoreLogger()->info(
            "PRE_EDITOR_PACKET_B_NOTIFICATION_PASS checks={} success=Transform_only", checks);
        return {};
    }

    inline ::GEngine::PlatformResult CheckTransformHierarchy()
    {
        using namespace ::GEngine;
        using namespace ::GEngine::Component;
        std::size_t checks = 0;
        auto scene = CreateRefPtr<_Scene>();
        auto parent = scene->CreateEntity("parent"), child = scene->CreateEntity("child");
        auto anchor = scene->CreateEntity("physics-anchor");
        TRANSFORM14_REQUIRE(parent && child && anchor, "hierarchy fixture");
        anchor->AddOrReplaceComponent<RigidBody3DComponent>();
        TRANSFORM14_REQUIRE(scene->SetLocalTranslation(*parent, {10, 0, 0}) &&
                                scene->SetLocalTranslation(*child, {2, 0, 0}) &&
                                scene->SetLocalTranslation(*anchor, {3, 0, 0}),
                            "hierarchy local poses");
        unsigned changed = 0;
        bool storedParent = true;
        auto listener = scene->SubscribeToAuthoritativeTransformChanges(
            [&](const AuthoritativeTransformChange& event)
            {
                ++changed;
                auto resolved = scene->RenderData().Resolve(event.entity);
                storedParent = storedParent && resolved &&
                               _Entity(*resolved, scene.get()).GetParentUUID() == event.parent;
            });
        TRANSFORM14_REQUIRE(listener && child->SetParent(*parent) && anchor->SetParent(*parent),
                            "legacy hierarchy adapter delegates to Scene");
        auto unchanged = scene->SetParent(*child, *parent);
        auto cycle = scene->SetParent(*parent, *child);
        TRANSFORM14_REQUIRE(unchanged && !unchanged->changed && !cycle &&
                                cycle.error().code == TransformMutationErrorCode::Cycle &&
                                changed == 2 && storedParent,
                            "no-op and cycle leave links/notifications unchanged");
        auto snapshot = scene->UpdateWorldTransforms();
        TRANSFORM14_REQUIRE(snapshot, "world hierarchy snapshot");
        const auto childId = scene->RenderData().Identify(*child);
        const auto anchorId = scene->RenderData().Identify(*anchor);
        bool childWorld = false, anchorWorld = false;
        for (const auto& world : snapshot->transforms)
        {
            if (world.entity == *childId)
                childWorld = world.matrix[3].x == 12;
            if (world.entity == *anchorId)
                anchorWorld = world.matrix[3].x == 3;
        }
        TRANSFORM14_REQUIRE(childWorld && anchorWorld && child->Transform().Translation.x == 2,
                            "local/world composition and Physics world-anchor policy preserved");
        auto cached = scene->UpdateWorldTransforms();
        TRANSFORM14_REQUIRE(cached && cached->recomputed == 0,
                            "unchanged hierarchy reuses world cache");
        auto detached = scene->SetParent(*child, {});
        TRANSFORM14_REQUIRE(detached && detached->changed && child->GetParentUUID() == 0 &&
                                child->Transform().Translation.x == 2 && changed == 3,
                            "detach preserves local pose and publishes stored relationship");
        auto other = CreateRefPtr<_Scene>();
        auto foreign = other->CreateEntity("foreign-parent");
        TRANSFORM14_REQUIRE(foreign, "foreign parent fixture");
        auto invalid = child->SetParent(*foreign);
        TRANSFORM14_REQUIRE(!invalid && invalid.error().code == TransformErrorCode::ForeignEntity &&
                                changed == 3,
                            "legacy foreign-parent error retained");
        TRANSFORM14_REQUIRE(listener->Reset(), "hierarchy observer detached");
        Log::GetCoreLogger()->info("PRE_EDITOR_PACKET_B_HIERARCHY_PASS checks={}", checks);
        return {};
    }

    struct TransformPhysicsFixture
    {
        ::GEngine::RefPtr<::GEngine::_Scene> scene = ::GEngine::CreateRefPtr<::GEngine::_Scene>();
        std::vector<std::unique_ptr<::GEngine::PhysicalShape>> shapes;
        void CaptureShapes()
        {
            for (auto* body : scene->GetPhysicsSystem()->GetPhysicsWorld()->GetPhysicsBodies())
                shapes.emplace_back(body->m_Shape);
        }
        // Existing runtime startup transfers shapes to this fixture, not the Physics world.
        ~TransformPhysicsFixture()
        {
            scene->OnRuntimeStop();
        }
    };

    inline ::GEngine::PlatformResult
    CheckTransformColliderPolicy(::GEngine::SceneRenderResources& resources,
                                 ::GEngine::Asset::MeshHandle mesh,
                                 ::GEngine::Asset::MaterialInstanceHandle material)
    {
        using namespace ::GEngine;
        using namespace ::GEngine::Component;
        std::size_t checks = 0;
        TransformPhysicsFixture fixture;
        auto& scene = fixture.scene;
        auto sphere = scene->CreateEntity("sphere"), box = scene->CreateEntity("box");
        auto convex = scene->CreateEntity("convex");
        TRANSFORM14_REQUIRE(sphere && box && convex, "collider fixtures");
        for (auto entity : {*sphere, *box, *convex})
        {
            TRANSFORM14_REQUIRE(
                resources.AssignRenderable(entity, {mesh, material}) &&
                    resources.AttachPhysicsShape(entity, entity == *sphere ? "Sphere" : "Diamond"),
                "existing CPU geometry and render resource assignment");
            entity.AddOrReplaceComponent<RigidBody3DComponent>();
        }
        SphereFixture3DComponent sphereFixture;
        sphereFixture.Radius = 2;
        sphere->AddOrReplaceComponent<SphereFixture3DComponent>(sphereFixture);
        box->AddOrReplaceComponent<BoxFixture3DComponent>();
        convex->AddOrReplaceComponent<ConvexFixture3DComponent>();
        box->Transform().SetScale({2, 3, 4});
        convex->Transform().SetScale({2, 3, 4});
        TRANSFORM14_REQUIRE(scene->OnRuntimeStart(), "collider startup");
        fixture.CaptureShapes();
        auto* sphereShape = dynamic_cast<ShapeSphere*>(
            sphere->GetComponent<RigidBody3DComponent>().RuntimeBody->m_Shape);
        auto* boxShape =
            dynamic_cast<ShapeBox*>(box->GetComponent<RigidBody3DComponent>().RuntimeBody->m_Shape);
        auto* convexShape = dynamic_cast<ShapeConvex*>(
            convex->GetComponent<RigidBody3DComponent>().RuntimeBody->m_Shape);
        TRANSFORM14_REQUIRE(sphereShape && boxShape && convexShape, "expected collider families");
        unsigned notifications = 0;
        auto token = scene->SubscribeToAuthoritativeTransformChanges(
            [&](const AuthoritativeTransformChange&)
            {
                ++notifications;
            });
        TRANSFORM14_REQUIRE(token, "collider-policy Transform observer");
        auto scaled = scene->SetLocalScale(*sphere, Vec3f{2, 9, -4});
        TRANSFORM14_REQUIRE(scaled && scaled->changed && sphereShape->GetRadius() == 4 &&
                                notifications == 1,
                            "sphere X-axis scaling preserved");
        const auto sphereRevision = sphereShape->GetRevision();
        auto rejectedCollider = scene->SetLocalScale(*sphere, Vec3f{-1, 2, 3});
        TRANSFORM14_REQUIRE(
            rejectedCollider && rejectedCollider->changed && rejectedCollider->notification &&
                sphere->Transform().Scale == Vec3f(-1, 2, 3) && notifications == 2 &&
                sphereShape->GetRadius() == 4 && sphereShape->GetRevision() == sphereRevision,
            "Transform commits and notifies while sphere independently rejects collider scale");
        sphereShape->SetRadius(3);
        TRANSFORM14_REQUIRE(scene->SetLocalScale(*sphere, 2.f) && sphereShape->GetRadius() == 6,
                            "explicit radius establishes base independently");
        for (auto pair : {std::pair{*box, static_cast<PhysicalShape*>(boxShape)},
                          std::pair{*convex, static_cast<PhysicalShape*>(convexShape)}})
        {
            TRANSFORM14_REQUIRE(scene->SetLocalScale(pair.first, Vec3f{-.5f, 1.5f, 2}),
                                "signed nonuniform semantic scale");
            const auto bounds = pair.second->GetBounds();
            TRANSFORM14_REQUIRE(scene->SetLocalScale(pair.first, 2.f) &&
                                    scene->SetLocalScale(pair.first, Vec3f{-.5f, 1.5f, 2}) &&
                                    pair.second->GetBounds().mins == bounds.mins &&
                                    pair.second->GetBounds().maxs == bounds.maxs,
                                "point-shape scaling remains relative to base source");
            const auto revision = pair.second->GetRevision();
            const auto before = notifications;
            auto zero = scene->SetLocalScale(pair.first, Vec3f{0, 1, 1});
            TRANSFORM14_REQUIRE(
                zero && zero->changed && zero->notification && notifications == before + 1 &&
                    pair.first.Transform().Scale == Vec3f(0, 1, 1) &&
                    pair.second->GetRevision() == revision &&
                    pair.second->GetBounds().mins == bounds.mins,
                "Transform-only zero-scale success preserves rejected point collider");
        }
        TRANSFORM14_REQUIRE(token->Reset(), "collider policy observer detached");
        Log::GetCoreLogger()->info(
            "PRE_EDITOR_PACKET_B_COLLIDER_POLICY_PASS checks={} atomicity=false", checks);
        return {};
    }

    inline ::GEngine::PlatformResult CheckTransformTeleportInterpolation()
    {
        using namespace ::GEngine;
        using namespace ::GEngine::Component;
        std::size_t checks = 0;
        TransformPhysicsFixture fixture;
        auto& scene = fixture.scene;
        auto entity = scene->CreateEntity("kinematic-interpolation");
        TRANSFORM14_REQUIRE(entity, "interpolation fixture");
        RigidBody3DComponent bodyDescription;
        bodyDescription.Type = BodyType::Kinematic;
        entity->AddOrReplaceComponent<RigidBody3DComponent>(bodyDescription);
        entity->AddOrReplaceComponent<SphereFixture3DComponent>();
        TRANSFORM14_REQUIRE(scene->OnRuntimeStart(), "interpolation startup");
        fixture.CaptureShapes();
        auto* body = entity->GetComponent<RigidBody3DComponent>().RuntimeBody;
        body->m_LinearVelocity = {6, 0, 0};
        unsigned notifications = 0;
        auto token = scene->SubscribeToAuthoritativeTransformChanges(
            [&](const AuthoritativeTransformChange&)
            {
                ++notifications;
            });
        TRANSFORM14_REQUIRE(token, "teleport observer");
        scene->Update(Timestep(_Scene::PhysicsStepSeconds * 1.5));
        auto sampled = scene->GetRenderTransform(*entity);
        const auto close = [](float a, float b)
        {
            return std::abs(a - b) <= 0.0001f;
        };
        TRANSFORM14_REQUIRE(sampled && close(body->m_Position.x, .1f) &&
                                close(sampled->matrix[3].x, .05f) && notifications == 0,
                            "fixed-step publication interpolates without authoring notification");
        auto unchanged = scene->SetLocalTranslation(*entity, entity->Transform().Translation);
        auto noOpSample = scene->GetRenderTransform(*entity);
        TRANSFORM14_REQUIRE(unchanged && !unchanged->changed && noOpSample &&
                                noOpSample->matrix == sampled->matrix && notifications == 0,
                            "no-op pose preserves previous/current history");
        const auto velocity = body->m_LinearVelocity;
        auto invalid = scene->SetLocalPose(*entity, {20, 0, 0}, Quat{0, 0, 0, 0});
        auto failedSample = scene->GetRenderTransform(*entity);
        TRANSFORM14_REQUIRE(!invalid && failedSample && failedSample->matrix == sampled->matrix &&
                                notifications == 0,
                            "rejected teleport leaves body, history and notification unchanged");
        auto teleport = scene->SetLocalTranslation(*entity, {10, 0, 0});
        auto snapped = scene->GetRenderTransform(*entity);
        TRANSFORM14_REQUIRE(
            teleport && teleport->changed && snapped && snapped->matrix[3].x == 10 &&
                body->m_Position.x == 10 && body->m_LinearVelocity == velocity &&
                notifications == 1,
            "accepted authoring teleport synchronizes pose and snaps interpolation, preserving velocity");
        scene->Update(Timestep(0));
        auto noTick = scene->GetRenderTransform(*entity);
        TRANSFORM14_REQUIRE(noTick && noTick->matrix == snapped->matrix && notifications == 1,
                            "teleport is not replayed on zero-time update");
        scene->Update(Timestep(_Scene::PhysicsStepSeconds));
        auto next = scene->GetRenderTransform(*entity);
        TRANSFORM14_REQUIRE(
            next && close(next->matrix[3].x, 10.05f) && notifications == 1,
            "next ordinary tick retains interpolation and no public authoring event");
        TRANSFORM14_REQUIRE(token->Reset(), "teleport observer detached");
        Log::GetCoreLogger()->info("PRE_EDITOR_PACKET_B_INTERPOLATION_PASS checks={}", checks);
        return {};
    }

    inline ::GEngine::PlatformResult
    CheckAuthoritativeTransformMutations(::GEngine::SceneRenderResources& resources,
                                         ::GEngine::Asset::MeshHandle mesh,
                                         ::GEngine::Asset::MaterialInstanceHandle material)
    {
        if (auto result = CheckAuthoritativeTransformNotifications(); !result)
            return result;
        if (auto result = CheckTransformHierarchy(); !result)
            return result;
        if (auto result = CheckTransformColliderPolicy(resources, mesh, material); !result)
            return result;
        return CheckTransformTeleportInterpolation();
    }
#undef TRANSFORM14_REQUIRE
}
