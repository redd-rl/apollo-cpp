#pragma once

#define and &&
#define or ||
#include <RLGymSim_CPP/Utils/StateSetters/StateSetter.h>
#include <vector>
#include <random>
using namespace RLGSC;
// arena is StateWrapper

// https://github.com/RLGym/rlgym-tools/blob/main/rlgym_tools/extra_state_setters/weighted_sample_setter.py

class WeightedSampleSetter : public StateSetter {
public:
	std::vector<StateSetter*> stateSetters;
	std::vector<float> setterWeights;
	bool ownsFuncs;
	WeightedSampleSetter(std::vector<StateSetter*> stateSetters, std::vector<float> setterWeights, bool ownsFuncs = false) :
		stateSetters(stateSetters), setterWeights(setterWeights), ownsFuncs(ownsFuncs) {
		assert(stateSetters.size() == setterWeights.size());
	}

	WeightedSampleSetter(std::vector<std::pair<StateSetter*, float>> settersWithWeights, bool ownsFuncs = false) :
		ownsFuncs(ownsFuncs) {
		for (auto& pair : settersWithWeights) {
			stateSetters.push_back(pair.first);
			setterWeights.push_back(pair.second);
		}
	}
	virtual GameState ResetState(Arena* arena) {
		StateSetter* setter = weightedRandomSelection(setterWeights);
		setter->ResetState(arena);
		return GameState(arena);
	};
	virtual StateSetter* weightedRandomSelection(const std::vector<float>& weights) {
		std::random_device rd;  // Seed
		std::mt19937 gen(rd()); // Mersenne Twister engine
		std::discrete_distribution<> dist(weights.begin(), weights.end());

		int index = dist(gen);
		return stateSetters[index];
	};
};

// https://github.com/RLGym/rlgym-tools/blob/main/rlgym_tools/extra_state_setters/wall_state.py
// deg to rad here is calculated using btDegrees(x)
// BallRadius is a CommonValues object, not using 94.

class WallPracticeState : public StateSetter {
public:
	float airDribbleOdds;
	float backboardRollOdds;
	float sideHighOdds;
	float DEG_TO_RAD = 3.14159265f / 180.f;
	float weights[3];
	WallPracticeState(float airDribbleOdds = 1.f / 3.f, float backboardRollOdds = 1.f / 3.f, float sideHighOdds = 1.f / 3.f)
		: airDribbleOdds(airDribbleOdds), backboardRollOdds(backboardRollOdds), sideHighOdds(sideHighOdds) {
		weights[0] = airDribbleOdds;
		weights[1] = backboardRollOdds;
		weights[2] = sideHighOdds;
	}
	virtual int weightedRandomSelection() {
		std::random_device rd;  // Seed
		std::mt19937 gen(rd()); // Mersenne Twister engine
		std::vector<float> weightVector(std::begin(weights), std::end(weights));
		std::discrete_distribution<> dist(weightVector.begin(), weightVector.end());

		int index = dist(gen);
		return index;
	};
	virtual GameState ResetState(Arena* arena) {
		int scenarioPick = weightedRandomSelection();
		if (scenarioPick == 0) {
			return shortGoalRoll(arena);
		}
		else if (scenarioPick == 1) {
			return sideHighRoll(arena);
		}
		else if (scenarioPick == 2) {
			return airDribbleSetup(arena);
		};
		return GameState(arena);
	}
private:
	/// <summary>
	/// A medium roll up a side wall with the car facing the roll path
	/// </summary>
	/// <param name="arena"></param>
	/// <returns></returns>
	virtual GameState airDribbleSetup(Arena* arena) {
		srand((unsigned) time(NULL));
		int axisInverter = (RocketSim::Math::RandInt(0, 2) == 1) ? 1 : -1;
		int teamSide = RocketSim::Math::RandInt(0, 2);
		Team teamTeamSide = (Team)teamSide;
		int teamInverter = (teamSide == 0) ? 1 : -1;

		// if only 1 play, team is always 0

		float ballXPos = 3000 * axisInverter;
		float ballYPos = (RocketSim::Math::RandInt(0, 7600) - 3800);
		float ballZPos = CommonValues::BALL_RADIUS;

		float ballXVel = (2000 + (RocketSim::Math::RandInt(0, 1000) - 500)) * axisInverter;
		float ballYVel = (RocketSim::Math::RandInt(0, 1000)) * teamInverter;
		float ballZVel = 0;
		Car* chosenCar;
		for (Car* car : arena->GetCars()) {
			if (car->team == teamTeamSide) {
				chosenCar = car;
				break;  // Assuming you want to break after finding the first match
			};
		};

		float carXPos = 2500 * axisInverter;
		float carYPos = ballYPos;
		float carZPos = 27;

		float yaw = (axisInverter == 1) ? 0 : 180;
		float carPitchRot = btDegrees(0);
		float carYawRot = btDegrees((yaw + (RocketSim::Math::RandInt(0, 40) - 20)));
		float carRollRot = btDegrees(0);

		CarState cs = {};
		cs.pos = { carXPos, carYPos, carZPos };
		cs.rotMat = Angle(carYawRot, carPitchRot, carRollRot).ToRotMat();
		cs.boost = 100;

		chosenCar->SetState(cs);

		for (Car* car : arena->GetCars()) {
			if (car == chosenCar) {
				continue;
			};
			// set all other cars randomly in the field
			CarState cs = {};
			cs.pos = { (float)RocketSim::Math::RandInt(0, 2944) - 1472, (float)RocketSim::Math::RandInt(0, 3968) - 1984, 0};
			cs.rotMat = Angle(0, btDegrees(RocketSim::Math::RandInt(0, 360) - 180), 0).ToRotMat();
			cs.boost = 33;
			car->SetState(cs);
		};
		return GameState(arena);
	};
	/// <summary>
	/// A high vertical roll up the side of the field
	/// </summary>
	/// <param name="arena"></param>
	/// <returns></returns>
	virtual GameState sideHighRoll(Arena* arena) {
		int sidePick = RocketSim::Math::RandInt(0, 2);
		int sideInverter = 1;
		if (sidePick == 1) {
			// change side
			sideInverter = -1;
		};

		// magic numbers are from manual calibration and what feels right

		BallState bs = {};

		bs.pos.x = 3000 * sideInverter;
		bs.pos.y = RocketSim::Math::RandInt(0, 1500) - 750;
		bs.pos.z = CommonValues::BALL_RADIUS;
		

		bs.vel.x = (2000 + RocketSim::Math::RandInt(0, 1000) - 500) * sideInverter;
		bs.vel.y = RocketSim::Math::RandInt(0, 1500) - 750;
		bs.vel.z = RocketSim::Math::RandInt(0, 300);

		arena->ball->SetState(bs);

		Car* wallCarBlue;
		for (Car* car : arena->GetCars()) {
			if (car->team == (Team)0) {
				wallCarBlue = car;
				break;  // Assuming you want to break after finding the first match
			};
		};

		CarState blueCs = {};

		float bluePitchRot = btDegrees(0);
		float blueYawRot = btDegrees(90);
		float blueRollRot = btDegrees(90 * sideInverter);

		float blueX = 4096 * sideInverter;
		float blueY = -2500 + (RocketSim::Math::RandInt(0, 500) - 250);
		float blueZ = 600 + (RocketSim::Math::RandInt(0, 400) - 200);

		blueCs.pos = { blueX, blueY, blueZ };
		blueCs.rotMat = Angle(blueYawRot, bluePitchRot, blueRollRot).ToRotMat();
		blueCs.boost = 100;

		wallCarBlue->SetState(blueCs);

		Car* wallCarOrange;
		if (arena->_cars.size() > 1) {
			for (Car* car : arena->GetCars()) {
				if (car->team == (Team)1) {
					wallCarOrange = car;
					break;  // Assuming you want to break after finding the first match
				};
			};
			CarState orangeCs = {};

			float orangePitchRot = btDegrees(0);
			float orangeYawRot = btDegrees(-90);
			float orangeRollRot = btDegrees(-90 * sideInverter);

			float orangeX = 4096 * sideInverter;
			float orangeY = 2500 + (RocketSim::Math::RandInt(0, 500) - 250);
			float orangeZ = 400 + (RocketSim::Math::RandInt(0, 400) - 200);

			orangeCs.pos = { orangeX, orangeY, orangeZ };
			orangeCs.rotMat = Angle(orangeYawRot, orangePitchRot, orangeRollRot).ToRotMat();
			orangeCs.boost = 100;

			wallCarOrange->SetState(orangeCs);
		};
		for (Car* car : arena->GetCars()) {
			if (arena->_cars.size() == 1 or car->id == wallCarOrange->id or car->id == wallCarBlue->id) {
				continue;
			};
			// set all other cars randomly in the field
			CarState cs = {};
			cs.pos = { (float)RocketSim::Math::RandInt(0, 2944) - 1472, (float)RocketSim::Math::RandInt(0, 3968) - 1984, 0 };
			cs.rotMat = Angle(0, btDegrees(RocketSim::Math::RandInt(0, 360) - 180), 0).ToRotMat();
			cs.boost = 33;
			car->SetState(cs);
		};
		return GameState(arena);
	};
	/// <summary>
	/// A short roll across the backboard and own in front of the goal
	/// </summary>
	/// <param name="arena"></param>
	/// <returns></returns>
	virtual GameState shortGoalRoll(Arena* arena) {
		int defenseTeam;
		if (arena->_cars.size() > 1) {
			defenseTeam = RocketSim::Math::RandInt(0, 2);
		}
		else {
			defenseTeam = 0;
		};
		int sidePick = RocketSim::Math::RandInt(0, 2);
		
		int defenseInverter = 1;
		if (defenseTeam == 0) {
			defenseInverter = -1;
		};

		int sideInverter = 1;
		if (sidePick == 1) {
			sideInverter = -1;
		}

		BallState bs = {};
		float xRandom = RocketSim::Math::RandInt(0, 446);
		bs.pos.x = (-2850 + xRandom) * sideInverter;
		bs.pos.y = (5120 - CommonValues::BALL_RADIUS) * defenseInverter;
		bs.pos.z = 1400 + RocketSim::Math::RandInt(0, 400) - 200;

		bs.vel.x = (1000 + RocketSim::Math::RandInt(0, 400) - 200) * sideInverter;
		bs.vel.y = 0;
		bs.vel.z = 550;

		Car* wallCar = NULL;
		for (Car* car : arena->GetCars()) {
			if (car->team == (Team)defenseTeam) {
				wallCar = car;
				break;  // Assuming you want to break after finding the first match
			};
		};

		CarState wallCarCs = {};
		wallCarCs.pos.x = (2000 - RocketSim::Math::RandInt(0, 500)) * sideInverter;
		wallCarCs.pos.y = 5120 * sideInverter;
		wallCarCs.pos.z = 1000 + (RocketSim::Math::RandInt(0, 500) - 500);

		float wallCarPitchRot = btDegrees((sideInverter == -1) ? 0 : 180);
		float wallCarYawRot = btDegrees(0);
		float wallCarRollRot = btDegrees(-90 * defenseInverter);

		wallCarCs.rotMat = Angle(wallCarYawRot, wallCarPitchRot, wallCarRollRot).ToRotMat();
		wallCarCs.boost = 25;
		wallCar->SetState(wallCarCs);

		Car* challengeCar;
		if (arena->_cars.size() > 1) {
			for (Car* car : arena->GetCars()) {
				if (car->team != (Team)defenseTeam) {
					challengeCar = car;
					break;  // Assuming you want to break after finding the first match
				};
			};
			CarState challengeCarCs = {};
			challengeCarCs.pos.x = 0;
			challengeCarCs.pos.y = 1000 * defenseInverter;
			challengeCarCs.pos.z = 0;

			float challengeCarPitchRot = btDegrees(0);
			float challengeCarYawRot = btDegrees(90 * defenseInverter);
			float challengeCarRollRot = btDegrees(0);

			challengeCarCs.rotMat = Angle(challengeCarYawRot, challengeCarPitchRot, challengeCarRollRot).ToRotMat();
			challengeCarCs.boost = 100;

			challengeCar->SetState(challengeCarCs);
		}
		for (Car* car : arena->GetCars()) {
			if (arena->_cars.size() == 1 or car->id == wallCar->id or car->id == challengeCar->id) {
				continue;
			};
			// set all other cars randomly in the field
			CarState cs = {};
			cs.pos = { (float)RocketSim::Math::RandInt(0, 2944) - 1472, (float)(- 4500 + RocketSim::Math::RandInt(0, 3968) - 250) * defenseInverter, 0};
			cs.rotMat = Angle(0, btDegrees(RocketSim::Math::RandInt(0, 360) - 180), 0).ToRotMat();
			cs.boost = 33;
			car->SetState(cs);
		};

		return GameState(arena);
	};
};