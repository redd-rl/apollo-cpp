#include <RLGymPPO_CPP/Learner.h>

#include <RLGymSim_CPP/Utils/RewardFunctions/CommonRewards.h>
#include <RLGymSim_CPP/Utils/RewardFunctions/CombinedReward.h>
#include <RLGymSim_CPP/Utils/TerminalConditions/NoTouchCondition.h>
#include <RLGymSim_CPP/Utils/TerminalConditions/GoalScoreCondition.h>
#include <RLGymSim_CPP/Utils/OBSBuilders/DefaultOBS.h>
#include <RLGymSim_CPP/Utils/StateSetters/RandomState.h>
#include <RLGymSim_CPP/Utils/StateSetters/KickoffState.h>
#include <RLGymSim_CPP/Utils/ActionParsers/DiscreteAction.h>
#include "CustomRewards.h"
#include "CustomStateSetters.h"
#include "CustomCombinedReward.h"
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

// Create the RLGymSim environment for each of our games
EnvCreateResult EnvCreateFunc() {
	constexpr int TICK_SKIP = 8;
	constexpr float NO_TOUCH_TIMEOUT_SECS = 3.f;

	auto rewards = new LogCombinedReward( // Format is { RewardFunc(), weight }
		{ 
			{ new EventReward({.touch = 1.f}), 50.f },
			// Small reward for facing the ball
			{ new SpeedTowardBallReward(), 5.f },

			// Moderate reward for going towards the ball
			{ new FaceBallReward(), 1.f }, 

			// Bigger reward for having the ball go towards the goal
			{ new InAirReward(), 1.0f }, 
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
	auto stateSetter = new WeightedSampleSetter(
		{ 
			{new RandomState(true, true, false), 0.5f},
		    {new KickoffState(), 0.5f},
			{new WallPracticeState(), 0.2} 
		}
	);

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

int main() {
	// Initialize RocketSim with collision meshes
	RocketSim::Init("./collision_meshes");

	// Make configuration for the learner
	LearnerConfig cfg = {};

	// Play around with these to see what the optimal is for your machine, more isn't always better
	cfg.numThreads = 16;
	cfg.numGamesPerThread = 16;

	cfg.metricsRunName = "Apollo-X";
	cfg.metricsProjectName = "Apollo";
	cfg.metricsGroupName = "Dionysus";

	// We want a large itr/batch size
	// You'll want to increase this as your bot improves, up to an extent
	int tsPerItr = 100 * 1000;
	cfg.timestepsPerIteration = tsPerItr;
	cfg.ppo.batchSize = tsPerItr;
	cfg.ppo.miniBatchSize = 25 * 1000; // Lower this if too much VRAM is being allocated
	cfg.expBufferSize = tsPerItr * 3;
	
	// This is just set to 1 to match rlgym-ppo example
	// I've found the best value is somewhere between 2 and 4
	// Increasing this will lower SPS, but increase step efficiency
	cfg.ppo.epochs = 1; 

	// Reasonable starting entropy
	cfg.ppo.entCoef = 0.01f;

	// Decently-strong learning rate to start, may start to be too high around 100m steps
	cfg.ppo.policyLR = 8e-4;
	cfg.ppo.criticLR = 8e-4;

	// Default model size
	cfg.ppo.policyLayerSizes = { 1024, 1024, 1024, 1024, 512 };
	cfg.ppo.criticLayerSizes = { 2048, 2048, 1024, 1024, 512 };
	
	cfg.sendMetrics = true; // Send metrics
	cfg.renderMode = false; // Don't render

	// Make the learner with the environment creation function and the config we just made
	Learner learner = Learner(EnvCreateFunc, cfg);

	// Set up our callbacks
	learner.stepCallback = OnStep;
	learner.iterationCallback = OnIteration;

	// Start learning!
	learner.Learn();

	return 0;
}