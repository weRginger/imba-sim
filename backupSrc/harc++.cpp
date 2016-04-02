//
// C++ Implementation: H-ARC++
//
// Description: 1) when a write hit on a page in clean cache, we not only migrate this page to dirty cache,
// 		but also increase the desired size of dirty cache by one
//		2) a write hit on dirty ghost will enlarge dirty cache; a read hit on clean ghost will enlarge clean cache.
//		Other ghost hits do not react.
//
// Author: ziqi fan, UMN
//
#include "harc++.h"



