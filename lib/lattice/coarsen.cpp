/*
 * coarsen.cpp
 *
 *  Created on: Oct 12, 2015
 *      Author: bjoo
 */

#include "lattice/coarsen.h"
#include "lattice/nodeinfo.h"
#include "utils/print_utils.h"

using namespace MGUtils;

namespace MGGeometry {

	/*!  Coarsen a lattice info given a number of vectors and an aggregation */
	LatticeInfo CoarsenLattice(const LatticeInfo& fine_geom,
							   const Aggregation& blocking,
							   unsigned int num_vec)
	{
		std::vector<unsigned int> coarse_dims(fine_geom.GetLatticeDimensions());
		std::vector<unsigned int> blocking_dims(blocking.GetBlockDimensions());

		if (coarse_dims.size() != blocking_dims.size()
				&& coarse_dims.size() != n_dim) {
			MasterLog(ERROR, "Dimensions of lattice and blocking do not match");
		}

		// Check Divisibility
		for (unsigned int mu = 0; mu < n_dim; ++mu) {
			if (coarse_dims[mu] % blocking_dims[mu] != 0) {
				MasterLog(ERROR, "blocking does not divide lattice in dimension %d",
						mu);
			} else {
				// If divisible, than divide
				coarse_dims[mu] /= blocking_dims[mu];
			}
		}

		int n_colors = num_vec; // The number of vectors is the number of coarse colors
		int n_spins = blocking.GetNumAggregates(); // The number of aggregates is the new number of spins
		LatticeInfo ret_val(coarse_dims, n_spins, n_colors, NodeInfo()); // This is the value to return. Initialize

		return ret_val;
	} // End of function

} // end of namespace

