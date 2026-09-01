#include <GEngine/Physics/PhysicsProfile.h>
#include <GEngine/Physics/PhysicsSystem.h>
#include <GEngine/Physics/PhysicsWorld.h>
#include <GEngine/Physics/ShapeBox.h>

#include <algorithm>
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
		float dtSeconds{ 1.0f / 120.0f };
	};

	struct Sample
	{
		PhysicsProfileSnapshot profile;
		double externalStepMs{};
		bool finiteState{ true };
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
			else
			{
				throw std::invalid_argument("unknown argument: " + argument);
			}
		}

		if (options.warmupCount < 0 || options.sampleCount < 1 ||
			!std::isfinite(options.dtSeconds) || options.dtSeconds <= 0.0f)
		{
			throw std::invalid_argument("warmup must be non-negative, samples positive, and dt finite and positive");
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

	bool IsFinite(const GEngine::Vec3f& value)
	{
		return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
	}

	bool IsFinite(const GEngine::Quat& value)
	{
		return std::isfinite(value.w) && std::isfinite(value.x) &&
			std::isfinite(value.y) && std::isfinite(value.z);
	}

	Sample RunSample(GEngine::ShapeBox& shape, int bodyCount, float dtSeconds)
	{
		GEngine::PhysicsSystem system;
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
				: GEngine::Vec3f(pairOrigin + 1.45f, 0.15f, 0.1f);
			body->m_Orientation = firstBody
				? glm::angleAxis(0.12f, glm::normalize(GEngine::Vec3f(0.3f, 1.0f, 0.2f)))
				: GEngine::Quat(1.0f, 0.0f, 0.0f, 0.0f);
		}

		GEngine::ResetPhysicsProfile();
		const auto start = Clock::now();
		system.Update(GEngine::Timestep(dtSeconds));
		const auto end = Clock::now();

		Sample sample;
		sample.externalStepMs = std::chrono::duration<double, std::milli>(end - start).count();
		sample.profile = GEngine::GetPhysicsProfileSnapshot();
		for (const GEngine::RigidBody3D* body : world->GetPhysicsBodies())
		{
			sample.finiteState = sample.finiteState && IsFinite(body->m_Position) &&
				IsFinite(body->m_LinearVelocity) && IsFinite(body->m_AngularVelocity) &&
				IsFinite(body->m_Orientation);
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

	bool ValidateSample(const Sample& sample, int bodyCount)
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
		return profile.stepCount == 1 &&
			profile.bodyCount == static_cast<std::uint64_t>(bodyCount) &&
			profile.dynamicBodyCount == expectedDynamic &&
			profile.activeBodyCount == expectedDynamic &&
			profile.sleepingBodyCount == 0 &&
			profile.candidatePairCount >= static_cast<std::uint64_t>(bodyCount / 2) &&
			(bodyCount < 6 || profile.pairFilterRejectedCount > 0) &&
			profile.narrowphaseCallCount > 0 &&
			profile.gjkCallCount > 0 && profile.gjkIterationCount > 0 &&
			profile.supportCallCount > 0 && profile.epaCallCount > 0 &&
			profile.generatedContactCount > 0 && profile.manifoldCount > 0 &&
			profile.manifoldContactCount > 0 && profile.solverConstraintCount > 0 &&
			profile.integratedBodyCount >= static_cast<std::uint64_t>(bodyCount) &&
			profile.broadphaseTimeNs > 0 && profile.narrowphaseTimeNs > 0 &&
			profile.solverTimeNs > 0 && profile.integrationTimeNs > 0 &&
			profile.physicsWorldTimeNs > 0;
	}

	void PrintHeader()
	{
		std::cout
			<< "body_count,dynamic_bodies,active_bodies,sleeping_bodies,candidate_pairs,"
			<< "pair_filter_rejected,gjk_calls,gjk_total_ms,gjk_avg_iterations,gjk_max_iterations,"
			<< "support_calls,support_ms,epa_calls,epa_ms,contacts,manifolds,manifold_contacts,"
			<< "solver_constraints,solver_iterations,gravity_ms,broadphase_ms,pair_filter_ms,"
			<< "narrowphase_ms,manifold_ms,solver_ms,contact_resolution_ms,integration_ms,"
			<< "physics_world_ms,external_mean_ms,external_median_ms,external_min_ms,external_max_ms,"
			<< "external_median_fps\n";
	}

	void PrintResult(int bodyCount, const std::vector<Sample>& samples)
	{
		const double gjkCalls = Average(samples, &PhysicsProfileSnapshot::gjkCallCount);
		const double gjkIterations = Average(samples, &PhysicsProfileSnapshot::gjkIterationCount);
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
			<< Average(samples, &PhysicsProfileSnapshot::pairFilterRejectedCount) << ','
			<< gjkCalls << ','
			<< Average(samples, &PhysicsProfileSnapshot::gjkTimeNs) * nsToMs << ','
			<< (gjkCalls > 0.0 ? gjkIterations / gjkCalls : 0.0) << ','
			<< Max(samples, &PhysicsProfileSnapshot::gjkMaxIterations) << ','
			<< Average(samples, &PhysicsProfileSnapshot::supportCallCount) << ','
			<< Average(samples, &PhysicsProfileSnapshot::supportTimeNs) * nsToMs << ','
			<< Average(samples, &PhysicsProfileSnapshot::epaCallCount) << ','
			<< Average(samples, &PhysicsProfileSnapshot::epaTimeNs) * nsToMs << ','
			<< Average(samples, &PhysicsProfileSnapshot::generatedContactCount) << ','
			<< Average(samples, &PhysicsProfileSnapshot::manifoldCount) << ','
			<< Average(samples, &PhysicsProfileSnapshot::manifoldContactCount) << ','
			<< Average(samples, &PhysicsProfileSnapshot::solverConstraintCount) << ','
			<< Average(samples, &PhysicsProfileSnapshot::solverIterationCount) << ','
			<< Average(samples, &PhysicsProfileSnapshot::gravityTimeNs) * nsToMs << ','
			<< Average(samples, &PhysicsProfileSnapshot::broadphaseTimeNs) * nsToMs << ','
			<< Average(samples, &PhysicsProfileSnapshot::pairFilterTimeNs) * nsToMs << ','
			<< Average(samples, &PhysicsProfileSnapshot::narrowphaseTimeNs) * nsToMs << ','
			<< Average(samples, &PhysicsProfileSnapshot::manifoldTimeNs) * nsToMs << ','
			<< Average(samples, &PhysicsProfileSnapshot::solverTimeNs) * nsToMs << ','
			<< Average(samples, &PhysicsProfileSnapshot::contactResolutionTimeNs) * nsToMs << ','
			<< Average(samples, &PhysicsProfileSnapshot::integrationTimeNs) * nsToMs << ','
			<< Average(samples, &PhysicsProfileSnapshot::physicsWorldTimeNs) * nsToMs << ','
			<< externalMeanMs << ',' << externalMedianMs << ','
			<< minimumSample->externalStepMs << ',' << maximumSample->externalStepMs << ','
			<< (1000.0 / externalMedianMs) << '\n';
	}
}

int main(int argc, char** argv)
{
	try
	{
		const Options options = ParseOptions(argc, argv);
		GEngine::ShapeBox shape(UnitBoxPoints());

		std::cout << "# benchmark=paired_overlapping_boxes\n";
		std::cout << "# physics_profiling=" << (GEngine::IsPhysicsProfilingEnabled() ? "enabled" : "disabled") << '\n';
		std::cout << "# warmup=" << options.warmupCount << "\n# samples=" << options.sampleCount
			<< "\n# dt_seconds=" << std::setprecision(9) << options.dtSeconds << '\n';
		PrintHeader();

		for (const int bodyCount : options.bodyCounts)
		{
			for (int warmup = 0; warmup < options.warmupCount; ++warmup)
			{
				const Sample sample = RunSample(shape, bodyCount, options.dtSeconds);
				if (!ValidateSample(sample, bodyCount))
				{
					std::cerr << "benchmark validation failed during warmup for body_count=" << bodyCount << '\n';
					return EXIT_FAILURE;
				}
			}

			std::vector<Sample> samples;
			samples.reserve(options.sampleCount);
			for (int sampleIndex = 0; sampleIndex < options.sampleCount; ++sampleIndex)
			{
				samples.push_back(RunSample(shape, bodyCount, options.dtSeconds));
				if (!ValidateSample(samples.back(), bodyCount))
				{
					std::cerr << "benchmark validation failed for body_count=" << bodyCount
						<< ", sample=" << sampleIndex << '\n';
					return EXIT_FAILURE;
				}
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
