#include <GEngine/Core/Log.h>
#include <GEngine/Physics/PhysicsProfile.h>
#include <GEngine/Physics/PhysicsSystem.h>
#include <GEngine/Physics/PhysicsWorld.h>
#include <GEngine/Physics/ShapeBox.h>
#include <GEngine/Physics/ShapeSphere.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using Clock = std::chrono::steady_clock;
	using GEngine::PhysicsProfileSnapshot;

	struct Options
	{
		std::vector<int> bodyCounts{ 50, 100, 200, 500, 1000, 2000 };
		int warmupCount{ 2 };
		int sampleCount{ 5 };
		int steadyStateWarmupSteps{};
		int steadyStateMeasuredSteps{};
		float dtSeconds{ 1.0f / 120.0f };
		bool physicsRegressionBaseline{};
		int solverIterations{ GEngine::PhysicsSystem::DefaultSolverIterations };
	};

	struct Sample
	{
		PhysicsProfileSnapshot profile;
		double externalStepMs{};
		int measuredSteps{ 1 };
		bool finiteState{ true };
		std::uint64_t finalStateFingerprint{ 14695981039346656037ull };
	};

	std::vector<int> ParseBodyCounts(const std::string& value)
	{
		std::vector<int> counts;
		std::size_t begin = 0;
		while (begin < value.size())
		{
			const std::size_t end = value.find(',', begin);
			const std::string token = value.substr(begin, end - begin);
			const int count = std::stoi(token);
			if (count < 2)
			{
				throw std::invalid_argument("body counts must be at least 2");
			}
			counts.push_back(count);
			if (end == std::string::npos)
			{
				break;
			}
			begin = end + 1;
		}

		if (counts.empty())
		{
			throw std::invalid_argument("at least one body count is required");
		}
		return counts;
	}

	Options ParseOptions(int argc, char** argv)
	{
		Options options;
		for (int i = 1; i < argc; ++i)
		{
			const std::string argument = argv[i];
			if (argument.starts_with("--body-counts="))
			{
				options.bodyCounts = ParseBodyCounts(argument.substr(14));
			}
			else if (argument.starts_with("--warmup="))
			{
				options.warmupCount = std::stoi(argument.substr(9));
			}
			else if (argument.starts_with("--samples="))
			{
				options.sampleCount = std::stoi(argument.substr(10));
			}
			else if (argument.starts_with("--dt="))
			{
				options.dtSeconds = std::stof(argument.substr(5));
			}
			else if (argument.starts_with("--steady-state-warmup-steps="))
			{
				options.steadyStateWarmupSteps = std::stoi(argument.substr(28));
			}
			else if (argument.starts_with("--steady-state-measured-steps="))
			{
				options.steadyStateMeasuredSteps = std::stoi(argument.substr(30));
			}
			else if (argument.starts_with("--solver-iterations="))
			{
				const std::string value = argument.substr(20);
				std::size_t consumed{};
				options.solverIterations = std::stoi(value, &consumed);
				if (consumed != value.size() ||
					options.solverIterations < GEngine::PhysicsSystem::MinSolverIterations ||
					options.solverIterations > GEngine::PhysicsSystem::MaxSolverIterations)
				{
					throw std::invalid_argument("solver iterations must be an integer in [1, 32]");
				}
			}
			else if (argument == "--physics-regression-baseline")
			{
				options.physicsRegressionBaseline = true;
			}
			else
			{
				throw std::invalid_argument("unknown argument: " + argument);
			}
		}

		if (options.warmupCount < 0 || options.sampleCount < 1 ||
			options.steadyStateWarmupSteps < 0 || options.steadyStateMeasuredSteps < 0 ||
			(options.steadyStateWarmupSteps > 0 && options.steadyStateMeasuredSteps == 0) ||
			!std::isfinite(options.dtSeconds) || options.dtSeconds <= 0.0f)
		{
			throw std::invalid_argument(
				"warmups and steady-state step counts must be non-negative, samples positive, "
				"steady-state warmup requires measured steps, and dt must be finite and positive");
		}
		return options;
	}

	std::vector<GEngine::Vec3f> UnitBoxPoints()
	{
		return {
			{ -1.0f, -1.0f, -1.0f }, { 1.0f, -1.0f, -1.0f },
			{ -1.0f,  1.0f, -1.0f }, { 1.0f,  1.0f, -1.0f },
			{ -1.0f, -1.0f,  1.0f }, { 1.0f, -1.0f,  1.0f },
			{ -1.0f,  1.0f,  1.0f }, { 1.0f,  1.0f,  1.0f }
		};
	}

	std::vector<GEngine::Vec3f> BoxPoints(const GEngine::Vec3f& halfExtents)
	{
		return {
			{ -halfExtents.x, -halfExtents.y, -halfExtents.z },
			{  halfExtents.x, -halfExtents.y, -halfExtents.z },
			{ -halfExtents.x,  halfExtents.y, -halfExtents.z },
			{  halfExtents.x,  halfExtents.y, -halfExtents.z },
			{ -halfExtents.x, -halfExtents.y,  halfExtents.z },
			{  halfExtents.x, -halfExtents.y,  halfExtents.z },
			{ -halfExtents.x,  halfExtents.y,  halfExtents.z },
			{  halfExtents.x,  halfExtents.y,  halfExtents.z }
		};
	}

	struct RegressionState
	{
		double mechanicalEnergy{};
		double averageY{};
		double maxLinearSpeed{};
		double maxAngularSpeed{};
		double angularMomentumMagnitude{};
		std::uint64_t movingBodyCount{};
		bool finite{ true };
	};

	struct RegressionResult
	{
		std::string scenario;
		std::uint64_t dynamicBodyCount{};
		double initialEnergy{};
		double peakEnergy{};
		double finalEnergy{};
		double initialAngularMomentum{};
		double finalAngularMomentum{};
		double finalAverageY{};
		double peakLinearSpeed{};
		double finalLinearSpeed{};
		double peakAngularSpeed{};
		double finalAngularSpeed{};
		std::uint64_t finalMovingBodyCount{};
		double averageStepMs{};
		double averageSolverMs{};
		double averageSolverIterations{};
		double peakFloorPlanePenetration{};
		double finalFloorPlanePenetration{};
		double averageGeneratedContacts{};
		std::uint64_t finalManifoldCount{};
		std::uint64_t finalManifoldContactCount{};
		bool finite{ true };
	};

	void ConfigureProbeBody(GEngine::RigidBody3D& body, GEngine::PhysicalShape& shape,
		const GEngine::Vec3f& position, GEngine::Component::BodyType type, float inverseMass)
	{
		body.m_Shape = &shape;
		body.m_Position = position;
		body.m_Orientation = GEngine::Quat(1.0f, 0.0f, 0.0f, 0.0f);
		body.m_InvMass = inverseMass;
		body.m_Elasticity = 0.5f;
		body.m_Friction = 0.5f;
		body.Type = type;
	}

	RegressionState CaptureRegressionState(const std::vector<GEngine::RigidBody3D*>& bodies,
		float gravityMagnitude)
	{
		RegressionState state;
		GEngine::Vec3f totalAngularMomentum(0.0f);
		for (const GEngine::RigidBody3D* body : bodies)
		{
			state.finite = state.finite && body && body->HasFiniteState() && body->m_Shape &&
				std::isfinite(body->m_InvMass) && body->m_InvMass > 0.0f;
			if (!state.finite)
			{
				continue;
			}

			const float mass = 1.0f / body->m_InvMass;
			const GEngine::Mat3& rotation = body->GetBodyToWorldRotation();
			const GEngine::Mat3 inertiaWorld =
				rotation * (body->m_Shape->InertiaTensor() * mass) * glm::transpose(rotation);
			const GEngine::Vec3f angularMomentum = inertiaWorld * body->m_AngularVelocity;
			const double linearSpeed = static_cast<double>(glm::length(body->m_LinearVelocity));
			const double angularSpeed = static_cast<double>(glm::length(body->m_AngularVelocity));
			const double linearEnergy = 0.5 * static_cast<double>(mass) * linearSpeed * linearSpeed;
			const double angularEnergy = 0.5 * static_cast<double>(
				glm::dot(body->m_AngularVelocity, angularMomentum));
			const double potentialEnergy = static_cast<double>(mass) *
				static_cast<double>(gravityMagnitude) * static_cast<double>(body->GetCenterOfMassWorldSpace().y);

			state.mechanicalEnergy += linearEnergy + angularEnergy + potentialEnergy;
			state.averageY += static_cast<double>(body->GetCenterOfMassWorldSpace().y);
			state.maxLinearSpeed = std::max(state.maxLinearSpeed, linearSpeed);
			state.maxAngularSpeed = std::max(state.maxAngularSpeed, angularSpeed);
			totalAngularMomentum += angularMomentum;
			if (linearSpeed > 0.05 || angularSpeed > 0.05)
			{
				++state.movingBodyCount;
			}
		}

		if (!bodies.empty())
		{
			state.averageY /= static_cast<double>(bodies.size());
		}
		state.angularMomentumMagnitude = static_cast<double>(glm::length(totalAngularMomentum));
		state.finite = state.finite && std::isfinite(state.mechanicalEnergy) &&
			std::isfinite(state.averageY) && std::isfinite(state.maxLinearSpeed) &&
			std::isfinite(state.maxAngularSpeed) && std::isfinite(state.angularMomentumMagnitude);
		return state;
	}

	// Exact lowest shape extent relative to the audit floor's top plane at y=0.5.
	// This measures floor-plane intrusion, not inter-body or manifold penetration.
	// Observe only; no collision queries or simulation changes are made.
	double FloorPlanePenetration(const std::vector<GEngine::RigidBody3D*>& bodies)
	{
		double penetration = 0.0;
		for (const auto* body : bodies)
		{
			penetration = std::max(penetration, 0.5 - static_cast<double>(body->GetWorldBounds().mins.y));
		}
		return penetration;
	}

	RegressionResult RunWorldRegression(const std::string& scenario, GEngine::PhysicsSystem& system,
		const std::vector<GEngine::RigidBody3D*>& dynamicBodies, float gravityMagnitude,
		int stepCount, float dtSeconds)
	{
		RegressionResult result;
		result.scenario = scenario;
		result.dynamicBodyCount = dynamicBodies.size();
		const RegressionState initial = CaptureRegressionState(dynamicBodies, gravityMagnitude);
		result.initialEnergy = initial.mechanicalEnergy;
		result.peakEnergy = initial.mechanicalEnergy;
		result.initialAngularMomentum = initial.angularMomentumMagnitude;
		result.finite = initial.finite;

		GEngine::ResetPhysicsProfile();
		const auto start = Clock::now();
		for (int step = 0; step < stepCount; ++step)
		{
			system.Update(GEngine::Timestep(dtSeconds));
			const RegressionState state = CaptureRegressionState(dynamicBodies, gravityMagnitude);
			result.peakEnergy = std::max(result.peakEnergy, state.mechanicalEnergy);
			result.peakLinearSpeed = std::max(result.peakLinearSpeed, state.maxLinearSpeed);
			result.peakAngularSpeed = std::max(result.peakAngularSpeed, state.maxAngularSpeed);
			result.finite = result.finite && state.finite;
			result.peakFloorPlanePenetration = std::max(result.peakFloorPlanePenetration,
				FloorPlanePenetration(dynamicBodies));
		}
		const auto end = Clock::now();

		const RegressionState finalState = CaptureRegressionState(dynamicBodies, gravityMagnitude);
		const PhysicsProfileSnapshot profile = GEngine::GetPhysicsProfileSnapshot();
		result.finalEnergy = finalState.mechanicalEnergy;
		result.finalAngularMomentum = finalState.angularMomentumMagnitude;
		result.finalAverageY = finalState.averageY;
		result.finalLinearSpeed = finalState.maxLinearSpeed;
		result.finalAngularSpeed = finalState.maxAngularSpeed;
		result.finalMovingBodyCount = finalState.movingBodyCount;
		result.averageStepMs = std::chrono::duration<double, std::milli>(end - start).count() /
			static_cast<double>(stepCount);
		result.averageGeneratedContacts = static_cast<double>(profile.generatedContactCount) /
			static_cast<double>(stepCount);
		result.averageSolverMs = static_cast<double>(profile.solverTimeNs) / (1.0e6 * stepCount);
		result.averageSolverIterations = static_cast<double>(profile.solverIterationCount) / stepCount;
		result.finalFloorPlanePenetration = FloorPlanePenetration(dynamicBodies);
		result.finalManifoldCount = profile.manifoldCount;
		result.finalManifoldContactCount = profile.manifoldContactCount;
		result.finite = result.finite && finalState.finite && std::isfinite(result.averageStepMs);
		return result;
	}

	bool IsFinite(const GEngine::Vec3f& value)
	{
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}

	bool IsFinite(const GEngine::Quat& value)
	{
		return std::isfinite(value.w) && std::isfinite(value.x) &&
			std::isfinite(value.y) && std::isfinite(value.z);
	}

	RegressionResult RunAsymmetricBodyRegression(int stepCount, float dtSeconds)
	{
		GEngine::ShapeBox shape(BoxPoints(GEngine::Vec3f(1.0f, 2.0f, 3.0f)));
		GEngine::RigidBody3D body;
		ConfigureProbeBody(body, shape, GEngine::Vec3f(0.0f),
			GEngine::Component::BodyType::Dynamic, 1.0f);
		body.m_AngularVelocity = GEngine::Vec3f(0.7f, 1.1f, 1.6f);
		const std::vector<GEngine::RigidBody3D*> bodies{ &body };

		RegressionResult result;
		result.scenario = "asymmetric_free_body";
		result.dynamicBodyCount = 1;
		const RegressionState initial = CaptureRegressionState(bodies, 0.0f);
		result.initialEnergy = initial.mechanicalEnergy;
		result.peakEnergy = initial.mechanicalEnergy;
		result.initialAngularMomentum = initial.angularMomentumMagnitude;
		result.finite = initial.finite;

		const auto start = Clock::now();
		for (int step = 0; step < stepCount; ++step)
		{
			body.Update(dtSeconds);
			const RegressionState state = CaptureRegressionState(bodies, 0.0f);
			result.peakEnergy = std::max(result.peakEnergy, state.mechanicalEnergy);
			result.peakLinearSpeed = std::max(result.peakLinearSpeed, state.maxLinearSpeed);
			result.peakAngularSpeed = std::max(result.peakAngularSpeed, state.maxAngularSpeed);
			result.finite = result.finite && state.finite;
		}
		const auto end = Clock::now();

		const RegressionState finalState = CaptureRegressionState(bodies, 0.0f);
		result.finalEnergy = finalState.mechanicalEnergy;
		result.finalAngularMomentum = finalState.angularMomentumMagnitude;
		result.finalAverageY = finalState.averageY;
		result.finalLinearSpeed = finalState.maxLinearSpeed;
		result.finalAngularSpeed = finalState.maxAngularSpeed;
		result.finalMovingBodyCount = finalState.movingBodyCount;
		result.averageStepMs = std::chrono::duration<double, std::milli>(end - start).count() /
			static_cast<double>(stepCount);
		result.finite = result.finite && finalState.finite && std::isfinite(result.averageStepMs);
		return result;
	}

	RegressionResult RunBoxStackRegression(int stepCount, float dtSeconds, int solverIterations)
	{
		GEngine::ShapeBox box(UnitBoxPoints());
		GEngine::ShapeBox floor(BoxPoints(GEngine::Vec3f(50.0f, 0.5f, 50.0f)));
		GEngine::PhysicsSystem system;
		system.SetSolverIterations(solverIterations);
		auto* world = new GEngine::PhysicsWorld(GEngine::Vec3f(0.0f, -12.0f, 0.0f));
		system.SetPhysicsWorld(world);

		GEngine::RigidBody3D* floorBody = world->CreateRigidBody3D();
		ConfigureProbeBody(*floorBody, floor, GEngine::Vec3f(0.0f),
			GEngine::Component::BodyType::Static, 0.0f);

		std::vector<GEngine::RigidBody3D*> boxes;
		boxes.reserve(16);
		for (int y = 0; y < 4; ++y)
		{
			for (int x = 0; x < 4; ++x)
			{
				GEngine::RigidBody3D* body = world->CreateRigidBody3D();
				ConfigureProbeBody(*body, box,
					GEngine::Vec3f(static_cast<float>(x) * 2.01f, 1.5f + static_cast<float>(y) * 2.0f, 0.0f),
					GEngine::Component::BodyType::Dynamic, 1.0f);
				boxes.push_back(body);
			}
		}

		return RunWorldRegression("box_stack_4x4", system, boxes, 12.0f, stepCount, dtSeconds);
	}

	RegressionResult RunSingleSphereRegression(int stepCount, float dtSeconds, int solverIterations)
	{
		GEngine::ShapeSphere sphere(1.0f);
		GEngine::ShapeBox floor(BoxPoints(GEngine::Vec3f(50.0f, 0.5f, 50.0f)));
		GEngine::PhysicsSystem system;
		system.SetSolverIterations(solverIterations);
		auto* world = new GEngine::PhysicsWorld(GEngine::Vec3f(0.0f, -12.0f, 0.0f));
		system.SetPhysicsWorld(world);

		GEngine::RigidBody3D* floorBody = world->CreateRigidBody3D();
		ConfigureProbeBody(*floorBody, floor, GEngine::Vec3f(0.0f),
			GEngine::Component::BodyType::Static, 0.0f);
		GEngine::RigidBody3D* sphereBody = world->CreateRigidBody3D();
		ConfigureProbeBody(*sphereBody, sphere, GEngine::Vec3f(0.0f, 10.0f, 0.0f),
			GEngine::Component::BodyType::Dynamic, 1.0f);

		return RunWorldRegression("single_sphere", system, { sphereBody }, 12.0f, stepCount, dtSeconds);
	}

	RegressionResult RunSphereLatticeRegression(int stepCount, float dtSeconds, int solverIterations)
	{
		GEngine::ShapeSphere sphere(1.0f);
		GEngine::ShapeBox floor(BoxPoints(GEngine::Vec3f(50.0f, 0.5f, 50.0f)));
		GEngine::PhysicsSystem system;
		system.SetSolverIterations(solverIterations);
		auto* world = new GEngine::PhysicsWorld(GEngine::Vec3f(0.0f, -12.0f, 0.0f));
		system.SetPhysicsWorld(world);

		GEngine::RigidBody3D* floorBody = world->CreateRigidBody3D();
		ConfigureProbeBody(*floorBody, floor, GEngine::Vec3f(0.0f),
			GEngine::Component::BodyType::Static, 0.0f);

		std::vector<GEngine::RigidBody3D*> spheres;
		spheres.reserve(180);
		for (int vertical = 0; vertical < 5; ++vertical)
		{
			for (int x = 0; x < 6; ++x)
			{
				for (int z = 0; z < 6; ++z)
				{
					GEngine::RigidBody3D* body = world->CreateRigidBody3D();
					ConfigureProbeBody(*body, sphere, GEngine::Vec3f(
						static_cast<float>(x - 1) * 2.0f,
						10.0f + static_cast<float>(vertical) * 2.0f,
						static_cast<float>(z - 1) * 2.0f),
						GEngine::Component::BodyType::Dynamic, 1.0f);
					spheres.push_back(body);
				}
			}
		}

		return RunWorldRegression("sphere_lattice_180", system, spheres, 12.0f, stepCount, dtSeconds);
	}

	void PrintRegressionResult(const RegressionResult& result)
	{
		const double peakPercent = result.initialEnergy != 0.0
			? result.peakEnergy * 100.0 / result.initialEnergy
			: 0.0;
		const double energyChangePercent = result.initialEnergy != 0.0
			? (result.finalEnergy - result.initialEnergy) * 100.0 / result.initialEnergy
			: 0.0;
		const double angularMomentumChangePercent = result.initialAngularMomentum != 0.0
			? (result.finalAngularMomentum - result.initialAngularMomentum) * 100.0 /
				result.initialAngularMomentum
			: 0.0;

		std::cout << std::fixed << std::setprecision(6)
			<< result.scenario << ',' << result.dynamicBodyCount << ','
			<< result.initialEnergy << ',' << result.peakEnergy << ',' << result.finalEnergy << ','
			<< peakPercent << ',' << energyChangePercent << ','
			<< result.initialAngularMomentum << ',' << result.finalAngularMomentum << ','
			<< angularMomentumChangePercent << ',' << result.finalAverageY << ','
			<< result.peakLinearSpeed << ',' << result.finalLinearSpeed << ','
			<< result.peakAngularSpeed << ',' << result.finalAngularSpeed << ','
			<< result.finalMovingBodyCount << ',' << result.averageGeneratedContacts << ','
			<< result.finalManifoldCount << ',' << result.finalManifoldContactCount << ','
			<< result.averageStepMs << ',' << (result.finite ? 1 : 0) << ','
			<< result.averageSolverMs << ',' << result.averageSolverIterations << ','
			<< result.peakFloorPlanePenetration << ',' << result.finalFloorPlanePenetration << '\n';
	}

	int RunPhysicsRegressionBaseline(int solverIterations)
	{
		constexpr int stepCount = 1200;
		constexpr float dtSeconds = 1.0f / 120.0f;
		GEngine::Log::Initialize();
		GEngine::Log::GetLogger()->set_level(spdlog::level::off);

		std::vector<RegressionResult> results;
		results.reserve(4);
		results.push_back(RunAsymmetricBodyRegression(stepCount, dtSeconds));
		results.push_back(RunBoxStackRegression(stepCount, dtSeconds, solverIterations));
		results.push_back(RunSingleSphereRegression(stepCount, dtSeconds, solverIterations));
		results.push_back(RunSphereLatticeRegression(stepCount, dtSeconds, solverIterations));

		std::cout << "# benchmark=physics_regression_baseline\n"
			<< "# dt_seconds=" << std::setprecision(9) << dtSeconds << '\n'
			<< "# solver_iterations=" << solverIterations << '\n'
			<< "# steps=" << stepCount << '\n'
			<< "# simulated_seconds=" << static_cast<double>(stepCount) * dtSeconds << '\n'
			<< "# moving_threshold_linear=0.05\n"
			<< "# moving_threshold_angular=0.05\n"
			<< "scenario,dynamic_bodies,initial_energy,peak_energy,final_energy,peak_energy_percent,"
			<< "final_energy_change_percent,initial_angular_momentum,final_angular_momentum,"
			<< "angular_momentum_change_percent,final_average_y,peak_linear_speed,final_linear_speed,"
			<< "peak_angular_speed,final_angular_speed,final_moving_bodies,average_generated_contacts,"
			<< "final_manifolds,final_manifold_contacts,average_step_ms,finite,average_solver_ms,average_solver_iterations,peak_floor_plane_penetration,final_floor_plane_penetration\n";

		bool allFinite = true;
		for (const RegressionResult& result : results)
		{
			PrintRegressionResult(result);
			allFinite = allFinite && result.finite;
		}
		return allFinite ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	Sample RunSample(GEngine::ShapeBox& shape, int bodyCount, const Options& options)
	{
		GEngine::PhysicsSystem system;
		system.SetSolverIterations(options.solverIterations);
		auto* world = new GEngine::PhysicsWorld(GEngine::Vec3f(0.0f));
		system.SetPhysicsWorld(world);

		for (int bodyIndex = 0; bodyIndex < bodyCount; ++bodyIndex)
		{
			GEngine::RigidBody3D* body = world->CreateRigidBody3D();
			const int pairIndex = bodyIndex / 2;
			const int pairPattern = pairIndex % 4;
			const bool firstBody = (bodyIndex % 2) == 0;
			const bool dynamic = pairPattern == 3 || (pairPattern != 2 && firstBody);
			const float pairOrigin = static_cast<float>(pairIndex) * 10.0f;

			body->m_Shape = &shape;
			body->Type = dynamic ? GEngine::Component::BodyType::Dynamic : GEngine::Component::BodyType::Static;
			body->m_InvMass = dynamic ? 1.0f : 0.0f;
			body->m_Elasticity = 0.25f;
			body->m_Friction = 0.5f;
			body->m_Position = firstBody
				? GEngine::Vec3f(pairOrigin, 0.0f, 0.0f)
				: GEngine::Vec3f(
					pairOrigin + (options.steadyStateMeasuredSteps > 0 ? 4.0f : 1.45f), 0.15f, 0.1f);
			body->m_Orientation = firstBody
				? glm::angleAxis(0.12f, glm::normalize(GEngine::Vec3f(0.3f, 1.0f, 0.2f)))
				: GEngine::Quat(1.0f, 0.0f, 0.0f, 0.0f);
		}

		for (int step = 0; step < options.steadyStateWarmupSteps; ++step)
		{
			system.Update(GEngine::Timestep(options.dtSeconds));
		}

		const int measuredSteps = std::max(options.steadyStateMeasuredSteps, 1);
		GEngine::ResetPhysicsProfile();
		const auto start = Clock::now();
		for (int step = 0; step < measuredSteps; ++step)
		{
			system.Update(GEngine::Timestep(options.dtSeconds));
		}
		const auto end = Clock::now();

		Sample sample;
		sample.externalStepMs = std::chrono::duration<double, std::milli>(end - start).count() /
			static_cast<double>(measuredSteps);
		sample.measuredSteps = measuredSteps;
		sample.profile = GEngine::GetPhysicsProfileSnapshot();
		for (const GEngine::RigidBody3D* body : world->GetPhysicsBodies())
		{
			sample.finiteState = sample.finiteState && IsFinite(body->m_Position) &&
				IsFinite(body->m_LinearVelocity) && IsFinite(body->m_AngularVelocity) &&
				IsFinite(body->m_Orientation);
			for (const float value : { body->m_Position.x, body->m_Position.y, body->m_Position.z,
				body->m_Orientation.w, body->m_Orientation.x, body->m_Orientation.y, body->m_Orientation.z,
				body->m_LinearVelocity.x, body->m_LinearVelocity.y, body->m_LinearVelocity.z,
				body->m_AngularVelocity.x, body->m_AngularVelocity.y, body->m_AngularVelocity.z })
			{
				sample.finalStateFingerprint ^= std::bit_cast<std::uint32_t>(value);
				sample.finalStateFingerprint *= 1099511628211ull;
			}
		}
		return sample;
	}

	double Average(const std::vector<Sample>& samples, std::uint64_t PhysicsProfileSnapshot::* field)
	{
		double total = 0.0;
		for (const Sample& sample : samples)
		{
			total += static_cast<double>(sample.profile.*field);
		}
		return total / static_cast<double>(samples.size());
	}

	double AveragePerStep(const std::vector<Sample>& samples, std::uint64_t PhysicsProfileSnapshot::* field)
	{
		double total = 0.0;
		for (const Sample& sample : samples)
		{
			total += static_cast<double>(sample.profile.*field) /
				static_cast<double>(sample.measuredSteps);
		}
		return total / static_cast<double>(samples.size());
	}

	double AverageExternalMs(const std::vector<Sample>& samples)
	{
		double total = 0.0;
		for (const Sample& sample : samples)
		{
			total += sample.externalStepMs;
		}
		return total / static_cast<double>(samples.size());
	}

	double MedianExternalMs(const std::vector<Sample>& samples)
	{
		std::vector<double> values;
		values.reserve(samples.size());
		for (const Sample& sample : samples)
		{
			values.push_back(sample.externalStepMs);
		}
		std::sort(values.begin(), values.end());
		const std::size_t middle = values.size() / 2;
		return values.size() % 2 == 0
			? (values[middle - 1] + values[middle]) * 0.5
			: values[middle];
	}

	std::uint64_t Max(const std::vector<Sample>& samples, std::uint64_t PhysicsProfileSnapshot::* field)
	{
		std::uint64_t result = 0;
		for (const Sample& sample : samples)
		{
			result = std::max(result, sample.profile.*field);
		}
		return result;
	}

	std::uint64_t ExpectedDynamicBodyCount(int bodyCount)
	{
		std::uint64_t result = 0;
		for (int bodyIndex = 0; bodyIndex < bodyCount; ++bodyIndex)
		{
			const int pairPattern = (bodyIndex / 2) % 4;
			const bool firstBody = (bodyIndex % 2) == 0;
			if (pairPattern == 3 || (pairPattern != 2 && firstBody))
			{
				++result;
			}
		}
		return result;
	}

	bool ValidateSample(const Sample& sample, int bodyCount, bool steadyState, int solverIterations)
	{
		if (!sample.finiteState || !std::isfinite(sample.externalStepMs) || sample.externalStepMs <= 0.0)
		{
			return false;
		}
		if (!GEngine::IsPhysicsProfilingEnabled())
		{
			return true;
		}

		const auto& profile = sample.profile;
		const std::uint64_t expectedDynamic = ExpectedDynamicBodyCount(bodyCount);
		const bool commonState = profile.stepCount == static_cast<std::uint64_t>(sample.measuredSteps) &&
			profile.bodyCount == static_cast<std::uint64_t>(bodyCount) &&
			profile.dynamicBodyCount == expectedDynamic &&
			profile.activeBodyCount == expectedDynamic &&
			profile.sleepingBodyCount == 0 &&
			profile.integratedBodyCount >= static_cast<std::uint64_t>(bodyCount * sample.measuredSteps) &&
			profile.broadphaseTimeNs > 0 && profile.integrationTimeNs > 0 &&
			profile.physicsWorldTimeNs > 0;
		if (steadyState)
		{
			return commonState &&
				profile.candidatePairCount == 0 &&
				profile.broadphaseFullSortCount == 0 &&
				profile.epaCallCount == 0 &&
				profile.generatedContactCount == 0 && profile.manifoldCount == 0 &&
				profile.manifoldContactCount == 0 && profile.solverConstraintCount == 0 &&
				profile.solverIterationCount == 0;
		}

		return commonState &&
			profile.candidatePairCount > 0 &&
			profile.broadphaseAxisOverlapCount >= profile.candidatePairCount &&
			(bodyCount < 6 || profile.broadphaseStaticPairRejectedCount > 0) &&
			profile.broadphaseFullSortCount == 1 &&
			profile.narrowphaseCallCount > 0 &&
			profile.gjkCallCount > 0 && profile.gjkIterationCount > 0 &&
			profile.supportCallCount > 0 && profile.epaCallCount > 0 &&
			profile.generatedContactCount > 0 && profile.manifoldCount > 0 &&
			profile.manifoldContactCount > 0 && profile.solverConstraintCount > 0 &&
			profile.solverIterationCount == static_cast<std::uint64_t>(solverIterations) * static_cast<std::uint64_t>(sample.measuredSteps) &&
			profile.narrowphaseTimeNs > 0 &&
			profile.solverTimeNs > 0 && profile.integrationTimeNs > 0 &&
			profile.physicsWorldTimeNs > 0;
	}

	void PrintHeader()
	{
		std::cout
			<< "body_count,dynamic_bodies,active_bodies,sleeping_bodies,candidate_pairs,"
			<< "axis_overlaps,aabb_rejected,static_rejected,mask_rejected,endpoint_swaps,full_sorts,"
			<< "pair_filter_rejected,gjk_calls,gjk_total_ms,gjk_avg_iterations,gjk_max_iterations,"
			<< "support_calls,support_ms,epa_calls,epa_ms,contacts,manifolds,manifold_contacts,"
			<< "solver_constraints,solver_iterations,gravity_ms,broadphase_ms,pair_filter_ms,"
			<< "narrowphase_ms,manifold_ms,solver_ms,contact_resolution_ms,integration_ms,"
			<< "physics_world_ms,external_mean_ms,external_median_ms,external_min_ms,external_max_ms,"
			<< "external_median_fps,final_state_fingerprint\n";
	}

	void PrintResult(int bodyCount, const std::vector<Sample>& samples)
	{
		const double gjkCalls = AveragePerStep(samples, &PhysicsProfileSnapshot::gjkCallCount);
		const double gjkIterations = AveragePerStep(samples, &PhysicsProfileSnapshot::gjkIterationCount);
		const double externalMeanMs = AverageExternalMs(samples);
		const double externalMedianMs = MedianExternalMs(samples);
		auto [minimumSample, maximumSample] = std::minmax_element(samples.begin(), samples.end(),
			[](const Sample& lhs, const Sample& rhs) { return lhs.externalStepMs < rhs.externalStepMs; });
		const double nsToMs = 1.0 / 1'000'000.0;

		std::cout << std::fixed << std::setprecision(6)
			<< bodyCount << ','
			<< Average(samples, &PhysicsProfileSnapshot::dynamicBodyCount) << ','
			<< Average(samples, &PhysicsProfileSnapshot::activeBodyCount) << ','
			<< Average(samples, &PhysicsProfileSnapshot::sleepingBodyCount) << ','
			<< Average(samples, &PhysicsProfileSnapshot::candidatePairCount) << ','
			<< AveragePerStep(samples, &PhysicsProfileSnapshot::broadphaseAxisOverlapCount) << ','
			<< AveragePerStep(samples, &PhysicsProfileSnapshot::broadphaseAabbRejectedCount) << ','
			<< AveragePerStep(samples, &PhysicsProfileSnapshot::broadphaseStaticPairRejectedCount) << ','
			<< AveragePerStep(samples, &PhysicsProfileSnapshot::broadphaseMaskRejectedCount) << ','
			<< AveragePerStep(samples, &PhysicsProfileSnapshot::broadphaseInsertionSortSwapCount) << ','
			<< AveragePerStep(samples, &PhysicsProfileSnapshot::broadphaseFullSortCount) << ','
			<< AveragePerStep(samples, &PhysicsProfileSnapshot::pairFilterRejectedCount) << ','
			<< gjkCalls << ','
			<< AveragePerStep(samples, &PhysicsProfileSnapshot::gjkTimeNs) * nsToMs << ','
			<< (gjkCalls > 0.0 ? gjkIterations / gjkCalls : 0.0) << ','
			<< Max(samples, &PhysicsProfileSnapshot::gjkMaxIterations) << ','
			<< AveragePerStep(samples, &PhysicsProfileSnapshot::supportCallCount) << ','
			<< AveragePerStep(samples, &PhysicsProfileSnapshot::supportTimeNs) * nsToMs << ','
			<< AveragePerStep(samples, &PhysicsProfileSnapshot::epaCallCount) << ','
			<< AveragePerStep(samples, &PhysicsProfileSnapshot::epaTimeNs) * nsToMs << ','
			<< AveragePerStep(samples, &PhysicsProfileSnapshot::generatedContactCount) << ','
			<< Average(samples, &PhysicsProfileSnapshot::manifoldCount) << ','
			<< Average(samples, &PhysicsProfileSnapshot::manifoldContactCount) << ','
			<< Average(samples, &PhysicsProfileSnapshot::solverConstraintCount) << ','
			<< AveragePerStep(samples, &PhysicsProfileSnapshot::solverIterationCount) << ','
			<< AveragePerStep(samples, &PhysicsProfileSnapshot::gravityTimeNs) * nsToMs << ','
			<< AveragePerStep(samples, &PhysicsProfileSnapshot::broadphaseTimeNs) * nsToMs << ','
			<< AveragePerStep(samples, &PhysicsProfileSnapshot::pairFilterTimeNs) * nsToMs << ','
			<< AveragePerStep(samples, &PhysicsProfileSnapshot::narrowphaseTimeNs) * nsToMs << ','
			<< AveragePerStep(samples, &PhysicsProfileSnapshot::manifoldTimeNs) * nsToMs << ','
			<< AveragePerStep(samples, &PhysicsProfileSnapshot::solverTimeNs) * nsToMs << ','
			<< AveragePerStep(samples, &PhysicsProfileSnapshot::contactResolutionTimeNs) * nsToMs << ','
			<< AveragePerStep(samples, &PhysicsProfileSnapshot::integrationTimeNs) * nsToMs << ','
			<< AveragePerStep(samples, &PhysicsProfileSnapshot::physicsWorldTimeNs) * nsToMs << ','
			<< externalMeanMs << ',' << externalMedianMs << ','
			<< minimumSample->externalStepMs << ',' << maximumSample->externalStepMs << ','
			<< (1000.0 / externalMedianMs) << ',' << samples.front().finalStateFingerprint << '\n';
	}
}

int main(int argc, char** argv)
{
	try
	{
		const Options options = ParseOptions(argc, argv);
		if (options.physicsRegressionBaseline)
		{
			return RunPhysicsRegressionBaseline(options.solverIterations);
		}
		GEngine::ShapeBox shape(UnitBoxPoints());

		std::cout << "# benchmark="
			<< (options.steadyStateMeasuredSteps > 0
				? "separated_boxes_steady_state"
				: "paired_overlapping_boxes") << '\n';
		std::cout << "# physics_profiling=" << (GEngine::IsPhysicsProfilingEnabled() ? "enabled" : "disabled") << '\n';
		std::cout << "# warmup=" << options.warmupCount << "\n# samples=" << options.sampleCount
			<< "\n# steady_state_warmup_steps=" << options.steadyStateWarmupSteps
			<< "\n# measured_steps_per_sample=" << std::max(options.steadyStateMeasuredSteps, 1)
			<< "\n# solver_iterations=" << options.solverIterations
			<< "\n# dt_seconds=" << std::setprecision(9) << options.dtSeconds << '\n';
		PrintHeader();

		for (const int bodyCount : options.bodyCounts)
		{
			for (int warmup = 0; warmup < options.warmupCount; ++warmup)
			{
				const Sample sample = RunSample(shape, bodyCount, options);
				if (!ValidateSample(sample, bodyCount, options.steadyStateMeasuredSteps > 0, options.solverIterations))
				{
					PrintResult(bodyCount, std::vector<Sample>{ sample });
					std::cerr << "benchmark validation failed during warmup for body_count=" << bodyCount << '\n';
					return EXIT_FAILURE;
				}
			}

			std::vector<Sample> samples;
			samples.reserve(options.sampleCount);
			for (int sampleIndex = 0; sampleIndex < options.sampleCount; ++sampleIndex)
			{
				samples.push_back(RunSample(shape, bodyCount, options));
				if (!ValidateSample(samples.back(), bodyCount, options.steadyStateMeasuredSteps > 0, options.solverIterations))
				{
					PrintResult(bodyCount, std::vector<Sample>{ samples.back() });
					std::cerr << "benchmark validation failed for body_count=" << bodyCount
						<< ", sample=" << sampleIndex << '\n';
					return EXIT_FAILURE;
				}
			}
			if (!std::all_of(samples.begin(), samples.end(), [&](const Sample& sample) {
				return sample.finalStateFingerprint == samples.front().finalStateFingerprint; }))
			{
				throw std::runtime_error("repeated benchmark samples produced different physical state");
			}
			PrintResult(bodyCount, samples);
		}
	}
	catch (const std::exception& exception)
	{
		std::cerr << "PhysicsBenchmark: " << exception.what() << '\n';
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
