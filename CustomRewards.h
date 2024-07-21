#pragma once
#define and &&
#define or ||
#include <RLGymSim_CPP/Utils/RewardFunctions/CombinedReward.h>
using namespace RLGSC; 

float KPH_TO_VEL(float kph) {
	return kph * (250 / 9);
};

// https://github.com/redd-rl/apollo-bot/blob/main/custom_rewards.py

float norm(Vec vec1) {
	return std::pow((std::pow(vec1.x, 2) + std::pow(vec1.y, 2) + std::pow(vec1.z, 2)), 2);
}

// reward the player for moving fast toward the ball, but only on kickoff state.
class SpeedflipKickoffReward : public RewardFunction {
	virtual float GetReward(const PlayerData& player, const GameState& state, const Action& prevAction) {
		if (state.ball.vel.IsZero() and player.boostFraction < 0.02) {
			return RS_MAX(0, player.phys.vel.Dot((state.ball.pos - player.phys.pos).Normalized()) / CommonValues::CAR_MAX_SPEED);
		};
		return 0;
	};
};

// reward the player for going fast
class LightingMcQueenReward : public RewardFunction {
	virtual float GetReward(const PlayerData& player, const GameState& state, const Action& prevAction) {
		return sqrt(RS_CLAMP(player.phys.vel.Length() / 1800, 0, 1));
	}
};

// reward the player for touching the ball mid air
class JumpTouchReward : public RewardFunction {
public:
	JumpTouchReward(float minHeight = 200.0f, float exp = 1.0f)
		: minHeight(minHeight), exp(exp), div(std::pow(CommonValues::CEILING_Z / 2 - CommonValues::BALL_RADIUS, exp)) {}

	virtual void Reset(const GameState& initialState) override {
		ticksUntilNextReward = 0;
	};

	virtual float GetReward(const PlayerData& player, const GameState& state, const Action& prevAction) override {
		if (player.ballTouchedStep && player.carState.isOnGround && state.ball.pos.z >= minHeight && ticksUntilNextReward <= 0) {
			ticksUntilNextReward = 149;
			float reward = std::pow(std::min(state.ball.pos.z, CommonValues::CEILING_Z / 2) - CommonValues::BALL_RADIUS, exp) / div;
			if (reward > 0.05) {
				return reward * 100;
			};
		}
		ticksUntilNextReward -= 1;
		return 0.0f;
	};

private:
	float minHeight;
	float exp;
	float div;
	int ticksUntilNextReward = 0;
};

// reward the player for moving fast at the ball
class SpeedTowardBallReward : public RewardFunction {
public:
	virtual float GetReward(const PlayerData& player, const GameState& state, const Action& prevAction) {
		return RS_MAX(0, player.phys.vel.Dot((state.ball.pos - player.phys.pos).Normalized()) / CommonValues::CAR_MAX_SPEED);
	};
};

// thanks so much fredrik, goat.

// reward the player for high goal speed, (warning very explosive as it's a high return reward, LOW WEIGHT.)
class GoalSpeedReward : public RewardFunction {
public:
	GoalSpeedReward(float rewardFactor = 0.1f) : rewardFactor(rewardFactor) {};
	virtual float GetReward(const PlayerData& player, const GameState& state, const Action& prevAction) {
		float reward = 0.0f;

		if (state.scoreLine[(int)player.team] > 0) {
			float ballSpeed = state.ball.vel.Length();

			reward += ballSpeed * rewardFactor;
		}

		return reward * 2.0f;
	};
private:
	float rewardFactor;
};

// reward the player for dribbling and matching the speed of the ball.
class DribbleReward : public RewardFunction {
public:
	virtual float GetReward(const PlayerData& player, const GameState& state, const Action& prevAction) {
		const float MIN_BALL_HEIGHT = 109.0f;
		const float MAX_BALL_HEIGHT = 180.0f;
		const float MAX_DISTANCE = 197.0f;
		const float SPEED_MATCH_FACTOR = 2.0f;

		if (player.carState.isOnGround && state.ball.pos.z >= MIN_BALL_HEIGHT && state.ball.pos.z <= MAX_BALL_HEIGHT && (player.phys.pos - state.ball.pos).Length() < MAX_DISTANCE) {
			float playerSpeed = player.phys.vel.Length();
			float ballSpeed = state.ball.vel.Length();
			float speedMatchReward = ((playerSpeed / CommonValues::CAR_MAX_SPEED) + SPEED_MATCH_FACTOR * (1.0f - std::abs(playerSpeed - ballSpeed) / (playerSpeed + ballSpeed)));
			return speedMatchReward;
		}
		else {
			return 0.0f;
		}
		return 0.0f;
	};
};


// reward the player for being in the air (opposite of lavafloor)
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

// reward player for hitting the ball hard
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

// reward player for goal speed and goal placement
class GoalSpeedAndPlacementReward : public RewardFunction {
public:
	float prevScoreBlue = 0;
	float prevScoreOrange = 0;
	GameState prevStateBlue;
	GameState prevStateOrange;
	float minHeight = CommonValues::BALL_RADIUS + 10;
	float heightReward = 1.75;

	virtual void Reset(const GameState& state) {
		prevScoreBlue = state.scoreLine[0];
		prevScoreOrange = state.scoreLine[1];
		prevStateBlue = state;
		prevStateOrange = state;
	}

	virtual float GetReward(const PlayerData& player, const GameState& state, const Action& prevAction) {
		if ((int)player.team == 0) {
			float score = state.scoreLine[0];
			if (score > prevScoreBlue) {
				float reward = prevStateBlue.ball.vel.Length() / CommonValues::BALL_MAX_SPEED;
				if (prevStateBlue.ball.pos.z > minHeight) {
					reward = heightReward * reward;
				};
				prevStateBlue = state;
				prevScoreBlue = score;

				return reward;
			};
		} else {
			float score = state.scoreLine[1];
			if (score > prevScoreOrange) {
				float reward = prevStateOrange.ball.vel.Length() / CommonValues::BALL_MAX_SPEED;
				if (prevStateOrange.ball.pos.z > minHeight) {
					reward = heightReward * reward;
				};
				prevStateOrange = state;
				prevScoreOrange = score;

				return reward;
			};
		};
		return 0;
	};
};

// reward player more for picking up big pads than small pads
class PickupBoostReward : public RewardFunction {
public:
	GameState lastState;
	PickupBoostReward(float small = 3.f, float big = 10.f) : small(small), big(big) {};
	virtual void Reset(const GameState& state) {
		lastState = state;
	};
	virtual float GetReward(const PlayerData& player, const GameState& state, const Action& prevAction) {
		float reward = 0;
		for (auto& _player : lastState.players) {
			if (player.carId == _player.carId) {
				float boostDiff = player.boostFraction;
				if (boostDiff > 0) {
					reward += boostDiff * big;
					if (player.boostFraction < 0.98 && _player.boostFraction < 0.88) {
						reward += boostDiff * small;
					}
				}
			}
		}
		lastState = state;
		return reward;
	}

private:
	float big;
	float small;
};

// ???? reward player for being closest to the ball on kickoff idk aether6837 wrote this.
class KickoffProximityRewardAllModes : public RewardFunction {
	virtual float GetReward(const PlayerData& player, const GameState& state, const Action& prevAction) {
		if (state.ball.vel.IsZero()) {
			std::vector<float> allyDistances;
			for (auto& _player : state.players) {
				if (_player.team == player.team) {
					float allyDistance = _player.phys.pos.Dist(state.ball.pos);
					allyDistances.push_back(allyDistance);
				};
			};
			std::vector<float> opponentDistances;
			for (auto& _player : state.players) {
				if (_player.team != player.team) {
					float opponentDistance = _player.phys.pos.Dist(state.ball.pos);
					opponentDistances.push_back(opponentDistance);
				};
			};
			if (opponentDistances.empty() && allyDistances.empty() && std::min_element(allyDistances.begin(), allyDistances.end()) < std::min_element(opponentDistances.begin(), opponentDistances.end())) {
				return 1;
			}
			else {
				return -1;
			};
		};
		return 0;
	};
};

// reward the player for being the closest to the ball and their team having touched the ball last
// ambiguous in 1v1.
class PossessionReward : public RewardFunction {
public:
	float prevTeamTouch = -1.f;
	float stacking = 0;
	PossessionReward(float minDist = 200.f) : minDist(minDist) {};
	virtual void Reset(const GameState& state) {
		prevTeamTouch = -1;
		stacking = 0;
	};
	virtual void PreStep(const GameState& state) {
		for (auto& player : state.players) {
			if (player.ballTouchedStep && !((int)player.team == prevTeamTouch)) {
				prevTeamTouch = (int)player.team;
				stacking = 0;
			}
		}
	}

	virtual float GetReward(const PlayerData& player, const GameState& state, const Action& prevAction) {
		float reward = 0;
		float distToBall = (player.phys.pos - state.ball.pos).Length();
		if ((int)player.team == prevTeamTouch && distToBall < minDist) {
			for (auto& _player : state.players) {
				if (_player.team != player.team && _player.carId != player.carId) {
					float oppDistBall = (_player.phys.pos - state.ball.pos).Length();
					if (oppDistBall < distToBall) {
						return 0;
					};
				}
			}
			stacking += 1;
			reward += stacking / 10;
		}
		return reward;
	}
private:
	float minDist;
};

// reward the player for aerial touches initially and then exponential increase the more it gets.
// idk rolv wrote this
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

// reward the player for having speed towards the ball mid-air
class AerialReward : public RewardFunction {
public:
	int BALL_MIN_HEIGHT = 300;
	virtual float GetReward(const PlayerData& player, const GameState& state, const Action& prevAction) override {

		Vec dirToBall = (state.ball.pos - player.phys.pos).Normalized();

		float distance = player.carState.pos.Dist(state.ball.pos) - CommonValues::BALL_RADIUS;
		float distanceReward = exp(-0.5 * distance / CommonValues::CAR_MAX_SPEED);

		float speedTowardBall = player.phys.vel.Dot(dirToBall);
		float speedReward = speedTowardBall / CommonValues::CAR_MAX_SPEED;

		if (speedTowardBall < 0) {
			return 0.0f;
		}
		if (player.carState.isOnGround) {
			return 0.0f;
		}
		if (state.ballState.pos.z < BALL_MIN_HEIGHT) {
			return 0.0f;
		}
		float reward = (speedReward * distanceReward);

		if (player.carState.hasDoubleJumped)
			reward += 0.1f; // Double jump bonus

		return reward;
	}
};

// reward the bot for being on the wall
class PlayerOnWallReward : public RewardFunction {
public:
	virtual float GetReward(const PlayerData& player, const GameState& state, const Action& prevAction) {
		return player.carState.isOnGround && player.phys.pos.z > 300;
	};
};

// punish the bot for being on the ground
class LavaFloorReward : public RewardFunction {
public:
	virtual float GetReward(const PlayerData& player, const GameState& state, const Action& prevAction) {
		return -int(player.carState.isOnGround);
	};
};

// takes a reward function pointer as input, it just makes the reward return 0 if the player is demoed
// prevents the bot from farming air reward by jumping before being demo'd for example.
class NotDemoedReward : public RewardFunction {
public:

	RewardFunction* childFunc;
	bool ownsFunc;

	NotDemoedReward(RewardFunction* childFunc, bool ownsFunc = true)
		: childFunc(childFunc), ownsFunc(ownsFunc) {

	}

	~NotDemoedReward() {
		if (ownsFunc)
			delete childFunc;
	}

protected:
	virtual void Reset(const GameState& initialState) {
		childFunc->Reset(initialState);
	}

	virtual void PreStep(const GameState& state) {
		childFunc->PreStep(state);
	}

	virtual float GetReward(const PlayerData& player, const GameState& state, const Action& prevAction) {
		if (!player.carState.isDemoed) {
			return childFunc->GetReward(player, state, prevAction);
		};
		return 0;
	};
};
