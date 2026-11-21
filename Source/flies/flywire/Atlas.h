// The FlyWire fly's atlas facts (written by hand: FlyData.cpp predates them and stays as generated).
#pragma once

namespace dendrite::flydata
{
// Shiu et al. 2024's synapse weight for FlyWire's synapse counts
constexpr float synapseMv = 0.275f;
// the extra weight of an inhibitory synapse: none
constexpr float inhibitoryScale = 1.0f;
// each sense group's input rate scale (B, A, CE, loom low, mid, high; each left then right): the model's own
constexpr float senseGain[12] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
// the readouts, as the reflexes read them: the grooming neurons' gain, the steering neurons' gain, the
// descending neurons' scale for the excitement and the MBONs' resting feeling; the reflexes were tuned on this fly
constexpr float groomGain = 1.0f, leanGain = 1.0f, excitedScale = 1.0f, feelingRest = 0.0f;
// the cut's box, front view (FlyWire's 4 nm voxels)
constexpr float widthMicrons = 816.0f, heightMicrons = 392.8f;
// for the window: what the specimen is and where its neurons are named
constexpr const char* specimenSex = "adult female";
constexpr const char* atlasName = "flywire 783";
constexpr const char* idSource = "FlyWire";
} // namespace dendrite::flydata
