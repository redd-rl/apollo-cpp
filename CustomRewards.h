#pragma once
#define and &&
#define or ||
#include <RLGymSim_CPP/Utils/RewardFunctions/CombinedReward.h>
using namespace RLGSC; 

float norm(Vec vec1) {
	return std::pow((std::pow(vec1.x, 2) + std::pow(vec1.y, 2) + std::pow(vec1.z, 2)), 2);
}

class SpeedflipKickoffReward : public RewardFunction {
	float goalSpeed = 0.5;
	SpeedflipKickoffReward(float goalSpeed) {
		this->goalSpeed = goalSpeed;
	};
	virtual float GetReward(const PlayerData& player, const GameState& state, const Action& prevAction) {
		if (state.ball.vel.IsZero() and player.boostFraction < 0.02) {
			return RS_MAX(0, player.phys.vel.Dot((state.ball.pos - player.phys.pos).Normalized()) / CommonValues::CAR_MAX_SPEED);
		};
		return 0;
	};
};

class SpeedTowardBallReward : public RewardFunction {
public:
	virtual float GetReward(const PlayerData& player, const GameState& state, const Action& prevAction) {
		return RS_MAX(0, player.phys.vel.Dot((state.ball.pos - player.phys.pos).Normalized()) / CommonValues::CAR_MAX_SPEED);
	};
};

class InAirReward : public RewardFunction {
public:
	virtual float GetReward(const PlayerData& player, const GameState& state, const Action& prevAction) {
		if (!player.carState.isOnGround) {
			return 1;
		}
		else {
			return 0;
		}
	};
};

class TouchBallRewardScaledByHitForce : public RewardFunction {
public:
	int maxHitSpeed = 3000;
	Vec lastBallVel;
	Vec curBallVel;
	virtual void Reset(const GameState& state) {
		lastBallVel = state.ball.vel;
		curBallVel = state.ball.vel;
	};

	virtual void PreStep(const GameState& state) {
		lastBallVel = curBallVel;
		curBallVel = state.ball.vel;
	};

	virtual float GetReward(const PlayerData& player, const GameState& state, const Action& prevAction) {
		if (player.ballTouchedStep) {
			float BallVelocity = curBallVel.Dist(lastBallVel);
			if (BallVelocity > 500) {
				float reward = BallVelocity / maxHitSpeed;
				return reward;
			};
		};
		return 0.0f;
	};
};

class AerialDistanceReward : public RewardFunction {
public:
	int RampHeight = 256;
	float heightScale;
	float distanceScale;
	float carDistance;
	float ballDistance;
	int currentCarId = 0;
	GameState prevState;
	AerialDistanceReward(float heightScale, float distanceScale) {
		this->heightScale = heightScale;
		this->distanceScale = distanceScale;
	};
	const PlayerData* findCarOnField(const GameState& state, int carID) {
		for (auto& player : state.players)
			if (player.carId == carID)
				return &player;
		return NULL;
	};
	virtual void Reset(const GameState& state) {
		int currentCarId = 0;
		prevState = state;
	};
	virtual float GetReward(const PlayerData& player, const GameState& state, const Action& prevAction) {
		float reward = 0;
		bool isCurrent = (currentCarId != 0 && currentCarId == player.carId);
		if (player.phys.pos.z < RampHeight) {
			if (isCurrent) {
				isCurrent = false;
				currentCarId = 0;
			}
		}
		else if (player.ballTouchedStep && !isCurrent) {
			isCurrent = true;
			ballDistance = 0;
			carDistance = 0;
			reward = heightScale * RS_MAX(player.phys.pos.z + state.ball.pos.z - 2 * RampHeight, 0);
		}
		else if (isCurrent) {
			PlayerData currentCar = *findCarOnField(state, currentCarId);
			int carDistance = carDistance + player.phys.pos.Dist(currentCar.phys.pos);
			int ballDistance = ballDistance + state.ball.pos.Dist(prevState.ball.pos);
			if (player.ballTouchedStep) {
				reward = distanceScale * (carDistance + ballDistance);
				carDistance = 0;
				ballDistance = 0;
			};
		};
		if (isCurrent) {
			currentCarId = player.carId;
		};

		prevState = state;

		return reward / (2 * CommonValues::BACK_WALL_Y);
	};
};

class PlayerOnWallReward : public RewardFunction {
public:
	virtual float GetReward(const PlayerData& player, const GameState& state, const Action& prevAction) {
		return player.carState.isOnGround && player.phys.pos.z > 300;
	};
};

class LavaFloorReward : public RewardFunction {
public:
	virtual float GetReward(const PlayerData& player, const GameState& state, const Action& prevAction) {
		return -int(player.carState.isOnGround);
	};
};