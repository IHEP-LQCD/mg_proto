#ifndef LATTICE_H
#define LATTICE_H

#include <array>

#include "lattice/constants.h"  // n_dim and friends
#include "lattice/nodeinfo.h"

#include "utils/print_utils.h"

using namespace MGUtils;

namespace MGGeometry {



class LatticeInfo {
public:

	/** Most General Constructor
	 *  \param origin   is a vector containing the coordinates of the origin of the Lattice Block
	 *  \param lat_dims is a vector containing the dimensions of the lattice block
	 *  \param n_spin   is the number of spin components of the lattice block
	 *  \param n_color  is the number of color components of the lattice block
	 *  \param node     is the NodeInfo() object for the current node.
	 */
	LatticeInfo(const IndexArray& lat_origin,
				const IndexArray& lat_dims,
				const IndexType n_spin,
				const IndexType n_color,
				const NodeInfo& node);

	/** DelegatingConstructor -- for when there is only one lattice block per node. Local origin assumed
	 *   to be ( lat_dims[0]*node_coord[0], lat_dims[1]*node_coord[1], lat_dims[2]*node_coord[2], lat_dims[3]*node_coord[3] )
	 *
	 * \param lat_dims is a vector containing the dimensions of the lattice  in sites
	 * \param n_spin is the number of spin components
	 * \param n_colo is the number of color components
	 * \param node_info has details about the node (to help work out origins
	 *                              checkerboards etc.
	 */
	LatticeInfo(const IndexArray& lat_dims,
				const IndexType n_spin,
				const IndexType n_color,
				const NodeInfo& node);


	/** Delegating Constructor
	 *  Local origin assumed to be ( lat_dims[0]*node_coord[0], lat_dims[1]*node_coord[1], lat_dims[2]*node_coord[2], lat_dims[3]*node_coord[3] )
	 *  n_spin = 4, n_color = 3, NodeInfo instantiated on demand
	 */
	LatticeInfo(const IndexArray& lat_dims);

	~LatticeInfo();

	inline
	const IndexArray& GetLatticeDimensions() const {
		return _lat_dims;
	}

	inline
	const IndexArray& GetCBLatticeDimensions() const {
		return _cb_lat_dims;
	}

	inline
	const IndexArray& GetLatticeOrigin() const {
		return _lat_origin;
	}

	inline
	IndexType GetNumColors() const {
		return _n_color;
	}

	inline
	IndexType GetNumSpins() const {
		return _n_spin;
	}

	inline
	IndexType GetNumCBSites() const {
		return _n_cb_sites;
	}

	inline
	IndexType GetNumCBSurfaceSites(IndexType mu) const {
		return _num_cb_surface_sites[mu];

	}
	inline
	IndexType GetNumSites() const {
		return _n_sites;
	}

	inline
	IndexType GetCBOrigin(void) const {
		return _orig_cb;
	}

	inline
	const NodeInfo& GetNodeInfo(void) const{
		return _node_info;
	}
private:
	IndexArray _lat_origin;
	IndexArray _lat_dims;         // The lattice dimensions (COPIED In)
	IndexArray _cb_lat_dims;
	IndexType _n_color;
	IndexType _n_spin;
	const NodeInfo& _node_info;			   	   // The Node Info -- copied in


	IndexType _n_sites;                          // The total number of sites
	IndexType _n_cb_sites;
	IndexType _sum_orig_coords;
	IndexType _orig_cb;

	IndexArray _num_cb_surface_sites;


	/* Compute Origin from NodeInfo and NodeCoords */
	inline
	IndexArray
	ComputeOriginCoords(const IndexArray& lat_dims, const NodeInfo& node_info) const {
		IndexArray origin_coords;
		const IndexArray& node_coords = node_info.NodeCoords();
		for(IndexType mu=0; mu < n_dim; ++mu) {
		   origin_coords[mu] = lat_dims[mu]*node_coords[mu];
		}

		return origin_coords;
	}
};


}

#endif
