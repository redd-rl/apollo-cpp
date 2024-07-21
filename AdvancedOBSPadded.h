#pragma once
#include <RLGymSim_CPP/Utils/OBSBuilders/DefaultOBS.h>

namespace RLGSC {
	// Verion of DefaultOBS that supports a varying number of players (i.e. 1v1, 2v2, 3v3, etc.)
	// Maximum player count can be however high you want
	// Opponent and teammate slots are randomly shuffled to prevent slot bias
	class AdvancedOBSPadded : public DefaultOBS {
	public:

		int maxPlayers;

		AdvancedOBSPadded(
			int maxPlayers,
			Vec posCoef = Vec(1 / CommonValues::SIDE_WALL_X, 1 / CommonValues::BACK_WALL_Y, 1 / CommonValues::CEILING_Z),
			float velCoef = 1 / CommonValues::CAR_MAX_SPEED,
			float angVelCoef = 1 / CommonValues::CAR_MAX_ANG_VEL
		) : DefaultOBS(posCoef, velCoef, angVelCoef), maxPlayers(maxPlayers) {

		}

		virtual FList BuildOBS(const PlayerData& player, const GameState& state, const Action& prevAction);

		virtual void AddPlayerToOBSBetter(FList& obs, const PlayerData& player, bool inv, const PhysObj& ball) {
			PhysObj phys = player.GetPhys(inv);

			obs += phys.pos * posCoef;
			obs += phys.rotMat.forward;
			obs += phys.rotMat.up;
			obs += phys.vel * velCoef;
			obs += phys.angVel * angVelCoef;
			obs += (ball.pos - phys.pos) * posCoef;
			obs += (ball.vel - phys.vel) * velCoef;
			obs += phys.rotMat.Dot(ball.pos - phys.pos) * posCoef * 0.5f;
			obs += phys.rotMat.Dot(ball.vel - phys.vel) * velCoef * 0.5f;


				obs += {
				player.boostFraction,
					(float)player.carState.isOnGround,
					(float)player.hasFlip,
					(float)player.carState.isDemoed,
			};
		};
	};
}