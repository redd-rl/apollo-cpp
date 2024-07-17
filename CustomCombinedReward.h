#pragma once

#include <RLGymSim_CPP/Utils/RewardFunctions/CombinedReward.h>
#include <RLGymSim_CPP/Utils/RewardFunctions/ZeroSumReward.h>
#include <RLGymSim_CPP/Utils/BasicTypes/Lists.h>
using namespace RLGSC;

class LogCombinedReward : public CombinedReward {
public:

    bool rewardDebugMode = false;
    std::vector<float> lastRewards;

    LogCombinedReward(std::vector<std::pair<RewardFunction*, float>> funcsWithWeights, bool ownsFuncs = false)
        : CombinedReward(funcsWithWeights, ownsFuncs) {
        lastRewards.resize(rewardFuncs.size());
    }

    std::string GetRewardFuncName(int index) {
        auto func = rewardFuncs[index];
        if (dynamic_cast<ZeroSumReward*>(func))
            func = ((ZeroSumReward*)func)->childFunc;

        const char* typeName = (typeid(*func).name() + sizeof("class"));
        return typeName;
    }

    std::vector<float> GetTotalRewards(const GameState& state, const ActionSet& actions, bool final);

    virtual std::vector<float> GetAllRewards(const GameState& state, const ActionSet& actions, bool final) {
        return GetTotalRewards(state, actions, final);
    }
};
std::vector<float> LogCombinedReward::GetTotalRewards(const GameState& state, const ActionSet& actions, bool final) {

    FList totalRewards = FList(state.players.size());
    for (int i = 0; i < rewardFuncs.size(); i++) {
        auto func = rewardFuncs[i];
        float weight = rewardWeights[i];

        FList rewards = func->GetAllRewards(state, actions, final);
        float avgReward = 0;
        for (int j = 0; j < rewards.size(); j++) {
            totalRewards[j] += rewards[j] * rewardWeights[i];
            avgReward += rewards[j];
        }

        lastRewards[i] = avgReward / RS_MAX(rewards.size(), 1);
    }

    return totalRewards;
}