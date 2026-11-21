// Which fly lives in this build: CMake's DENDRITE_FLY picks one folder in Source/flies/.
//   flywire: FlyWire 783, an adult female brain (CC BY-NC 4.0), the fly of 2.1 to 3.0
//   malecns: Janelia's male CNS v1.0, the brain of an adult male (CC BY 4.0)
// Each folder holds the generated wiring (FlyData), who each neuron is (Identities) and the
// atlas facts the model and the window need (Atlas.h).
#pragma once

#if defined (DENDRITE_FLY_MALECNS)
 #include "../flies/malecns/FlyData.h"
 #include "../flies/malecns/Atlas.h"
#else
 #include "../flies/flywire/FlyData.h"
 #include "../flies/flywire/Atlas.h"
#endif
