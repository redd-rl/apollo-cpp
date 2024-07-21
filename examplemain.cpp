#include <RLGymPPO_CPP/Learner.h>

#include <RLGymSim_CPP/Utils/RewardFunctions/CommonRewards.h>
#include <RLGymSim_CPP/Utils/RewardFunctions/CombinedReward.h>
#include <RLGymSim_CPP/Utils/TerminalConditions/NoTouchCondition.h>
#include <RLGymSim_CPP/Utils/TerminalConditions/GoalScoreCondition.h>
#include <RLGymSim_CPP/Utils/OBSBuilders/DefaultOBS.h>
#include <RLGymSim_CPP/Utils/OBSBuilders/DefaultOBSPadded.h>
#include <RLGymSim_CPP/Utils/StateSetters/RandomState.h>
#include <RLGymSim_CPP/Utils/StateSetters/KickoffState.h>
#include <RLGymSim_CPP/Utils/ActionParsers/DiscreteAction.h>
#include "CustomRewards.h"
#include "CustomStateSetters.h"
#include "CustomCombinedReward.h"
#include "RLBotClient.h"
#include "AdvancedOBSPadded.h"
#include "RLBotClient.h"

using namespace RLGPC; // RLGymPPO
using namespace RLGSC; // RLGymSim

static std::vector<std::string> g_RewardNames = {};



// This is our step callback, it's called every step from every RocketSim game
// WARNING: This is called from multiple threads, often simultaneously, 
//	so don't access things apart from these arguments unless you know what you're doing.
// gameMetrics: The metrics for this specific game
void OnStep(GameInst* gameInst, const RLGSC::Gym::StepResult& stepResult, Report& gameMetrics) {
	if (dynamic_cast<LogCombinedReward*>(gameInst->match->rewardFn)) {
		auto combRewards = (LogCombinedReward*)gameInst->match->rewardFn;

		float totalReward = 0;
		for (int i = 0; i < combRewards->rewardFuncs.size(); i++) {
			float reward = combRewards->lastRewards[i];
			gameMetrics.AccumAvg(combRewards->GetRewardFuncName(i), reward);
		}
	}
	auto& gameState = stepResult.state;
	for (auto& player : gameState.players) {
		// Track average player speed
		float speed = player.phys.vel.Length();
		gameMetrics.AccumAvg("player_speed", speed);

		// Track ball touch ratio
		gameMetrics.AccumAvg("ball_touch_ratio", player.ballTouchedStep);

		// Track in-air ratio
		gameMetrics.AccumAvg("in_air_ratio", !player.carState.isOnGround);
	}
}

// This is our iteration callback, it's called every time we complete an iteration, after learning
// Here we can add custom metrics to the metrics report, for example
void OnIteration(Learner* learner, Report& allMetrics) {

	AvgTracker avgPlayerSpeed = {};
	AvgTracker avgBallTouchRatio = {};
	AvgTracker avgAirRatio = {};
	
	// Get metrics for every gameInst
	auto allGameMetrics = learner->GetAllGameMetrics();
	for (auto& gameReport : allGameMetrics) {
		avgPlayerSpeed += gameReport.GetAvg("player_speed");
		avgBallTouchRatio += gameReport.GetAvg("ball_touch_ratio");
		avgAirRatio += gameReport.GetAvg("in_air_ratio");
	}

	allMetrics["player_speed"] = avgPlayerSpeed.Get();
	allMetrics["ball_touch_ratio"] = avgBallTouchRatio.Get();
	allMetrics["in_air_ratio"] = avgAirRatio.Get();
	{ // Average rewards
		std::vector<AvgTracker> avgRews(g_RewardNames.size());
		for (int i = 0; i < g_RewardNames.size(); i++)
			for (auto& gameReport : allGameMetrics)
				if (!gameReport.data.empty())
					avgRews[i] += gameReport.GetAvg(g_RewardNames[i]);

		for (int i = 0; i < g_RewardNames.size(); i++)
			allMetrics["reward/ " + g_RewardNames[i]] = avgRews[i].Get();
	}
}

constexpr auto NDR = [](RewardFunction* rewardFunc) -> NotDemoedReward* {
	return new NotDemoedReward(rewardFunc);
};
constexpr auto UBE = [](StateSetter* stateSetter, float unlimBoostChance) -> UnlimBoostEpisodeSetter* {
	return new UnlimBoostEpisodeSetter(stateSetter, unlimBoostChance);
};


// Create the RLGymSim environment for each of our games
EnvCreateResult EnvCreateFunc() {
	constexpr int TICK_SKIP = 8;
	constexpr float NO_TOUCH_TIMEOUT_SECS = 15.f;
	float aggressionBias = 0.25f;
	float goalReward = 10;
	float concedeReward = -goalReward * (1 - aggressionBias);
	auto rewards = new LogCombinedReward( // Format is { RewardFunc(), weight }
		{ 
			{ new EventReward({.teamGoal = goalReward, .concede = concedeReward,}), 30.f},
			{ new VelocityBallToGoalReward(), 25.f},
			{ new SpeedflipKickoffReward(), 15.f },
			{ new ZeroSumReward(new TouchBallRewardScaledByHitForce(), 1.f, 0.7f), 16.f},
			{ new ZeroSumReward(new PossessionReward(), 1.f, 1.f), 5.f},
			{ new SpeedTowardBallReward(), 4.f},
			{ new ZeroSumReward(new JumpTouchReward(), 1.f, 0.5f), 3.f},
			{ new ZeroSumReward(new AerialReward(), 1.f, 0.5f), 1.3f},
			{ new ZeroSumReward(new SaveBoostReward(), 1.f, 0.35f), 1.3f},
			{ new ZeroSumReward(new GoalSpeedAndPlacementReward(), 1, 0.6f), 1.f},
			{ NDR(new InAirReward()), 0.25f},
			{ new LightingMcQueenReward(), 3.f},
		}
	);

	{
		static std::once_flag onceFlag;
		std::call_once(onceFlag,
			[&] {
				for (int i = 0; i < rewards->rewardFuncs.size(); i++)
					g_RewardNames.push_back(rewards->GetRewardFuncName(i));
			}
		);
	}

	std::vector<TerminalCondition*> terminalConditions = {
		new NoTouchCondition(NO_TOUCH_TIMEOUT_SECS * 120 / TICK_SKIP),
		new GoalScoreCondition()
	};

	auto obs = new DefaultOBS();
	auto actionParser = new DiscreteAction();
	auto stateSetter = UBE(new WeightedSampleSetter(
		{ 
			{new RandomState(true, true, false), 0.7f},
		    {new KickoffState(), 0.4f},
			{new WallPracticeState(), 0.7f},
		}
	), 0.15f);

	Match* match = new Match(
		rewards,
		terminalConditions,
		obs,
		actionParser,
		stateSetter,

		1, // Team size
		true // Spawn opponents
	);

	Gym* gym = new Gym(match, TICK_SKIP);
	return { match, gym };
}

int main(int argc, char* argv[]) {
	std::filesystem::path exePath = std::filesystem::path(argv[0]);
	std::filesystem::path policyPath = exePath.parent_path() / "PPO_POLICY.lt";
	// Initialize RocketSim with collision meshes
	RocketSim::Init("./collision_meshes");

	// Make configuration for the learner
	LearnerConfig cfg = {};
	cfg.renderMode = false; // Don't render

	// Check if the --render flag was provided
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--render") == 0) {
			cfg.renderMode = true; // Do render
			break;
		};
	};
	// Play around with these to see what the optimal is for your machine, more isn't always better
	cfg.numThreads = 22;
	cfg.numGamesPerThread = 30;
	cfg.skillTrackerConfig.enabled = true;
	// cfg.skillTrackerConfig.perModeRatings = true later
	cfg.timestepsPerSave = 2 * 1000 * 1000;
	cfg.checkpointsToKeep = 30;
	cfg.skillTrackerConfig.numEnvs = 6;
	cfg.skillTrackerConfig.numThreads = 6;

	cfg.metricsRunName = "Apollo-X";
	cfg.metricsProjectName = "Apollo";
	cfg.metricsGroupName = "Dionysus";

	// We want a large itr/batch size
	// You'll want to increase this as your bot improves, up to an extent
	int tsPerItr = 100 * 1000;
	cfg.timestepsPerIteration = tsPerItr;
	cfg.ppo.batchSize = tsPerItr;
	cfg.ppo.miniBatchSize = 50 * 1000; // Lower this if too much VRAM is being allocated
	cfg.expBufferSize = tsPerItr * 3;
	
	// This is just set to 1 to match rlgym-ppo example
	// I've found the best value is somewhere between 2 and 4
	// Increasing this will lower SPS, but increase step efficiency
	cfg.ppo.epochs = 1; 

	// Reasonable starting entropy
	cfg.ppo.entCoef = 0.01f;

	// Decently-strong learning rate to start, may start to be too high around 100m steps
	cfg.ppo.policyLR = 1e-4;
	cfg.ppo.criticLR = 1e-4;

	// Default model size
	cfg.ppo.policyLayerSizes = { 1024, 1024, 1024, 1024, 512 };
	cfg.ppo.criticLayerSizes = { 2048, 2048, 1024, 1024, 512 };
	
	cfg.sendMetrics = true; // Send metrics

	// Make the learner with the environment creation function and the config we just made
	Learner learner = Learner(EnvCreateFunc, cfg);

	// Set up our callbacks
	learner.stepCallback = OnStep;
	learner.iterationCallback = OnIteration;

	// Start learning!
	learner.Learn();

	return 0;
}