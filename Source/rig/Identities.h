// Who each of the cut's neurons is in its atlas (see dsp/FlyData.h for which fly this build holds).
#pragma once

#if defined (DENDRITE_FLY_MALECNS)
 #include "../flies/malecns/Identities.h"
#else
 #include "../flies/flywire/Identities.h"
#endif
